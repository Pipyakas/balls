#include "cpu/Avx2Backend.h"

#include <immintrin.h>

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

struct PhysicalMemory {
    size_t total = 0;
    size_t available = 0;
};

bool isMemoryLow() {
#if defined(_WIN32)
    static const HANDLE lowMemory = CreateMemoryResourceNotification(LowMemoryResourceNotification);
    BOOL low = FALSE;
    return lowMemory && QueryMemoryResourceNotification(lowMemory, &low) && low;
#else
    return false;
#endif
}

std::optional<PhysicalMemory> physicalMemory() {
#if defined(_WIN32)
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) {
        return std::nullopt;
    }
    return PhysicalMemory{static_cast<size_t>(memory.ullTotalPhys), static_cast<size_t>(memory.ullAvailPhys)};
#else
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long availablePages = sysconf(_SC_AVPHYS_PAGES);
    if (pageSize <= 0 || pages <= 0 || availablePages < 0) {
        return std::nullopt;
    }
    return PhysicalMemory{static_cast<size_t>(pages) * static_cast<size_t>(pageSize),
                          static_cast<size_t>(availablePages) * static_cast<size_t>(pageSize)};
#endif
}

constexpr size_t ballsPerChunk = 16384;
constexpr int paddingRowsAboveFirstRow = 1;
constexpr float coastMargin = 1e-3f;

struct LaneConstants {
    __m256 deltaTime;
    __m256 radius;
    __m256 radiusSquared;
    __m256 twiceRadius;
    __m256 rightLimit;
    __m256 bottomLimit;
    __m256 twiceRightLimit;
    __m256 twiceBottomLimit;
    __m256 oneMinusRadius;
    __m256 one;
    __m256 minusOne;
    __m256 zero;
    __m256 halfCell;
    __m256 speed;
    __m256 embeddedEpsilon;
    __m256 retargetEpsilon;
    __m256 paddedWidthFloat;
    __m256 inversePaddedWidth;
    __m256 absMask;
    __m256i paddedWidth;
    __m256i paddedOffset;
    __m256i paddedCellCount;
    __m256i retargetThreshold;
    __m256i oneInt;
    __m256i minusOneInt;
    __m256i sentinelBrick;
    __m256 packScaleX;
    __m256 packScaleY;
    __m256 renderMax;
    __m256 positionToX;
    __m256 positionToY;
    __m256 xToPosition;
    __m256 yToPosition;
    __m256 positionMax;
    __m256 inverseSpeed;
    __m256 directionScale;
    __m256i positionMask;
    __m256i directionMask;
    __m256i colorMask;
    __m256i bounceMask;
    __m256i savedBounceLimit;
    __m256i lastNearRow;
    __m256 tickTime;
    __m256 coastLeft;
    __m256 coastRight;
    __m256 coastTop;
    __m256 coastBottom;

