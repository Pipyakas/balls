#include "cpu/Zen4Backend.h"
#include "cpu/SystemMemory.h"

#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

#include <algorithm>
#include <atomic>
#include <bit>

#include <limits>
#include <optional>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

using system_memory::isMemoryLow;
using system_memory::PhysicalMemory;
using system_memory::physicalMemory;

constexpr size_t ballsPerChunk = 16384;
constexpr int paddingRowsAboveFirstRow = 1;
constexpr float coastMargin = 1e-3f;

constexpr size_t zen4LaneCount = 16;

template <int Predicate>
inline __m512 compareMask(__m512 a, __m512 b) {
    const __mmask16 mask = _mm512_cmp_ps_mask(a, b, Predicate);
    return _mm512_castsi512_ps(_mm512_maskz_set1_epi32(mask, -1));
}
inline __m512i greaterThanMask(__m512i a, __m512i b) {
    const __mmask16 mask = _mm512_cmpgt_epi32_mask(a, b);
    return _mm512_maskz_set1_epi32(mask, -1);
}
inline __m512i equalMask(__m512i a, __m512i b) {
    const __mmask16 mask = _mm512_cmpeq_epi32_mask(a, b);
    return _mm512_maskz_set1_epi32(mask, -1);
}

struct LaneConstants {
    __m512 deltaTime;
    __m512 radius;
    __m512 radiusSquared;
    __m512 twiceRadius;
    __m512 rightLimit;
    __m512 bottomLimit;
    __m512 twiceRightLimit;
    __m512 twiceBottomLimit;
    __m512 oneMinusRadius;
    __m512 one;
    __m512 minusOne;
    __m512 zero;
    __m512 halfCell;
    __m512 speed;
    __m512 embeddedEpsilon;
    __m512 retargetEpsilon;
    __m512 paddedWidthFloat;
    __m512 inversePaddedWidth;
    __m512 absMask;
    __m512i paddedWidth;
    __m512i paddedOffset;
    __m512i paddedCellCount;
    __m512i retargetThreshold;
    __m512i oneInt;
    __m512i minusOneInt;
    __m512i sentinelBrick;
    __m512 packScaleX;
    __m512 packScaleY;
    __m512 renderMax;
    __m512 positionToX;
    __m512 positionToY;
    __m512 xToPosition;
    __m512 yToPosition;
    __m512 positionMax;
    __m512 inverseSpeed;
    __m512 directionScale;
    __m512i positionMask;
    __m512i directionMask;
    __m512i colorMask;
    __m512i bounceMask;
    __m512i savedBounceLimit;
    __m512i lastNearRow;
    __m512 tickTime;
    __m512 coastLeft;
    __m512 coastRight;
    __m512 coastTop;
    __m512 coastBottom;

    LaneConstants(const StepParams& params, int sentinel, int nearRowsEnd)
        : deltaTime(_mm512_set1_ps(params.deltaTime)), radius(_mm512_set1_ps(params.radius)),
          radiusSquared(_mm512_set1_ps(params.radius * params.radius)),
          twiceRadius(_mm512_set1_ps(2.0f * params.radius)),
          rightLimit(_mm512_set1_ps(params.fieldWidth - params.radius)),
          bottomLimit(_mm512_set1_ps(params.fieldHeight - params.radius)),
          twiceRightLimit(_mm512_set1_ps(2.0f * (params.fieldWidth - params.radius))),
          twiceBottomLimit(_mm512_set1_ps(2.0f * (params.fieldHeight - params.radius))),
          oneMinusRadius(_mm512_set1_ps(1.0f - params.radius)), one(_mm512_set1_ps(1.0f)),
          minusOne(_mm512_set1_ps(-1.0f)), zero(_mm512_setzero_ps()), halfCell(_mm512_set1_ps(0.5f)),
          speed(_mm512_set1_ps(params.speed)), embeddedEpsilon(_mm512_set1_ps(1e-12f)),
          retargetEpsilon(_mm512_set1_ps(1e-8f)),
          paddedWidthFloat(_mm512_set1_ps(static_cast<float>(params.paddedWidth))),
          inversePaddedWidth(_mm512_set1_ps(1.0f / static_cast<float>(params.paddedWidth))),
          absMask(_mm512_castsi512_ps(_mm512_set1_epi32(0x7fffffff))),
          paddedWidth(_mm512_set1_epi32(params.paddedWidth)),
          paddedOffset(_mm512_set1_epi32(params.paddedWidth + 1)),
          paddedCellCount(_mm512_set1_epi32(params.paddedCellCount)),
          retargetThreshold(_mm512_set1_epi32(params.retargetBounceThreshold - 1)), oneInt(_mm512_set1_epi32(1)),
          minusOneInt(_mm512_set1_epi32(-1)), sentinelBrick(_mm512_set1_epi32(sentinel)),
          packScaleX(_mm512_set1_ps(params.packScaleX)), packScaleY(_mm512_set1_ps(params.packScaleY)),
          renderMax(_mm512_set1_ps(static_cast<float>(renderCoordinateMax))),
          positionToX(_mm512_set1_ps(params.fieldWidth / compact::positionScale)),
          positionToY(_mm512_set1_ps(params.fieldHeight / compact::positionScale)),
          xToPosition(_mm512_set1_ps(compact::positionScale / params.fieldWidth)),
          yToPosition(_mm512_set1_ps(compact::positionScale / params.fieldHeight)),
          positionMax(_mm512_set1_ps(compact::positionScale)), inverseSpeed(_mm512_set1_ps(1.0f / params.speed)),
          directionScale(_mm512_set1_ps(compact::directionScale)),
          positionMask(_mm512_set1_epi32(static_cast<int>(compact::positionMask))),
          directionMask(_mm512_set1_epi32(static_cast<int>(compact::directionMask))),
          colorMask(_mm512_set1_epi32(static_cast<int>(compact::colorMask))),
          bounceMask(_mm512_set1_epi32(static_cast<int>(compact::bounceMask))),
          savedBounceLimit(_mm512_set1_epi32(compact::savedBounceLimit)),
          lastNearRow(_mm512_set1_epi32(nearRowsEnd - paddingRowsAboveFirstRow - 1)),
          tickTime(_mm512_set1_ps(params.deltaTime * static_cast<float>(params.substeps))),
          coastLeft(_mm512_set1_ps(params.radius + coastMargin)),
          coastRight(_mm512_set1_ps(params.fieldWidth - params.radius - coastMargin)),
          coastTop(_mm512_set1_ps(std::max(params.radius, static_cast<float>(nearRowsEnd - paddingRowsAboveFirstRow)) +
                                  coastMargin)),
          coastBottom(_mm512_set1_ps(params.fieldHeight - params.radius - coastMargin)) {}
};

