#include "sim/backends/Avx2Backend.h"

#include <immintrin.h>

#include <bit>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace {

constexpr size_t minimumBallsPerChunk = 2048;

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

    LaneConstants(const StepParams& params, int sentinel)
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
          renderMax(_mm256_set1_ps(static_cast<float>(renderCoordinateMax))) {}
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
    chunkHits.resize(pool->chunkCount());
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

void Avx2Backend::step(const StepParams& params, std::vector<BallHit>& hits) {
    if (store.count == 0) {
        return;
    }
    if (store.count < minimumBallsPerChunk * 2) {
        chunkHits[0].clear();
        stepRange(params, 0, store.count, chunkHits[0]);
        hits.insert(hits.end(), chunkHits[0].begin(), chunkHits[0].end());
        return;
    }
    pool->runChunks(store.count, simdLaneCount, [&](size_t chunk, size_t begin, size_t end) {
        chunkHits[chunk].clear();
        stepRange(params, begin, end, chunkHits[chunk]);
    });
    for (const auto& chunk : chunkHits) {
        hits.insert(hits.end(), chunk.begin(), chunk.end());
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
    considerCell(best, board.paddedCells, x, y, column, row, k);
    considerCell(best, board.paddedCells, x, y, otherColumn, row, k);
    considerCell(best, board.paddedCells, x, y, column, otherRow, k);
    considerCell(best, board.paddedCells, x, y, otherColumn, otherRow, k);

    const __m256 contact = _mm256_castsi256_ps(_mm256_cmpgt_epi32(best.brick, k.minusOneInt));
    const __m256i gatherIndex = selectInt(contact, best.brick, k.sentinelBrick);
    const __m256i brickColor = _mm256_i32gather_epi32(board.brickColors, gatherIndex, 4);
    const __m256 sameColor = _mm256_castsi256_ps(_mm256_cmpeq_epi32(brickColor, state.color));
    const __m256 embedded = _mm256_andnot_ps(
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

    const __m256i bounces = _mm256_sub_epi32(state.bounces, _mm256_castps_si256(bounced));
    const __m256 retarget =
        _mm256_and_ps(bounced, _mm256_castsi256_ps(_mm256_cmpgt_epi32(bounces, k.retargetThreshold)));
    if (!_mm256_testz_ps(retarget, retarget)) {
        const __m256i cellColumn = _mm256_cvttps_epi32(_mm256_floor_ps(x));
        const __m256i cellRow = _mm256_cvttps_epi32(_mm256_floor_ps(y));
        const __m256i cellIndex = _mm256_add_epi32(_mm256_mullo_epi32(cellRow, k.paddedWidth),
                                                   _mm256_add_epi32(cellColumn, k.paddedOffset));
        const __m256i fieldIndex = _mm256_add_epi32(_mm256_mullo_epi32(state.color, k.paddedCellCount), cellIndex);
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
        const __m256 apply = _mm256_and_ps(_mm256_and_ps(retarget, hasTarget),
                                           _mm256_and_ps(facing, greaterThan(lengthSquared, k.retargetEpsilon)));
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
    const int sentinel = static_cast<int>(brickColors.size()) - 1;
    const LaneConstants k(params, sentinel);
    float* const xs = store.x.data();
    float* const ys = store.y.data();
    float* const vxs = store.vx.data();
    float* const vys = store.vy.data();
    const int32_t* const colors = store.color.data();
    int32_t* const bounceCounts = store.bounces.data();
    uint32_t* const packedPositions = store.packed.data();
    const size_t validCount = store.count;
    const __m256 allLanes = _mm256_castsi256_ps(k.minusOneInt);

    for (size_t i = begin; i < end; i += simdLaneCount) {
        LaneState state{_mm256_load_ps(xs + i),
                        _mm256_load_ps(ys + i),
                        _mm256_load_ps(vxs + i),
                        _mm256_load_ps(vys + i),
                        _mm256_load_si256(reinterpret_cast<const __m256i*>(colors + i)),
                        _mm256_load_si256(reinterpret_cast<const __m256i*>(bounceCounts + i))};
        __m256 active = allLanes;
        __m256i hitBrick = k.minusOneInt;
        for (int substep = 0; substep < params.substeps; ++substep) {
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

        _mm256_store_ps(xs + i, state.x);
        _mm256_store_ps(ys + i, state.y);
        _mm256_store_ps(vxs + i, state.vx);
        _mm256_store_ps(vys + i, state.vy);
        _mm256_store_si256(reinterpret_cast<__m256i*>(bounceCounts + i), state.bounces);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(packedPositions + i),
                            packForRender(state.x, state.y, state.color, k));

        const unsigned laneLimit = i + simdLaneCount <= validCount ? 0xffu : ((1u << (validCount - i)) - 1u);
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