    LaneConstants(const StepParams& params, int sentinel, int nearRowsEnd)
        : deltaTime(_mm256_set1_ps(params.deltaTime)), radius(_mm256_set1_ps(params.radius)),
          radiusSquared(_mm256_set1_ps(params.radius * params.radius)),
          twiceRadius(_mm256_set1_ps(2.0f * params.radius)),
          rightLimit(_mm256_set1_ps(params.fieldWidth - params.radius)),
          bottomLimit(_mm256_set1_ps(params.fieldHeight - params.radius)),
          twiceRightLimit(_mm256_set1_ps(2.0f * (params.fieldWidth - params.radius))),
          twiceBottomLimit(_mm256_set1_ps(2.0f * (params.fieldHeight - params.radius))),
          oneMinusRadius(_mm256_set1_ps(1.0f - params.radius)), one(_mm256_set1_ps(1.0f)),
          minusOne(_mm256_set1_ps(-1.0f)), zero(_mm256_setzero_ps()), halfCell(_mm256_set1_ps(0.5f)),
          speed(_mm256_set1_ps(params.speed)), embeddedEpsilon(_mm256_set1_ps(1e-12f)),
          retargetEpsilon(_mm256_set1_ps(1e-8f)),
          paddedWidthFloat(_mm256_set1_ps(static_cast<float>(params.paddedWidth))),
          inversePaddedWidth(_mm256_set1_ps(1.0f / static_cast<float>(params.paddedWidth))),
          absMask(_mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff))),
          paddedWidth(_mm256_set1_epi32(params.paddedWidth)),
          paddedOffset(_mm256_set1_epi32(params.paddedWidth + 1)),
          paddedCellCount(_mm256_set1_epi32(params.paddedCellCount)),
          retargetThreshold(_mm256_set1_epi32(params.retargetBounceThreshold - 1)), oneInt(_mm256_set1_epi32(1)),
          minusOneInt(_mm256_set1_epi32(-1)), sentinelBrick(_mm256_set1_epi32(sentinel)),
          packScaleX(_mm256_set1_ps(params.packScaleX)), packScaleY(_mm256_set1_ps(params.packScaleY)),
          renderMax(_mm256_set1_ps(static_cast<float>(renderCoordinateMax))),
          positionToX(_mm256_set1_ps(params.fieldWidth / compact::positionScale)),
          positionToY(_mm256_set1_ps(params.fieldHeight / compact::positionScale)),
          xToPosition(_mm256_set1_ps(compact::positionScale / params.fieldWidth)),
          yToPosition(_mm256_set1_ps(compact::positionScale / params.fieldHeight)),
          positionMax(_mm256_set1_ps(compact::positionScale)), inverseSpeed(_mm256_set1_ps(1.0f / params.speed)),
          directionScale(_mm256_set1_ps(compact::directionScale)),
          positionMask(_mm256_set1_epi32(static_cast<int>(compact::positionMask))),
          directionMask(_mm256_set1_epi32(static_cast<int>(compact::directionMask))),
          colorMask(_mm256_set1_epi32(static_cast<int>(compact::colorMask))),
          bounceMask(_mm256_set1_epi32(static_cast<int>(compact::bounceMask))),
          savedBounceLimit(_mm256_set1_epi32(compact::savedBounceLimit)),
          lastNearRow(_mm256_set1_epi32(nearRowsEnd - paddingRowsAboveFirstRow - 1)),
          tickTime(_mm256_set1_ps(params.deltaTime * static_cast<float>(params.substeps))),
          coastLeft(_mm256_set1_ps(params.radius + coastMargin)),
          coastRight(_mm256_set1_ps(params.fieldWidth - params.radius - coastMargin)),
          coastTop(_mm256_set1_ps(std::max(params.radius, static_cast<float>(nearRowsEnd - paddingRowsAboveFirstRow)) +
                                  coastMargin)),
          coastBottom(_mm256_set1_ps(params.fieldHeight - params.radius - coastMargin)) {}
};

inline __m256 select(__m256 mask, __m256 whenTrue, __m256 whenFalse) {
    return _mm256_blendv_ps(whenFalse, whenTrue, mask);
}

inline __m256i selectInt(__m256 mask, __m256i whenTrue, __m256i whenFalse) {
    return _mm256_castps_si256(
        _mm256_blendv_ps(_mm256_castsi256_ps(whenFalse), _mm256_castsi256_ps(whenTrue), mask));
}

inline __m256i packForRender(__m256 x, __m256 y, __m256i color, const LaneConstants& k) {
    const __m256i packedX = _mm256_cvtps_epi32(_mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(x, k.packScaleX), k.zero), k.renderMax));
    const __m256i packedY = _mm256_cvtps_epi32(_mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(y, k.packScaleY), k.zero), k.renderMax));
    return _mm256_or_si256(_mm256_or_si256(packedX, _mm256_slli_epi32(packedY, renderCoordinateBits)),
                           _mm256_slli_epi32(color, renderColorShift));
}

struct GridTarget {
    uint32_t* slots;
    int width;
    __m256i maxColumn;
    __m256i maxRow;
    __m256 coordinateToColumn;
    __m256 coordinateToRow;
    __m256i coordinateMask;
    __m256i widthLanes;
    __m256i seed;
    __m256i bidderMask;
    __m256i laneOffsets;