inline __m512 select(__m512 mask, __m512 whenTrue, __m512 whenFalse) {
    const __mmask16 m = _mm512_movepi32_mask(_mm512_castps_si512(mask));
    return _mm512_mask_blend_ps(m, whenFalse, whenTrue);
}

inline __m512i selectInt(__m512 mask, __m512i whenTrue, __m512i whenFalse) {
    const __mmask16 m = _mm512_movepi32_mask(_mm512_castps_si512(mask));
    return _mm512_mask_blend_epi32(m, whenFalse, whenTrue);
}

inline __m512i packForRender(__m512 x, __m512 y, __m512i color, const LaneConstants& k) {
    const __m512i packedX = _mm512_cvtps_epi32(_mm512_min_ps(_mm512_max_ps(_mm512_mul_ps(x, k.packScaleX), k.zero), k.renderMax));
    const __m512i packedY = _mm512_cvtps_epi32(_mm512_min_ps(_mm512_max_ps(_mm512_mul_ps(y, k.packScaleY), k.zero), k.renderMax));
    return _mm512_or_si512(_mm512_or_si512(packedX, _mm512_slli_epi32(packedY, renderCoordinateBits)),
                           _mm512_slli_epi32(color, renderColorShift));
}

struct GridTarget {
    uint32_t* slots;
    int width;
    __m512i maxColumn;
    __m512i maxRow;
    __m512 coordinateToColumn;
    __m512 coordinateToRow;
    __m512i coordinateMask;
    __m512i widthLanes;
    __m512i seed;
    __m512i bidderMask;
    __m512i laneOffsets;

    GridTarget(CompactBallStore& store)
        : slots(store.gridFilled ? store.grid.data() : nullptr), width(store.gridWidth),
          maxColumn(_mm512_set1_epi32(store.gridWidth - 1)), maxRow(_mm512_set1_epi32(store.gridHeight - 1)),
          coordinateToColumn(_mm512_set1_ps(static_cast<float>(store.gridWidth) / (renderCoordinateMax + 1))),
          coordinateToRow(_mm512_set1_ps(static_cast<float>(store.gridHeight) / (renderCoordinateMax + 1))),
          coordinateMask(_mm512_set1_epi32(static_cast<int>(renderCoordinateMax))),
          widthLanes(_mm512_set1_epi32(store.gridWidth)), seed(_mm512_set1_epi32(static_cast<int>(store.frameSeed))),
          bidderMask(_mm512_set1_epi32(static_cast<int>(store.bidderMask))),
          laneOffsets(_mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)) {}

    unsigned bidders(size_t first) const {
        __m512i hash = _mm512_xor_si512(_mm512_add_epi32(_mm512_set1_epi32(static_cast<int>(first)), laneOffsets), seed);
        hash = _mm512_xor_si512(hash, _mm512_srli_epi32(hash, 16));
        hash = _mm512_mullo_epi32(hash, _mm512_set1_epi32(0x7feb352d));
        hash = _mm512_xor_si512(hash, _mm512_srli_epi32(hash, 15));
        hash = _mm512_mullo_epi32(hash, _mm512_set1_epi32(static_cast<int>(0x846ca68bu)));
        hash = _mm512_xor_si512(hash, _mm512_srli_epi32(hash, 16));
        const __m512i rejected = _mm512_and_si512(hash, bidderMask);
        return static_cast<unsigned>(
            _mm512_movepi32_mask(equalMask(rejected, _mm512_setzero_si512())));
    }

    void place(__m512i packed, unsigned lanes) const {
        const __m512i column = _mm512_min_epi32(
            _mm512_cvttps_epi32(_mm512_mul_ps(_mm512_cvtepi32_ps(_mm512_and_si512(packed, coordinateMask)), coordinateToColumn)),
            maxColumn);
        const __m512i row = _mm512_min_epi32(
            _mm512_cvttps_epi32(_mm512_mul_ps(
                _mm512_cvtepi32_ps(_mm512_and_si512(_mm512_srli_epi32(packed, renderCoordinateBits), coordinateMask)),
                coordinateToRow)),
            maxRow);
        alignas(64) int32_t slotIndex[zen4LaneCount];
        alignas(64) uint32_t values[zen4LaneCount];
        _mm512_store_si512(reinterpret_cast<__m512i*>(slotIndex), _mm512_add_epi32(_mm512_mullo_epi32(row, widthLanes), column));
        _mm512_store_si512(reinterpret_cast<__m512i*>(values), packed);
        while (lanes != 0) {
            const int lane = std::countr_zero(lanes);
            lanes &= lanes - 1;
            std::atomic_ref<uint32_t>(slots[slotIndex[lane]]).store(values[lane], std::memory_order_relaxed);
        }
    }
};