    GridTarget(CompactBallStore& store)
        : slots(store.gridFilled ? store.grid.data() : nullptr), width(store.gridWidth),
          maxColumn(_mm256_set1_epi32(store.gridWidth - 1)), maxRow(_mm256_set1_epi32(store.gridHeight - 1)),
          coordinateToColumn(_mm256_set1_ps(static_cast<float>(store.gridWidth) / (renderCoordinateMax + 1))),
          coordinateToRow(_mm256_set1_ps(static_cast<float>(store.gridHeight) / (renderCoordinateMax + 1))),
          coordinateMask(_mm256_set1_epi32(static_cast<int>(renderCoordinateMax))),
          widthLanes(_mm256_set1_epi32(store.gridWidth)), seed(_mm256_set1_epi32(static_cast<int>(store.frameSeed))),
          bidderMask(_mm256_set1_epi32(static_cast<int>(store.bidderMask))),
          laneOffsets(_mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7)) {}

    unsigned bidders(size_t first) const {
        __m256i hash = _mm256_xor_si256(_mm256_add_epi32(_mm256_set1_epi32(static_cast<int>(first)), laneOffsets), seed);
        hash = _mm256_xor_si256(hash, _mm256_srli_epi32(hash, 16));
        hash = _mm256_mullo_epi32(hash, _mm256_set1_epi32(0x7feb352d));
        hash = _mm256_xor_si256(hash, _mm256_srli_epi32(hash, 15));
        hash = _mm256_mullo_epi32(hash, _mm256_set1_epi32(static_cast<int>(0x846ca68bu)));
        hash = _mm256_xor_si256(hash, _mm256_srli_epi32(hash, 16));
        const __m256i rejected = _mm256_and_si256(hash, bidderMask);
        return static_cast<unsigned>(
            _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(rejected, _mm256_setzero_si256()))));
    }

    void place(__m256i packed, unsigned lanes) const {
        const __m256i column = _mm256_min_epi32(
            _mm256_cvttps_epi32(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(packed, coordinateMask)), coordinateToColumn)),
            maxColumn);
        const __m256i row = _mm256_min_epi32(
            _mm256_cvttps_epi32(_mm256_mul_ps(
                _mm256_cvtepi32_ps(_mm256_and_si256(_mm256_srli_epi32(packed, renderCoordinateBits), coordinateMask)),
                coordinateToRow)),
            maxRow);
        alignas(32) int32_t slotIndex[simdLaneCount];
        alignas(32) uint32_t values[simdLaneCount];
        _mm256_store_si256(reinterpret_cast<__m256i*>(slotIndex), _mm256_add_epi32(_mm256_mullo_epi32(row, widthLanes), column));
        _mm256_store_si256(reinterpret_cast<__m256i*>(values), packed);
        while (lanes != 0) {
            const int lane = std::countr_zero(lanes);
            lanes &= lanes - 1;
            std::atomic_ref<uint32_t>(slots[slotIndex[lane]]).store(values[lane], std::memory_order_relaxed);
        }
    }
};

inline __m256 lessThan(__m256 a, __m256 b) { return _mm256_cmp_ps(a, b, _CMP_LT_OQ); }
inline __m256 greaterThan(__m256 a, __m256 b) { return _mm256_cmp_ps(a, b, _CMP_GT_OQ); }

struct ContactSearch {
    __m256 distanceSquared;
    __m256 offsetX;
    __m256 offsetY;
    __m256i column;
    __m256i row;
    __m256i brick;
};

inline void considerCell(ContactSearch& best, const int32_t* paddedCells, __m256 x, __m256 y, __m256i column,
                         __m256i row, const LaneConstants& k) {
    const __m256i index = _mm256_add_epi32(_mm256_mullo_epi32(row, k.paddedWidth), _mm256_add_epi32(column, k.paddedOffset));
    const __m256i brick = _mm256_i32gather_epi32(paddedCells, index, 4);
    const __m256 occupied = _mm256_castsi256_ps(_mm256_cmpgt_epi32(brick, k.minusOneInt));
    const __m256 cellLeft = _mm256_cvtepi32_ps(column);
    const __m256 cellTop = _mm256_cvtepi32_ps(row);
    const __m256 closestX = _mm256_min_ps(_mm256_max_ps(x, cellLeft), _mm256_add_ps(cellLeft, k.one));
    const __m256 closestY = _mm256_min_ps(_mm256_max_ps(y, cellTop), _mm256_add_ps(cellTop, k.one));
    const __m256 offsetX = _mm256_sub_ps(x, closestX);
    const __m256 offsetY = _mm256_sub_ps(y, closestY);
    const __m256 distanceSquared = _mm256_fmadd_ps(offsetX, offsetX, _mm256_mul_ps(offsetY, offsetY));
    const __m256 closer = _mm256_and_ps(occupied, lessThan(distanceSquared, best.distanceSquared));
    best.distanceSquared = select(closer, distanceSquared, best.distanceSquared);
    best.offsetX = select(closer, offsetX, best.offsetX);
    best.offsetY = select(closer, offsetY, best.offsetY);
    best.column = selectInt(closer, column, best.column);
    best.row = selectInt(closer, row, best.row);
    best.brick = selectInt(closer, brick, best.brick);
}

}

Avx2Backend::Avx2Backend(size_t threadCount) : pool(std::make_unique<WorkerPool>(threadCount)) {
}

void Avx2Backend::setVisibilityGrid(int width, int height) {
    store.gridWidth = std::max(width, 0);
    store.gridHeight = std::max(height, 0);
}

void Avx2Backend::shareCoresWithGpu() {
    const size_t threads = pool->threadCount();
    pool = std::make_unique<WorkerPool>(threads > 1 ? threads - 1 : 1, WorkerPriority::Background);
}

bool Avx2Backend::isSupportedByCpu() {
#if defined(_MSC_VER)
    int registers[4]{};
    __cpuidex(registers, 7, 0);
    const bool hasAvx2 = (registers[1] & (1 << 5)) != 0;
    __cpuid(registers, 1);
    const bool hasFma = (registers[2] & (1 << 12)) != 0;
    const bool hasOsAvx = (registers[2] & (1 << 27)) != 0 && (_xgetbv(0) & 0x6) == 0x6;
    return hasAvx2 && hasFma && hasOsAvx;
#else
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#endif
}

void Avx2Backend::materializeFreeFlight() {
    if (!store.frozen) {
        return;
    }
    pool->runChunks(store.count, ballsPerChunk,
                    [&](size_t, size_t begin, size_t end) { store.materializeRange(begin, end); });
    store.frozen = false;
}

// What is physically free right now, less a margin for the rest of the game and the system, at 8 bytes per ball.
// Paging even part of the balls out would cost more than the frame budget, so the ceiling stops short of it, and
// once Windows reports memory as low no more balls are taken at all.
size_t Avx2Backend::ballCapacityLimit() const {
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

size_t Avx2Backend::preallocateBalls() {
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

void Avx2Backend::step(const StepParams& params, std::vector<BallHit>& hits) {
    store.clockTick = params.tick + static_cast<unsigned int>(params.substeps);
    store.flightParams = params;
    if (store.count == 0) {
        return;
    }
    store.speed = params.speed;
    // Free flight only pays off when the visibility grid limits a frame to the sampled balls: placing every ball
    // from the closed form costs more than the SIMD step, so without a grid the balls keep being stepped.
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
    __m256 x;
    __m256 y;
    __m256 vx;
    __m256 vy;
    __m256i color;
    __m256i bounces;
};

struct LaneStepResult {
    __m256 hit;
    __m256 embedded;
    __m256i brick;
    __m256i column;
    __m256i row;
};

LaneStepResult stepLanes(LaneState& state, const LaneConstants& k, const BoardView& board) {
    __m256 x = _mm256_fmadd_ps(state.vx, k.deltaTime, state.x);
    __m256 y = _mm256_fmadd_ps(state.vy, k.deltaTime, state.y);
    __m256 vx = state.vx;
    __m256 vy = state.vy;

    const __m256 hitLeft = lessThan(x, k.radius);
    const __m256 hitRight = _mm256_andnot_ps(hitLeft, greaterThan(x, k.rightLimit));
    const __m256 hitTop = lessThan(y, k.radius);
    const __m256 hitBottom = _mm256_andnot_ps(hitTop, greaterThan(y, k.bottomLimit));
    const __m256 absVx = _mm256_and_ps(vx, k.absMask);
    const __m256 absVy = _mm256_and_ps(vy, k.absMask);
    x = select(hitLeft, _mm256_sub_ps(k.twiceRadius, x), select(hitRight, _mm256_sub_ps(k.twiceRightLimit, x), x));
    y = select(hitTop, _mm256_sub_ps(k.twiceRadius, y), select(hitBottom, _mm256_sub_ps(k.twiceBottomLimit, y), y));
    vx = select(hitLeft, absVx, select(hitRight, _mm256_sub_ps(k.zero, absVx), vx));
    vy = select(hitTop, absVy, select(hitBottom, _mm256_sub_ps(k.zero, absVy), vy));
    __m256 normalX = select(hitLeft, k.one, select(hitRight, k.minusOne, k.zero));
    __m256 normalY = select(hitTop, k.one, select(hitBottom, k.minusOne, k.zero));
    __m256 bounced = _mm256_or_ps(_mm256_or_ps(hitLeft, hitRight), _mm256_or_ps(hitTop, hitBottom));

    const __m256 floorX = _mm256_floor_ps(x);
    const __m256 floorY = _mm256_floor_ps(y);
    const __m256i column = _mm256_cvttps_epi32(floorX);
    const __m256i row = _mm256_cvttps_epi32(floorY);
    const __m256 fractionX = _mm256_sub_ps(x, floorX);
    const __m256 fractionY = _mm256_sub_ps(y, floorY);
    const __m256i neighborColumn = _mm256_sub_epi32(_mm256_castps_si256(lessThan(fractionX, k.radius)),
                                                    _mm256_castps_si256(greaterThan(fractionX, k.oneMinusRadius)));
    const __m256i neighborRow = _mm256_sub_epi32(_mm256_castps_si256(lessThan(fractionY, k.radius)),
                                                 _mm256_castps_si256(greaterThan(fractionY, k.oneMinusRadius)));
    const __m256i otherColumn = _mm256_add_epi32(column, neighborColumn);
    const __m256i otherRow = _mm256_add_epi32(row, neighborRow);

    ContactSearch best{k.radiusSquared, k.zero, k.zero, column, row, k.minusOneInt};
    __m256 sameColor = k.zero;
    __m256 contact = k.zero;
    __m256 embedded = k.zero;
    const __m256i maybeNear = _mm256_xor_si256(_mm256_cmpgt_epi32(row, k.lastNearRow), k.minusOneInt);
    const __m256i cellIndex = _mm256_add_epi32(_mm256_mullo_epi32(row, k.paddedWidth), _mm256_add_epi32(column, k.paddedOffset));
    const __m256i nearBrick = _mm256_testz_si256(maybeNear, maybeNear)
                                  ? _mm256_setzero_si256()
                                  : _mm256_i32gather_epi32(board.nearBrickCells, cellIndex, 4);
    if (!_mm256_testz_si256(nearBrick, nearBrick)) {
        considerCell(best, board.paddedCells, x, y, column, row, k);
        considerCell(best, board.paddedCells, x, y, otherColumn, row, k);
        considerCell(best, board.paddedCells, x, y, column, otherRow, k);
        considerCell(best, board.paddedCells, x, y, otherColumn, otherRow, k);

        contact = _mm256_castsi256_ps(_mm256_cmpgt_epi32(best.brick, k.minusOneInt));
        const __m256i gatherIndex = selectInt(contact, best.brick, k.sentinelBrick);
        const __m256i brickColor = _mm256_i32gather_epi32(board.brickColors, gatherIndex, 4);
        sameColor = _mm256_castsi256_ps(_mm256_cmpeq_epi32(brickColor, state.color));
        embedded = _mm256_andnot_ps(
            sameColor, _mm256_and_ps(contact, _mm256_cmp_ps(best.distanceSquared, k.embeddedEpsilon, _CMP_LE_OQ)));
        const __m256 reflect = _mm256_andnot_ps(embedded, _mm256_andnot_ps(sameColor, contact));

        const __m256 distance = _mm256_sqrt_ps(_mm256_max_ps(best.distanceSquared, k.embeddedEpsilon));
        const __m256 inverseDistance = _mm256_div_ps(k.one, distance);
        const __m256 contactNormalX = _mm256_mul_ps(best.offsetX, inverseDistance);
        const __m256 contactNormalY = _mm256_mul_ps(best.offsetY, inverseDistance);
        const __m256 push = _mm256_sub_ps(k.radius, distance);
        x = select(reflect, _mm256_fmadd_ps(contactNormalX, push, x), x);
        y = select(reflect, _mm256_fmadd_ps(contactNormalY, push, y), y);
        normalX = select(reflect, contactNormalX, normalX);
        normalY = select(reflect, contactNormalY, normalY);
        const __m256 alongNormal = _mm256_fmadd_ps(vx, normalX, _mm256_mul_ps(vy, normalY));
        const __m256 approaching = _mm256_and_ps(reflect, lessThan(alongNormal, k.zero));
        const __m256 twiceAlong = _mm256_add_ps(alongNormal, alongNormal);
        vx = select(approaching, _mm256_fnmadd_ps(twiceAlong, normalX, vx), vx);
        vy = select(approaching, _mm256_fnmadd_ps(twiceAlong, normalY, vy), vy);
        bounced = _mm256_andnot_ps(_mm256_or_ps(sameColor, embedded), _mm256_or_ps(bounced, reflect));
    }

    const __m256i bounces = _mm256_sub_epi32(state.bounces, _mm256_castps_si256(bounced));
    const __m256 retarget =
        _mm256_and_ps(bounced, _mm256_castsi256_ps(_mm256_cmpgt_epi32(bounces, k.retargetThreshold)));
    if (!_mm256_testz_ps(retarget, retarget)) {
        const __m256i cellColumn = _mm256_cvttps_epi32(_mm256_floor_ps(x));
        const __m256i cellRow = _mm256_cvttps_epi32(_mm256_floor_ps(y));
        const __m256i targetCellIndex = _mm256_add_epi32(_mm256_mullo_epi32(cellRow, k.paddedWidth),
                                                   _mm256_add_epi32(cellColumn, k.paddedOffset));
        const __m256i fieldIndex = _mm256_add_epi32(_mm256_mullo_epi32(state.color, k.paddedCellCount), targetCellIndex);
        const __m256i target = _mm256_mask_i32gather_epi32(k.minusOneInt, board.targetCells, fieldIndex,
                                                           _mm256_castps_si256(retarget), 4);
        const __m256 targetFloat = _mm256_cvtepi32_ps(target);
        const __m256 targetRow =
            _mm256_floor_ps(_mm256_mul_ps(_mm256_add_ps(targetFloat, k.halfCell), k.inversePaddedWidth));
        const __m256 targetColumn = _mm256_fnmadd_ps(targetRow, k.paddedWidthFloat, targetFloat);
        const __m256 toTargetX = _mm256_sub_ps(_mm256_sub_ps(targetColumn, k.halfCell), x);
        const __m256 toTargetY = _mm256_sub_ps(_mm256_sub_ps(targetRow, k.halfCell), y);
        const __m256 lengthSquared = _mm256_fmadd_ps(toTargetX, toTargetX, _mm256_mul_ps(toTargetY, toTargetY));
        const __m256 inverseLength =
            _mm256_div_ps(k.one, _mm256_sqrt_ps(_mm256_max_ps(lengthSquared, k.retargetEpsilon)));
        const __m256 directionX = _mm256_mul_ps(toTargetX, inverseLength);
        const __m256 directionY = _mm256_mul_ps(toTargetY, inverseLength);
        const __m256 facing = _mm256_cmp_ps(
            _mm256_fmadd_ps(directionX, normalX, _mm256_mul_ps(directionY, normalY)), k.zero, _CMP_GE_OQ);
        const __m256 hasTarget = _mm256_castsi256_ps(_mm256_cmpgt_epi32(target, k.minusOneInt));
        __m256 apply = _mm256_and_ps(_mm256_and_ps(retarget, hasTarget),
                                     _mm256_and_ps(facing, greaterThan(lengthSquared, k.retargetEpsilon)));
        // Few lanes retarget in a step, so the line-of-sight walk runs per lane.
        if (unsigned lanes = static_cast<unsigned>(_mm256_movemask_ps(apply)); lanes != 0) {
            alignas(32) float laneX[simdLaneCount];
            alignas(32) float laneY[simdLaneCount];
            alignas(32) int32_t laneTarget[simdLaneCount];
            alignas(32) int32_t inSight[simdLaneCount] = {};
            _mm256_store_ps(laneX, x);
            _mm256_store_ps(laneY, y);
            _mm256_store_si256(reinterpret_cast<__m256i*>(laneTarget), target);
            const int paddedWidth = _mm256_cvtsi256_si32(k.paddedWidth);
            while (lanes != 0) {
                const int lane = std::countr_zero(lanes);
                lanes &= lanes - 1;
                inSight[lane] = targetInSight(laneX[lane], laneY[lane], laneTarget[lane] % paddedWidth - 1,
                                              laneTarget[lane] / paddedWidth - 1, board, paddedWidth)
                                    ? -1
                                    : 0;
            }
            apply = _mm256_and_ps(apply, _mm256_castsi256_ps(_mm256_load_si256(reinterpret_cast<const __m256i*>(inSight))));
        }
        vx = select(apply, _mm256_mul_ps(directionX, k.speed), vx);
        vy = select(apply, _mm256_mul_ps(directionY, k.speed), vy);
    }

    state.x = x;
    state.y = y;
    state.vx = vx;
    state.vy = vy;
    state.bounces = bounces;
    return {_mm256_and_ps(sameColor, contact), embedded, best.brick, best.column, best.row};
}

bool coastsWholeTick(const LaneState& state, const LaneConstants& k) {
    const __m256 endX = _mm256_fmadd_ps(state.vx, k.tickTime, state.x);
    const __m256 endY = _mm256_fmadd_ps(state.vy, k.tickTime, state.y);
    const __m256 insideX = _mm256_and_ps(greaterThan(endX, k.coastLeft), lessThan(endX, k.coastRight));
    const __m256 insideY = _mm256_and_ps(greaterThan(_mm256_min_ps(endY, state.y), k.coastTop), lessThan(endY, k.coastBottom));
    return _mm256_movemask_ps(_mm256_and_ps(insideX, insideY)) == 0xff;
}

void coast(LaneState& state, const LaneConstants& k, int substeps) {
    for (int substep = 0; substep < substeps; ++substep) {
        state.x = _mm256_fmadd_ps(state.vx, k.deltaTime, state.x);
        state.y = _mm256_fmadd_ps(state.vy, k.deltaTime, state.y);
    }
}

LaneState decodeLanes(__m256i position, __m256i state, const LaneConstants& k) {
    const __m256 x = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(position, k.positionMask)), k.positionToX);
    const __m256 y = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srli_epi32(position, 16)), k.positionToY);
    constexpr int signShift = 32 - compact::directionBits;
    const __m256 dx = _mm256_cvtepi32_ps(_mm256_srai_epi32(_mm256_slli_epi32(state, signShift), signShift));
    const __m256 dy = _mm256_cvtepi32_ps(
        _mm256_srai_epi32(_mm256_slli_epi32(state, signShift - compact::directionBits), signShift));
    const __m256 lengthSquared = _mm256_max_ps(_mm256_fmadd_ps(dx, dx, _mm256_mul_ps(dy, dy)), k.one);
    const __m256 scale = _mm256_div_ps(k.speed, _mm256_sqrt_ps(lengthSquared));
    return {x,
            y,
            _mm256_mul_ps(dx, scale),
            _mm256_mul_ps(dy, scale),
            _mm256_and_si256(_mm256_srli_epi32(state, compact::colorShift), k.colorMask),
            _mm256_and_si256(_mm256_srli_epi32(state, compact::bounceShift), k.bounceMask)};
}