inline __m512 lessThan(__m512 a, __m512 b) { return compareMask<_CMP_LT_OQ>(a, b); }
inline __m512 greaterThan(__m512 a, __m512 b) { return compareMask<_CMP_GT_OQ>(a, b); }

struct ContactSearch {
    __m512 distanceSquared;
    __m512 offsetX;
    __m512 offsetY;
    __m512i column;
    __m512i row;
    __m512i brick;
};

inline void considerCell(ContactSearch& best, const int32_t* paddedCells, __m512 x, __m512 y, __m512i column,
                         __m512i row, __mmask16 lanes, const LaneConstants& k) {
    const __m512i index = _mm512_add_epi32(_mm512_mullo_epi32(row, k.paddedWidth), _mm512_add_epi32(column, k.paddedOffset));
    const __m512i brick = _mm512_mask_i32gather_epi32(k.minusOneInt, lanes, index, paddedCells, 4);
    const __m512 occupied = _mm512_castsi512_ps(greaterThanMask(brick, k.minusOneInt));
    const __m512 cellLeft = _mm512_cvtepi32_ps(column);
    const __m512 cellTop = _mm512_cvtepi32_ps(row);
    const __m512 closestX = _mm512_min_ps(_mm512_max_ps(x, cellLeft), _mm512_add_ps(cellLeft, k.one));
    const __m512 closestY = _mm512_min_ps(_mm512_max_ps(y, cellTop), _mm512_add_ps(cellTop, k.one));
    const __m512 offsetX = _mm512_sub_ps(x, closestX);
    const __m512 offsetY = _mm512_sub_ps(y, closestY);
    const __m512 distanceSquared = _mm512_fmadd_ps(offsetX, offsetX, _mm512_mul_ps(offsetY, offsetY));
    const __m512 closer = _mm512_and_ps(occupied, lessThan(distanceSquared, best.distanceSquared));
    best.distanceSquared = select(closer, distanceSquared, best.distanceSquared);
    best.offsetX = select(closer, offsetX, best.offsetX);
    best.offsetY = select(closer, offsetY, best.offsetY);
    best.column = selectInt(closer, column, best.column);
    best.row = selectInt(closer, row, best.row);
    best.brick = selectInt(closer, brick, best.brick);
}

}

Zen4Backend::Zen4Backend(size_t threadCount) : pool(std::make_unique<WorkerPool>(threadCount)) {
}

void Zen4Backend::setVisibilityGrid(int width, int height) {
    store.gridWidth = std::max(width, 0);
    store.gridHeight = std::max(height, 0);
}

void Zen4Backend::shareCoresWithGpu() {
    const size_t threads = pool->threadCount();
    pool = std::make_unique<WorkerPool>(threads > 1 ? threads - 1 : 1, WorkerPriority::Background);
}

bool Zen4Backend::isSupportedByCpu() {
#if defined(_MSC_VER)
    int registers[4]{};
    __cpuidex(registers, 7, 0);
    const unsigned ebx7 = registers[1];
    const unsigned ecx7 = registers[2];
    const bool hasF = (ebx7 & (1u << 16)) != 0;
    const bool hasDq = (ebx7 & (1u << 17)) != 0;
    const bool hasBw = (ebx7 & (1u << 30)) != 0;
    const bool hasVl = (ebx7 & (1u << 31)) != 0;
    const bool hasVbmi2 = (ecx7 & (1u << 1)) != 0;
    const bool hasVpopcntdq = (ecx7 & (1u << 14)) != 0;
    const bool hasBmi2 = (ebx7 & (1u << 8)) != 0;
    __cpuid(registers, 1);
    const bool hasFma = (registers[2] & (1 << 12)) != 0;
    const unsigned xcr0 = static_cast<unsigned>(_xgetbv(0));
    const bool osSupport = (xcr0 & 0xE6u) == 0xE6u;
    return hasF && hasDq && hasBw && hasVl && hasVbmi2 && hasVpopcntdq && hasBmi2 && hasFma && osSupport;
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
    const bool hasFma = (ecx & (1u << 12)) != 0;
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return false;
    const bool hasF = (ebx & (1u << 16)) != 0;
    const bool hasDq = (ebx & (1u << 17)) != 0;
    const bool hasBw = (ebx & (1u << 30)) != 0;
    const bool hasVl = (ebx & (1u << 31)) != 0;
    const bool hasVbmi2 = (ecx & (1u << 1)) != 0;
    const bool hasVpopcntdq = (ecx & (1u << 14)) != 0;
    const bool hasBmi2 = (ebx & (1u << 8)) != 0;
    unsigned long long xcr0 = __builtin_ia32_xgetbv(0);
    const bool osSupport = (xcr0 & 0xE6ull) == 0xE6ull;
    return hasF && hasDq && hasBw && hasVl && hasVbmi2 && hasVpopcntdq && hasBmi2 && hasFma && osSupport;
#else
    return false;
#endif
}