__m256i roundingDitherLanes(__m256i previousPosition) {
    __m256i hash = _mm256_mullo_epi32(previousPosition, _mm256_set1_epi32(static_cast<int>(0x9E3779B1u)));
    hash = _mm256_xor_si256(hash, _mm256_srli_epi32(hash, 15));
    hash = _mm256_mullo_epi32(hash, _mm256_set1_epi32(static_cast<int>(0x85EBCA77u)));
    hash = _mm256_xor_si256(hash, _mm256_srli_epi32(hash, 13));
    hash = _mm256_mullo_epi32(hash, _mm256_set1_epi32(static_cast<int>(0xC2B2AE3Du)));
    return _mm256_xor_si256(hash, _mm256_srli_epi32(hash, 16));
}

__m256i encodeAxisDitheredLanes(__m256 value, __m256 toPosition, __m256i dither16, const LaneConstants& k) {
    const __m256 fraction = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(dither16, _mm256_set1_epi32(0xFFFF))),
                                          _mm256_set1_ps(1.0f / 65536.0f));
    const __m256 scaled = _mm256_fmadd_ps(value, toPosition, fraction);
    return _mm256_cvttps_epi32(_mm256_min_ps(_mm256_max_ps(scaled, k.zero), k.positionMax));
}

// A stepped ball's position: rounded without bias (see roundingDither in BallStepCore.h).
__m256i encodeSteppedPositionLanes(const LaneState& lanes, __m256i previousPosition, const LaneConstants& k) {
    const __m256i dither = roundingDitherLanes(previousPosition);
    return _mm256_or_si256(encodeAxisDitheredLanes(lanes.x, k.xToPosition, dither, k),
                           _mm256_slli_epi32(encodeAxisDitheredLanes(lanes.y, k.yToPosition,
                                                                     _mm256_srli_epi32(dither, 16), k),
                                             16));
}