void Zen4Backend::materializeFreeFlight() {
    if (!store.frozen) {
        return;
    }
    pool->runChunks(store.count, ballsPerChunk,
                    [&](size_t, size_t begin, size_t end) { store.materializeRange(begin, end); });
    store.frozen = false;
}

size_t Zen4Backend::ballCapacityLimit() const {
    if (store.isPreallocated()) {
        return store.capacity();
    }
    constexpr size_t bytesPerBall = 8;
    constexpr size_t minimumMarginBytes = size_t{1} << 30;
    constexpr size_t marginDivisor = 32;
    if (isMemoryLow()) {
        return store.count;
    }
    const std::optional<PhysicalMemory> memory = physicalMemory();
    if (!memory) {
        return std::numeric_limits<size_t>::max();
    }
    const size_t margin = std::max(minimumMarginBytes, memory->total / marginDivisor);
    const size_t spare = memory->available > margin ? memory->available - margin : 0;
    return store.count + spare / bytesPerBall;
}

size_t Zen4Backend::ballHardLimit() const {
    if (store.isPreallocated()) {
        return store.capacity();
    }
    constexpr size_t bytesPerBall = 8;
    constexpr size_t bytesPerArrayElement = 4;
    constexpr size_t marginBytes = size_t{512} << 20;
    const std::optional<size_t> committable = system_memory::committableBytes();
    const std::optional<PhysicalMemory> memory = physicalMemory();
    if (!committable || !memory) {
        return ballCapacityLimit();
    }
    const size_t spare = *committable > marginBytes ? *committable - marginBytes : 0;
    return std::max(ballCapacityLimit(), std::min(store.count + spare / bytesPerBall, memory->total / bytesPerArrayElement));
}

size_t Zen4Backend::preallocateBalls() {
    if (store.isPreallocated()) {
        return store.capacity();
    }
    if (isMemoryLow()) {
        return 0;
    }
    const std::optional<PhysicalMemory> memory = physicalMemory();
    if (!memory) {
        return 0;
    }
    constexpr size_t minimumMarginBytes = size_t{1} << 30;
    constexpr size_t marginDivisor = 32;
    const size_t margin = std::max(minimumMarginBytes, memory->total / marginDivisor);
    const size_t spare = memory->available > margin ? memory->available - margin : 0;
    if (spare == 0) {
        return 0;
    }
    constexpr size_t commitGranule = size_t{64} << 20;
    const size_t bytesPerArray = (spare / 2 / commitGranule) * commitGranule;
    if (bytesPerArray == 0) {
        return 0;
    }
    const size_t targetBalls = bytesPerArray / sizeof(uint32_t);
    return store.preallocate(targetBalls, pool.get());
}

void Zen4Backend::step(const StepParams& params, std::vector<BallHit>& hits) {
    store.clockTick = params.tick + static_cast<unsigned int>(params.substeps);
    store.flightParams = params;
    if (store.count == 0) {
        return;
    }
    store.speed = params.speed;

    const bool gridded = store.prepareGrid();
    if (params.freeFlight && gridded) {
        store.freeze(params.tick);
        constexpr size_t samplesPerTask = 64;
        pool->runChunks(store.sampledChunkCount(), samplesPerTask,
                        [&](size_t, size_t first, size_t end) { store.renderSampledChunks(first, end); });
        return;
    }
    materializeFreeFlight();
    if (store.count <= ballsPerChunk) {
        stepRange(params, 0, store.count, hits);
        return;
    }
    chunkHits.resize(std::max(chunkHits.size(), WorkerPool::chunkCountFor(store.count, ballsPerChunk)));
    pool->runChunks(store.count, ballsPerChunk, [&](size_t chunk, size_t begin, size_t end) {
        chunkHits[chunk].clear();
        stepRange(params, begin, end, chunkHits[chunk]);
    });
    const size_t usedChunks = WorkerPool::chunkCountFor(store.count, ballsPerChunk);
    for (size_t chunk = 0; chunk < usedChunks; ++chunk) {
        hits.insert(hits.end(), chunkHits[chunk].begin(), chunkHits[chunk].end());
    }
}