__m256i encodeComponentLanes(__m256 velocity, const LaneConstants& k) {
    const __m256 unit = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(velocity, k.inverseSpeed), k.minusOne), k.one);
    return _mm256_and_si256(_mm256_cvtps_epi32(_mm256_mul_ps(unit, k.directionScale)), k.directionMask);
}

__m256i encodeStateLanes(const LaneState& lanes, const LaneConstants& k) {
    const __m256i savedBounces = _mm256_min_epi32(lanes.bounces, k.savedBounceLimit);
    return _mm256_or_si256(
        _mm256_or_si256(encodeComponentLanes(lanes.vx, k),
                        _mm256_slli_epi32(encodeComponentLanes(lanes.vy, k), compact::directionBits)),
        _mm256_or_si256(_mm256_slli_epi32(lanes.color, compact::colorShift),
                        _mm256_slli_epi32(savedBounces, compact::bounceShift)));
}

LaneState blendState(__m256 mask, const LaneState& whenTrue, const LaneState& whenFalse) {
    return {select(mask, whenTrue.x, whenFalse.x),   select(mask, whenTrue.y, whenFalse.y),
            select(mask, whenTrue.vx, whenFalse.vx), select(mask, whenTrue.vy, whenFalse.vy),
            whenTrue.color,                          selectInt(mask, whenTrue.bounces, whenFalse.bounces)};
}

void resolveEmbeddedLanes(LaneState& state, unsigned lanes, const LaneStepResult& result, const BoardView& board,
                          const StepParams& params) {
    alignas(32) float x[simdLaneCount];
    alignas(32) float y[simdLaneCount];
    alignas(32) float vx[simdLaneCount];
    alignas(32) float vy[simdLaneCount];
    alignas(32) int32_t color[simdLaneCount];
    alignas(32) int32_t bounces[simdLaneCount];
    alignas(32) int32_t columns[simdLaneCount];
    alignas(32) int32_t rows[simdLaneCount];
    _mm256_store_ps(x, state.x);
    _mm256_store_ps(y, state.y);
    _mm256_store_ps(vx, state.vx);
    _mm256_store_ps(vy, state.vy);
    _mm256_store_si256(reinterpret_cast<__m256i*>(color), state.color);
    _mm256_store_si256(reinterpret_cast<__m256i*>(bounces), state.bounces);
    _mm256_store_si256(reinterpret_cast<__m256i*>(columns), result.column);
    _mm256_store_si256(reinterpret_cast<__m256i*>(rows), result.row);
    while (lanes != 0) {
        const int lane = std::countr_zero(lanes);
        lanes &= lanes - 1;
        resolveEmbeddedContact(x[lane], y[lane], vx[lane], vy[lane], color[lane], bounces[lane], columns[lane],
                               rows[lane], board, params);
    }
    state.x = _mm256_load_ps(x);
    state.y = _mm256_load_ps(y);
    state.vx = _mm256_load_ps(vx);
    state.vy = _mm256_load_ps(vy);
    state.bounces = _mm256_load_si256(reinterpret_cast<const __m256i*>(bounces));
}

}