namespace {

struct LaneState {
    __m512 x;
    __m512 y;
    __m512 vx;
    __m512 vy;
    __m512i color;
    __m512i bounces;
};

struct LaneStepResult {
    __m512 hit;
    __m512 embedded;
    __m512i brick;
    __m512i column;
    __m512i row;
};

LaneStepResult stepLanes(LaneState& state, const LaneConstants& k, const BoardView& board) {
    __m512 x = _mm512_fmadd_ps(state.vx, k.deltaTime, state.x);
    __m512 y = _mm512_fmadd_ps(state.vy, k.deltaTime, state.y);
    __m512 vx = state.vx;
    __m512 vy = state.vy;

    const __m512 hitLeft = lessThan(x, k.radius);
    const __m512 hitRight = _mm512_andnot_ps(hitLeft, greaterThan(x, k.rightLimit));
    const __m512 hitTop = lessThan(y, k.radius);
    const __m512 hitBottom = _mm512_andnot_ps(hitTop, greaterThan(y, k.bottomLimit));
    const __m512 absVx = _mm512_and_ps(vx, k.absMask);
    const __m512 absVy = _mm512_and_ps(vy, k.absMask);
    x = select(hitLeft, _mm512_sub_ps(k.twiceRadius, x), select(hitRight, _mm512_sub_ps(k.twiceRightLimit, x), x));
    y = select(hitTop, _mm512_sub_ps(k.twiceRadius, y), select(hitBottom, _mm512_sub_ps(k.twiceBottomLimit, y), y));
    vx = select(hitLeft, absVx, select(hitRight, _mm512_sub_ps(k.zero, absVx), vx));
    vy = select(hitTop, absVy, select(hitBottom, _mm512_sub_ps(k.zero, absVy), vy));
    __m512 normalX = select(hitLeft, k.one, select(hitRight, k.minusOne, k.zero));
    __m512 normalY = select(hitTop, k.one, select(hitBottom, k.minusOne, k.zero));
    __m512 bounced = _mm512_or_ps(_mm512_or_ps(hitLeft, hitRight), _mm512_or_ps(hitTop, hitBottom));

    const __m512 floorX = _mm512_floor_ps(x);
    const __m512 floorY = _mm512_floor_ps(y);
    const __m512i column = _mm512_cvttps_epi32(floorX);
    const __m512i row = _mm512_cvttps_epi32(floorY);
    const __m512 fractionX = _mm512_sub_ps(x, floorX);
    const __m512 fractionY = _mm512_sub_ps(y, floorY);
    const __m512i neighborColumn = _mm512_sub_epi32(_mm512_castps_si512(lessThan(fractionX, k.radius)),
                                                    _mm512_castps_si512(greaterThan(fractionX, k.oneMinusRadius)));
    const __m512i neighborRow = _mm512_sub_epi32(_mm512_castps_si512(lessThan(fractionY, k.radius)),
                                                 _mm512_castps_si512(greaterThan(fractionY, k.oneMinusRadius)));
    const __m512i otherColumn = _mm512_add_epi32(column, neighborColumn);
    const __m512i otherRow = _mm512_add_epi32(row, neighborRow);

    ContactSearch best{k.radiusSquared, k.zero, k.zero, column, row, k.minusOneInt};
    __m512 sameColor = k.zero;
    __m512 contact = k.zero;
    __m512 embedded = k.zero;
    const __m512i maybeNear = _mm512_xor_si512(greaterThanMask(row, k.lastNearRow), k.minusOneInt);
    const __m512i cellIndex = _mm512_add_epi32(_mm512_mullo_epi32(row, k.paddedWidth), _mm512_add_epi32(column, k.paddedOffset));
    const __m512i nearBrick = (_mm512_test_epi32_mask(maybeNear, maybeNear) == 0)
                                  ? _mm512_setzero_si512()
                                  : _mm512_i32gather_epi32(cellIndex, board.nearBrickCells, 4);

    const __mmask16 nearLanes = _mm512_test_epi32_mask(nearBrick, nearBrick);
    if (nearLanes != 0) {
        const __mmask16 awayColumn = _mm512_mask_test_epi32_mask(nearLanes, neighborColumn, neighborColumn);
        const __mmask16 awayRow = _mm512_mask_test_epi32_mask(nearLanes, neighborRow, neighborRow);
        considerCell(best, board.paddedCells, x, y, column, row, nearLanes, k);
        if (awayColumn != 0) {
            considerCell(best, board.paddedCells, x, y, otherColumn, row, awayColumn, k);
        }
        if (awayRow != 0) {
            considerCell(best, board.paddedCells, x, y, column, otherRow, awayRow, k);
        }
        if (const __mmask16 awayBoth = awayColumn & awayRow; awayBoth != 0) {
            considerCell(best, board.paddedCells, x, y, otherColumn, otherRow, awayBoth, k);
        }

        contact = _mm512_castsi512_ps(greaterThanMask(best.brick, k.minusOneInt));
        const __m512i gatherIndex = selectInt(contact, best.brick, k.sentinelBrick);
        const __m512i brickColor = _mm512_i32gather_epi32(gatherIndex, board.brickColors, 4);
        sameColor = _mm512_castsi512_ps(equalMask(brickColor, state.color));
        embedded = _mm512_andnot_ps(
            sameColor, _mm512_and_ps(contact, compareMask<_CMP_LE_OQ>(best.distanceSquared, k.embeddedEpsilon)));
        const __m512 reflect = _mm512_andnot_ps(embedded, _mm512_andnot_ps(sameColor, contact));

        const __m512 distance = _mm512_sqrt_ps(_mm512_max_ps(best.distanceSquared, k.embeddedEpsilon));
        const __m512 inverseDistance = _mm512_div_ps(k.one, distance);
        const __m512 contactNormalX = _mm512_mul_ps(best.offsetX, inverseDistance);
        const __m512 contactNormalY = _mm512_mul_ps(best.offsetY, inverseDistance);
        const __m512 push = _mm512_sub_ps(k.radius, distance);
        x = select(reflect, _mm512_fmadd_ps(contactNormalX, push, x), x);
        y = select(reflect, _mm512_fmadd_ps(contactNormalY, push, y), y);
        normalX = select(reflect, contactNormalX, normalX);
        normalY = select(reflect, contactNormalY, normalY);
        const __m512 alongNormal = _mm512_fmadd_ps(vx, normalX, _mm512_mul_ps(vy, normalY));
        const __m512 approaching = _mm512_and_ps(reflect, lessThan(alongNormal, k.zero));
        const __m512 twiceAlong = _mm512_add_ps(alongNormal, alongNormal);
        vx = select(approaching, _mm512_fnmadd_ps(twiceAlong, normalX, vx), vx);
        vy = select(approaching, _mm512_fnmadd_ps(twiceAlong, normalY, vy), vy);
        bounced = _mm512_andnot_ps(_mm512_or_ps(sameColor, embedded), _mm512_or_ps(bounced, reflect));
    }

    const __m512i bounces = _mm512_sub_epi32(state.bounces, _mm512_castps_si512(bounced));
    const __m512 retarget =
        _mm512_and_ps(bounced, _mm512_castsi512_ps(greaterThanMask(bounces, k.retargetThreshold)));
    if (!(_mm512_movepi32_mask(_mm512_castps_si512(retarget)) == 0)) {
        const __m512i cellColumn = _mm512_cvttps_epi32(_mm512_floor_ps(x));
        const __m512i cellRow = _mm512_cvttps_epi32(_mm512_floor_ps(y));
        const __m512i targetCellIndex = _mm512_add_epi32(_mm512_mullo_epi32(cellRow, k.paddedWidth),
                                                   _mm512_add_epi32(cellColumn, k.paddedOffset));
        const __m512i fieldIndex = _mm512_add_epi32(_mm512_mullo_epi32(state.color, k.paddedCellCount), targetCellIndex);
        const __mmask16 retargetMask = _mm512_movepi32_mask(_mm512_castps_si512(retarget));
        const __m512i target = _mm512_mask_i32gather_epi32(k.minusOneInt, retargetMask, fieldIndex, board.targetCells, 4);
        const __m512 targetFloat = _mm512_cvtepi32_ps(target);
        const __m512 targetRow =
            _mm512_floor_ps(_mm512_mul_ps(_mm512_add_ps(targetFloat, k.halfCell), k.inversePaddedWidth));
        const __m512 targetColumn = _mm512_fnmadd_ps(targetRow, k.paddedWidthFloat, targetFloat);
        const __m512 toTargetX = _mm512_sub_ps(_mm512_sub_ps(targetColumn, k.halfCell), x);
        const __m512 toTargetY = _mm512_sub_ps(_mm512_sub_ps(targetRow, k.halfCell), y);
        const __m512 lengthSquared = _mm512_fmadd_ps(toTargetX, toTargetX, _mm512_mul_ps(toTargetY, toTargetY));
        const __m512 inverseLength =
            _mm512_div_ps(k.one, _mm512_sqrt_ps(_mm512_max_ps(lengthSquared, k.retargetEpsilon)));
        const __m512 directionX = _mm512_mul_ps(toTargetX, inverseLength);
        const __m512 directionY = _mm512_mul_ps(toTargetY, inverseLength);
        const __m512 facing = compareMask<_CMP_GE_OQ>(
            _mm512_fmadd_ps(directionX, normalX, _mm512_mul_ps(directionY, normalY)), k.zero);
        const __m512 hasTarget = _mm512_castsi512_ps(greaterThanMask(target, k.minusOneInt));
        __m512 apply = _mm512_and_ps(_mm512_and_ps(retarget, hasTarget),
                                     _mm512_and_ps(facing, greaterThan(lengthSquared, k.retargetEpsilon)));

        if (unsigned lanes = static_cast<unsigned>(_mm512_movepi32_mask(_mm512_castps_si512(apply))); lanes != 0) {
            alignas(64) float laneX[zen4LaneCount];
            alignas(64) float laneY[zen4LaneCount];
            alignas(64) int32_t laneTarget[zen4LaneCount];
            alignas(64) int32_t inSight[zen4LaneCount] = {};
            _mm512_store_ps(laneX, x);
            _mm512_store_ps(laneY, y);
            _mm512_store_si512(reinterpret_cast<__m512i*>(laneTarget), target);
            const int paddedWidth = _mm512_cvtsi512_si32(k.paddedWidth);
            while (lanes != 0) {
                const int lane = std::countr_zero(lanes);
                lanes &= lanes - 1;
                inSight[lane] = targetInSight(laneX[lane], laneY[lane], laneTarget[lane] % paddedWidth - 1,
                                              laneTarget[lane] / paddedWidth - 1, board, paddedWidth)
                                    ? -1
                                    : 0;
            }
            apply = _mm512_and_ps(apply, _mm512_castsi512_ps(_mm512_load_si512(reinterpret_cast<const __m512i*>(inSight))));
        }
        vx = select(apply, _mm512_mul_ps(directionX, k.speed), vx);
        vy = select(apply, _mm512_mul_ps(directionY, k.speed), vy);
    }

    state.x = x;
    state.y = y;
    state.vx = vx;
    state.vy = vy;
    state.bounces = bounces;
    return {_mm512_and_ps(sameColor, contact), embedded, best.brick, best.column, best.row};
}

bool coastsWholeTick(const LaneState& state, const LaneConstants& k) {
    const __m512 endX = _mm512_fmadd_ps(state.vx, k.tickTime, state.x);
    const __m512 endY = _mm512_fmadd_ps(state.vy, k.tickTime, state.y);
    const __m512 insideX = _mm512_and_ps(greaterThan(endX, k.coastLeft), lessThan(endX, k.coastRight));
    const __m512 insideY = _mm512_and_ps(greaterThan(_mm512_min_ps(endY, state.y), k.coastTop), lessThan(endY, k.coastBottom));
    return _mm512_movepi32_mask(_mm512_castps_si512(_mm512_and_ps(insideX, insideY))) == 0xffff;
}

void coast(LaneState& state, const LaneConstants& k, int substeps) {
    for (int substep = 0; substep < substeps; ++substep) {
        state.x = _mm512_fmadd_ps(state.vx, k.deltaTime, state.x);
        state.y = _mm512_fmadd_ps(state.vy, k.deltaTime, state.y);
    }
}

LaneState decodeLanes(__m512i position, __m512i state, const LaneConstants& k) {
    const __m512 x = _mm512_mul_ps(_mm512_cvtepi32_ps(_mm512_and_si512(position, k.positionMask)), k.positionToX);
    const __m512 y = _mm512_mul_ps(_mm512_cvtepi32_ps(_mm512_srli_epi32(position, 16)), k.positionToY);
    constexpr int signShift = 32 - compact::directionBits;
    const __m512 dx = _mm512_cvtepi32_ps(_mm512_srai_epi32(_mm512_slli_epi32(state, signShift), signShift));
    const __m512 dy = _mm512_cvtepi32_ps(
        _mm512_srai_epi32(_mm512_slli_epi32(state, signShift - compact::directionBits), signShift));
    const __m512 lengthSquared = _mm512_max_ps(_mm512_fmadd_ps(dx, dx, _mm512_mul_ps(dy, dy)), k.one);
    const __m512 scale = _mm512_div_ps(k.speed, _mm512_sqrt_ps(lengthSquared));
    return {x,
            y,
            _mm512_mul_ps(dx, scale),
            _mm512_mul_ps(dy, scale),
            _mm512_and_si512(_mm512_srli_epi32(state, compact::colorShift), k.colorMask),
            _mm512_and_si512(_mm512_srli_epi32(state, compact::bounceShift), k.bounceMask)};
}

__m512i roundingDitherLanes(__m512i previousPosition) {
    __m512i hash = _mm512_mullo_epi32(previousPosition, _mm512_set1_epi32(static_cast<int>(0x9E3779B1u)));
    hash = _mm512_xor_si512(hash, _mm512_srli_epi32(hash, 15));
    hash = _mm512_mullo_epi32(hash, _mm512_set1_epi32(static_cast<int>(0x85EBCA77u)));
    hash = _mm512_xor_si512(hash, _mm512_srli_epi32(hash, 13));
    hash = _mm512_mullo_epi32(hash, _mm512_set1_epi32(static_cast<int>(0xC2B2AE3Du)));
    return _mm512_xor_si512(hash, _mm512_srli_epi32(hash, 16));
}

__m512i encodeAxisDitheredLanes(__m512 value, __m512 toPosition, __m512i dither16, const LaneConstants& k) {
    const __m512 fraction = _mm512_mul_ps(_mm512_cvtepi32_ps(_mm512_and_si512(dither16, _mm512_set1_epi32(0xFFFF))),
                                          _mm512_set1_ps(1.0f / 65536.0f));
    const __m512 scaled = _mm512_fmadd_ps(value, toPosition, fraction);
    return _mm512_cvttps_epi32(_mm512_min_ps(_mm512_max_ps(scaled, k.zero), k.positionMax));
}

__m512i encodeSteppedPositionLanes(const LaneState& lanes, __m512i previousPosition, const LaneConstants& k) {
    const __m512i dither = roundingDitherLanes(previousPosition);
    return _mm512_or_si512(encodeAxisDitheredLanes(lanes.x, k.xToPosition, dither, k),
                           _mm512_slli_epi32(encodeAxisDitheredLanes(lanes.y, k.yToPosition,
                                                                     _mm512_srli_epi32(dither, 16), k),
                                             16));
}

__m512i encodeComponentLanes(__m512 velocity, const LaneConstants& k) {
    const __m512 unit = _mm512_min_ps(_mm512_max_ps(_mm512_mul_ps(velocity, k.inverseSpeed), k.minusOne), k.one);
    return _mm512_and_si512(_mm512_cvtps_epi32(_mm512_mul_ps(unit, k.directionScale)), k.directionMask);
}

__m512i encodeStateLanes(const LaneState& lanes, const LaneConstants& k) {
    const __m512i savedBounces = _mm512_min_epi32(lanes.bounces, k.savedBounceLimit);
    return _mm512_or_si512(
        _mm512_or_si512(encodeComponentLanes(lanes.vx, k),
                        _mm512_slli_epi32(encodeComponentLanes(lanes.vy, k), compact::directionBits)),
        _mm512_or_si512(_mm512_slli_epi32(lanes.color, compact::colorShift),
                        _mm512_slli_epi32(savedBounces, compact::bounceShift)));
}

LaneState blendState(__m512 mask, const LaneState& whenTrue, const LaneState& whenFalse) {
    return {select(mask, whenTrue.x, whenFalse.x),   select(mask, whenTrue.y, whenFalse.y),
            select(mask, whenTrue.vx, whenFalse.vx), select(mask, whenTrue.vy, whenFalse.vy),
            whenTrue.color,                          selectInt(mask, whenTrue.bounces, whenFalse.bounces)};
}

void resolveEmbeddedLanes(LaneState& state, unsigned lanes, const LaneStepResult& result, const BoardView& board,
                          const StepParams& params) {
    alignas(64) float x[zen4LaneCount];
    alignas(64) float y[zen4LaneCount];
    alignas(64) float vx[zen4LaneCount];
    alignas(64) float vy[zen4LaneCount];
    alignas(64) int32_t color[zen4LaneCount];
    alignas(64) int32_t bounces[zen4LaneCount];
    alignas(64) int32_t columns[zen4LaneCount];
    alignas(64) int32_t rows[zen4LaneCount];
    _mm512_store_ps(x, state.x);
    _mm512_store_ps(y, state.y);
    _mm512_store_ps(vx, state.vx);
    _mm512_store_ps(vy, state.vy);
    _mm512_store_si512(reinterpret_cast<__m512i*>(color), state.color);
    _mm512_store_si512(reinterpret_cast<__m512i*>(bounces), state.bounces);
    _mm512_store_si512(reinterpret_cast<__m512i*>(columns), result.column);
    _mm512_store_si512(reinterpret_cast<__m512i*>(rows), result.row);
    while (lanes != 0) {
        const int lane = std::countr_zero(lanes);
        lanes &= lanes - 1;
        resolveEmbeddedContact(x[lane], y[lane], vx[lane], vy[lane], color[lane], bounces[lane], columns[lane],
                               rows[lane], board, params);
    }
    state.x = _mm512_load_ps(x);
    state.y = _mm512_load_ps(y);
    state.vx = _mm512_load_ps(vx);
    state.vy = _mm512_load_ps(vy);
    state.bounces = _mm512_load_si512(reinterpret_cast<const __m512i*>(bounces));
}

}