void Avx2Backend::stepRange(const StepParams& params, size_t begin, size_t end, std::vector<BallHit>& hits) {
    const BoardView board = boardView();
    const LaneConstants k(params, cells.sentinelBrick(), cells.nearRowsEnd);
    uint32_t* const positions = store.position.data();
    uint32_t* const states = store.state.data();
    uint32_t* const packedPositions = store.packed.data();
    const size_t validCount = store.count;
    const GridTarget grid(store);
    const __m256 allLanes = _mm256_castsi256_ps(k.minusOneInt);

    for (size_t i = begin; i < end; i += simdLaneCount) {
        const __m256i startState = _mm256_load_si256(reinterpret_cast<const __m256i*>(states + i));
        const __m256i startPosition = _mm256_load_si256(reinterpret_cast<const __m256i*>(positions + i));
        LaneState state = decodeLanes(startPosition, startState, k);
        const __m256i startBounces = state.bounces;
        __m256 active = allLanes;
        __m256i hitBrick = k.minusOneInt;
        const bool coasting = coastsWholeTick(state, k);
        if (coasting) {
            coast(state, k, params.substeps);
        }
        for (int substep = 0; substep < (coasting ? 0 : params.substeps); ++substep) {
            const LaneState before = state;
            const LaneStepResult result = stepLanes(state, k, board);
            state = blendState(active, state, before);
            const __m256 newHits = _mm256_and_ps(result.hit, active);
            hitBrick = selectInt(newHits, result.brick, hitBrick);
            const unsigned embeddedLanes =
                static_cast<unsigned>(_mm256_movemask_ps(_mm256_and_ps(result.embedded, active)));
            if (embeddedLanes != 0) {
                resolveEmbeddedLanes(state, embeddedLanes, result, board, params);
            }
            active = _mm256_andnot_ps(newHits, active);
            if (_mm256_testz_ps(active, active)) {
                break;
            }
        }

        _mm256_store_si256(reinterpret_cast<__m256i*>(positions + i), encodeSteppedPositionLanes(state, startPosition, k));
        const __m256i stateChanged = _mm256_xor_si256(_mm256_cmpeq_epi32(state.bounces, startBounces), k.minusOneInt);
        if (!_mm256_testz_si256(stateChanged, stateChanged)) {
            _mm256_maskstore_epi32(reinterpret_cast<int*>(states + i), stateChanged, encodeStateLanes(state, k));
        }
        const unsigned laneLimit = i + simdLaneCount <= validCount ? 0xffu : ((1u << (validCount - i)) - 1u);
        const __m256i packed = packForRender(state.x, state.y, state.color, k);
        if (grid.slots) {
            const unsigned bidding = grid.bidders(i) & laneLimit;
            if (bidding != 0) {
                grid.place(packed, bidding);
            }
        } else {
            _mm256_stream_si256(reinterpret_cast<__m256i*>(packedPositions + i), packed);
        }

        unsigned hitLanes = static_cast<unsigned>(_mm256_movemask_ps(
                                _mm256_castsi256_ps(_mm256_cmpgt_epi32(hitBrick, k.minusOneInt)))) &
                            laneLimit;
        if (hitLanes != 0) {
            alignas(32) int32_t bricks[simdLaneCount];
            _mm256_store_si256(reinterpret_cast<__m256i*>(bricks), hitBrick);
            while (hitLanes != 0) {
                const int lane = std::countr_zero(hitLanes);
                hitLanes &= hitLanes - 1;
                hits.push_back({static_cast<uint32_t>(i + static_cast<size_t>(lane)), bricks[lane]});
            }
        }
    }
    _mm_sfence();
}