void Zen4Backend::stepRange(const StepParams& params, size_t begin, size_t end, std::vector<BallHit>& hits) {
    const BoardView board = boardView();
    const LaneConstants k(params, cells.sentinelBrick(), cells.nearRowsEnd);
    uint32_t* const positions = store.position.data();
    uint32_t* const states = store.state.data();
    uint32_t* const packedPositions = store.packed.data();
    const size_t validCount = store.count;
    const GridTarget grid(store);
    const __m512 allLanes = _mm512_castsi512_ps(k.minusOneInt);

    for (size_t i = begin; i < end; i += zen4LaneCount) {
        const __m512i startState = _mm512_load_si512(reinterpret_cast<const __m512i*>(states + i));
        const __m512i startPosition = _mm512_load_si512(reinterpret_cast<const __m512i*>(positions + i));
        LaneState state = decodeLanes(startPosition, startState, k);
        const __m512i startBounces = state.bounces;
        __m512 active = allLanes;
        __m512i hitBrick = k.minusOneInt;
        const bool coasting = coastsWholeTick(state, k);
        if (coasting) {
            coast(state, k, params.substeps);
        }
        for (int substep = 0; substep < (coasting ? 0 : params.substeps); ++substep) {
            const LaneState before = state;
            const LaneStepResult result = stepLanes(state, k, board);
            state = blendState(active, state, before);
            const __m512 newHits = _mm512_and_ps(result.hit, active);
            hitBrick = selectInt(newHits, result.brick, hitBrick);
            const unsigned embeddedLanes =
                static_cast<unsigned>(_mm512_movepi32_mask(_mm512_castps_si512(_mm512_and_ps(result.embedded, active))));
            if (embeddedLanes != 0) {
                resolveEmbeddedLanes(state, embeddedLanes, result, board, params);
            }
            active = _mm512_andnot_ps(newHits, active);
            if ((_mm512_movepi32_mask(_mm512_castps_si512(active)) == 0)) {
                break;
            }
        }

        _mm512_store_si512(reinterpret_cast<__m512i*>(positions + i), encodeSteppedPositionLanes(state, startPosition, k));
        const __mmask16 stateChangedMask = _mm512_cmpneq_epi32_mask(state.bounces, startBounces);
        if (stateChangedMask) {
            _mm512_mask_store_epi32(reinterpret_cast<int*>(states + i), stateChangedMask, encodeStateLanes(state, k));
        }
        const unsigned laneLimit = i + zen4LaneCount <= validCount ? 0xffffu : ((1u << (validCount - i)) - 1u);
        const __m512i packed = packForRender(state.x, state.y, state.color, k);
        if (grid.slots) {
            const unsigned bidding = grid.bidders(i) & laneLimit;
            if (bidding != 0) {
                grid.place(packed, bidding);
            }
        } else {
            _mm512_stream_si512(reinterpret_cast<__m512i*>(packedPositions + i), packed);
        }

        unsigned hitLanes = static_cast<unsigned>(_mm512_movepi32_mask(greaterThanMask(hitBrick, k.minusOneInt))) &
                            laneLimit;
        if (hitLanes != 0) {
            alignas(64) int32_t bricks[zen4LaneCount];
            _mm512_store_si512(reinterpret_cast<__m512i*>(bricks), hitBrick);
            while (hitLanes != 0) {
                const int lane = std::countr_zero(hitLanes);
                hitLanes &= hitLanes - 1;
                hits.push_back({static_cast<uint32_t>(i + static_cast<size_t>(lane)), bricks[lane]});
            }
        }
    }
    _mm_sfence();
}
