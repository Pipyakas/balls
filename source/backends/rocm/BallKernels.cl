#pragma OPENCL EXTENSION cl_khr_int64_base_atomics : enable
#pragma OPENCL EXTENSION cl_khr_int64_extended_atomics : enable

typedef struct {
    float deltaTime;
    float radius;
    float speed;
    float fieldWidth;
    float fieldHeight;
    int paddedWidth;
    int paddedCellCount;
    int retargetBounceThreshold;
    float packScaleX;
    float packScaleY;
    int substeps;
    uint tick;
    int freeFlight;
} StepParams;

typedef struct {
    __global uint* position;
    __global uint* state;
    __global uint* packed;
} DeviceBalls;

typedef struct {
    __global ulong* slots;
    int width;
    int height;
    float scaleX;
    float scaleY;
    uint frameSeed;
    uint bidderMask;
} VisibilityGrid;

typedef struct {
    uint killedCount;
    uint holeCount;
    uint holeCursor;
    uint killsByColor[7];
} FrameCounters;

typedef struct {
    __global uint* cellCursors;
    __global uint* position;
    __global uint* state;
    int columns;
    int rows;
} SortedOutput;

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    uint wallCrossings;
} FreeFlightPosition;

#define noBrickHit -1
#define renderCoordinateMax 16383u
#define renderCoordinateBits 14u
#define renderColorShift 28u
#define emptyRenderSlot 0xFFFFFFFFu
#define emptyFarCell -1
#define emptyNearCell -2
#define cellColorMask 7
#define cellBrickShift 3
#define positionScale 65535.0f
#define positionMask 0xFFFFu
#define directionBits 13
#define directionMask ((1u << directionBits) - 1u)
#define directionScale 4095.0f
#define colorShift 26u
#define colorMask 7u
#define bounceShift 29u
#define bounceMask 3u
#define deadColor 7u
#define savedBounceLimit 3
#define maxSortCells 4096
#define sortBallsPerBlock 4096
#define sortItemsPerThread 16
#define rankBits 12u
#define rankMask ((1u << rankBits) - 1u)
#define stepSortThreads 256
#define stepSortItemsPerThread 16
#define stepSortBallsPerBlock (stepSortThreads * stepSortItemsPerThread)
#define stepThreads 256
#define ballsPerStepThread 4
#define freeFlightPhaseUnits 4294967296.0f
#define freeFlightPhaseHalf 0x80000000u

inline uint packRenderCoordinate(float value, float scale) {
    const float scaled = value * scale;
    const float clamped = scaled < 0.0f ? 0.0f : (scaled > (float)renderCoordinateMax ? (float)renderCoordinateMax : scaled);
    return (uint)(clamped + 0.5f);
}

inline uint packBallForRender(float x, float y, int color, float scaleX, float scaleY) {
    return packRenderCoordinate(x, scaleX) | (packRenderCoordinate(y, scaleY) << renderCoordinateBits) |
           ((uint)color << renderColorShift);
}

inline float clampFloat(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

inline int paddedCellIndex(int column, int row, int paddedWidth) {
    return (row + 1) * paddedWidth + column + 1;
}

inline void pushOutOfCell(float* x, float* y, float* normalX, float* normalY, int column, int row, float radius) {
    const float toLeft = *x - (float)column;
    const float toRight = (float)(column + 1) - *x;
    const float toTop = *y - (float)row;
    const float toBottom = (float)(row + 1) - *y;
    const float nearest = fmin(fmin(toLeft, toRight), fmin(toTop, toBottom));
    *normalX = 0.0f;
    *normalY = 0.0f;
    if (nearest == toBottom) {
        *normalY = 1.0f;
        *y = (float)(row + 1) + radius;
    } else if (nearest == toTop) {
        *normalY = -1.0f;
        *y = (float)row - radius;
    } else if (nearest == toLeft) {
        *normalX = -1.0f;
        *x = (float)column - radius;
    } else {
        *normalX = 1.0f;
        *x = (float)(column + 1) + radius;
    }
}

inline uint roundingDither(uint previousPosition) {
    uint hash = previousPosition * 0x9E3779B1u;
    hash ^= hash >> 15;
    hash *= 0x85EBCA77u;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE3Du;
    hash ^= hash >> 16;
    return hash;
}

inline float ditherFraction(uint dither16) {
    return (float)(dither16 & 0xFFFFu) * (1.0f / 65536.0f);
}

inline void reflectVelocity(float* vx, float* vy, float normalX, float normalY) {
    const float velocityAlongNormal = *vx * normalX + *vy * normalY;
    if (velocityAlongNormal < 0.0f) {
        *vx -= 2.0f * velocityAlongNormal * normalX;
        *vy -= 2.0f * velocityAlongNormal * normalY;
    }
}

inline int cellOccupant(__global const int* cellWords, int index, int* color) {
    const int word = cellWords[index];
    *color = word < 0 ? -1 : (word & cellColorMask);
    return word < 0 ? -1 : (word >> cellBrickShift);
}

inline bool targetInSight(float x, float y, int targetColumn, int targetRow,
                          __global const int* cellWords, int paddedWidth) {
    const float unreachable = 1e30f;
    const float toX = (float)targetColumn + 0.5f - x;
    const float toY = (float)targetRow + 0.5f - y;
    int column = (int)floor(x);
    int row = (int)floor(y);
    const int stepColumn = toX > 0.0f ? 1 : -1;
    const int stepRow = toY > 0.0f ? 1 : -1;
    const float deltaX = toX != 0.0f ? fabs(1.0f / toX) : unreachable;
    const float deltaY = toY != 0.0f ? fabs(1.0f / toY) : unreachable;
    float nextX = toX != 0.0f ? (stepColumn > 0 ? (float)(column + 1) - x : x - (float)column) * deltaX : unreachable;
    float nextY = toY != 0.0f ? (stepRow > 0 ? (float)(row + 1) - y : y - (float)row) * deltaY : unreachable;
    const int cellsToCross = (column > targetColumn ? column - targetColumn : targetColumn - column) +
                             (row > targetRow ? row - targetRow : targetRow - row);
    for (int crossed = 0; crossed < cellsToCross; ++crossed) {
        if (nextX < nextY) {
            nextX += deltaX;
            column += stepColumn;
        } else {
            nextY += deltaY;
            row += stepRow;
        }
        if (column == targetColumn && row == targetRow) return true;
        int color = -1;
        if (cellOccupant(cellWords, paddedCellIndex(column, row, paddedWidth), &color) >= 0) {
            return false;
        }
    }
    return column == targetColumn && row == targetRow;
}

inline void retargetTowardNearestBrick(float x, float y, float* vx, float* vy, int color,
                                       float normalX, float normalY,
                                       __global const int* cellWords,
                                       __global const int* targetCells,
                                       StepParams params) {
    const int column = (int)floor(x);
    const int row = (int)floor(y);
    const int target = targetCells[color * params.paddedCellCount + paddedCellIndex(column, row, params.paddedWidth)];
    if (target < 0) return;
    const float targetX = (float)(target % params.paddedWidth) - 0.5f;
    const float targetY = (float)(target / params.paddedWidth) - 0.5f;
    const float toTargetX = targetX - x;
    const float toTargetY = targetY - y;
    const float distance = sqrt(toTargetX * toTargetX + toTargetY * toTargetY);
    if (distance < 1e-4f) return;
    const float dirX = toTargetX / distance;
    const float dirY = toTargetY / distance;
    if (dirX * normalX + dirY * normalY < 0.0f) return;
    const int targetColumn = target % params.paddedWidth - 1;
    const int targetRow = target / params.paddedWidth - 1;
    if (!targetInSight(x, y, targetColumn, targetRow, cellWords, params.paddedWidth)) return;
    *vx = dirX * params.speed;
    *vy = dirY * params.speed;
}

inline int stepBall(float* x, float* y, float* vx, float* vy, int color, int* bounces,
                    __global const int* cellWords, __global const int* targetCells,
                    StepParams params) {
    const float radius = params.radius;
    *x += *vx * params.deltaTime;
    *y += *vy * params.deltaTime;

    float normalX = 0.0f;
    float normalY = 0.0f;
    bool bounced = false;
    if (*x < radius) {
        *x = 2.0f * radius - *x;
        *vx = fabs(*vx);
        normalX = 1.0f;
        bounced = true;
    } else if (*x > params.fieldWidth - radius) {
        *x = 2.0f * (params.fieldWidth - radius) - *x;
        *vx = -fabs(*vx);
        normalX = -1.0f;
        bounced = true;
    }
    if (*y < radius) {
        *y = 2.0f * radius - *y;
        *vy = fabs(*vy);
        normalY = 1.0f;
        bounced = true;
    } else if (*y > params.fieldHeight - radius) {
        *y = 2.0f * (params.fieldHeight - radius) - *y;
        *vy = -fabs(*vy);
        normalY = -1.0f;
        bounced = true;
    }

    const int column = (int)floor(*x);
    const int row = (int)floor(*y);
    if (cellWords[paddedCellIndex(column, row, params.paddedWidth)] == emptyFarCell) {
        if (bounced) {
            *bounces += 1;
            if (*bounces >= params.retargetBounceThreshold) {
                retargetTowardNearestBrick(*x, *y, vx, vy, color, normalX, normalY, cellWords, targetCells, params);
            }
        }
        return noBrickHit;
    }

    const float fractionX = *x - (float)column;
    const float fractionY = *y - (float)row;
    const int neighborColumn = fractionX < radius ? -1 : (fractionX > 1.0f - radius ? 1 : 0);
    const int neighborRow = fractionY < radius ? -1 : (fractionY > 1.0f - radius ? 1 : 0);

    float bestDistanceSquared = radius * radius;
    float bestOffsetX = 0.0f;
    float bestOffsetY = 0.0f;
    int bestColumn = 0;
    int bestRow = 0;
    int bestBrick = -1;
    int bestColor = -1;
    for (int candidate = 0; candidate < 4; ++candidate) {
        const int cellColumn = column + ((candidate & 1) ? neighborColumn : 0);
        const int cellRow = row + ((candidate & 2) ? neighborRow : 0);
        int cellColor = -1;
        const int brick = cellOccupant(cellWords, paddedCellIndex(cellColumn, cellRow, params.paddedWidth), &cellColor);
        if (brick < 0) continue;
        const float closestX = clampFloat(*x, (float)cellColumn, (float)(cellColumn + 1));
        const float closestY = clampFloat(*y, (float)cellRow, (float)(cellRow + 1));
        const float offsetX = *x - closestX;
        const float offsetY = *y - closestY;
        const float distanceSquared = offsetX * offsetX + offsetY * offsetY;
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestOffsetX = offsetX;
            bestOffsetY = offsetY;
            bestColumn = cellColumn;
            bestRow = cellRow;
            bestBrick = brick;
            bestColor = cellColor;
        }
    }

    if (bestBrick >= 0) {
        if (bestColor == color) return bestBrick;
        const float distance = sqrt(bestDistanceSquared);
        if (distance > 1e-6f) {
            normalX = bestOffsetX / distance;
            normalY = bestOffsetY / distance;
            *x += normalX * (radius - distance);
            *y += normalY * (radius - distance);
        } else {
            pushOutOfCell(x, y, &normalX, &normalY, bestColumn, bestRow, radius);
        }
        reflectVelocity(vx, vy, normalX, normalY);
        bounced = true;
    }

    if (bounced) {
        *bounces += 1;
        if (*bounces >= params.retargetBounceThreshold) {
            retargetTowardNearestBrick(*x, *y, vx, vy, color, normalX, normalY, cellWords, targetCells, params);
        }
    }
    return noBrickHit;
}

inline int stepBallSubsteps(float* x, float* y, float* vx, float* vy, int color, int* bounces,
                            __global const int* cellWords, __global const int* targetCells,
                            StepParams params) {
    for (int substep = 0; substep < params.substeps; ++substep) {
        const int brick = stepBall(x, y, vx, vy, color, bounces, cellWords, targetCells, params);
        if (brick != noBrickHit) return brick;
    }
    return noBrickHit;
}

inline uint stateColor(uint state) { return (state >> colorShift) & colorMask; }

inline uint encodeAxis(float value, float extent) {
    const float scaled = value / extent * positionScale + 0.5f;
    return (uint)fmin(fmax(scaled, 0.0f), positionScale);
}

inline uint encodePosition(float x, float y, StepParams params) {
    return encodeAxis(x, params.fieldWidth) | (encodeAxis(y, params.fieldHeight) << 16);
}

inline uint encodeAxisDithered(float value, float extent, float fraction) {
    const float scaled = value / extent * positionScale + fraction;
    return (uint)fmin(fmax(scaled, 0.0f), positionScale);
}

inline uint encodeSteppedPosition(float x, float y, uint previousPosition, StepParams params) {
    const uint dither = roundingDither(previousPosition);
    return encodeAxisDithered(x, params.fieldWidth, ditherFraction(dither)) |
           (encodeAxisDithered(y, params.fieldHeight, ditherFraction(dither >> 16)) << 16);
}

inline void decodePosition(uint position, float* x, float* y, StepParams params) {
    *x = (float)(position & positionMask) * (params.fieldWidth / positionScale);
    *y = (float)(position >> 16) * (params.fieldHeight / positionScale);
}

inline int signedComponent(uint bits) {
    return (int)(bits << (32 - directionBits)) >> (32 - directionBits);
}

inline uint encodeComponent(float value) {
    const int quantized = (int)round(fmin(fmax(value, -1.0f), 1.0f) * directionScale);
    return (uint)quantized & directionMask;
}

inline uint encodeState(float vx, float vy, uint color, int bounces, StepParams params) {
    const float inverseSpeed = 1.0f / params.speed;
    const uint savedBounces = (uint)min(bounces, savedBounceLimit);
    return encodeComponent(vx * inverseSpeed) | (encodeComponent(vy * inverseSpeed) << directionBits) |
           (color << colorShift) | (savedBounces << bounceShift);
}

inline void decodeVelocity(uint state, float* vx, float* vy, StepParams params) {
    const float dx = (float)signedComponent(state & directionMask);
    const float dy = (float)signedComponent((state >> directionBits) & directionMask);
    const float scale = params.speed * rsqrt(fmax(dx * dx + dy * dy, 1.0f));
    *vx = dx * scale;
    *vy = dy * scale;
}

inline uint packFromPosition(uint position, uint color) {
    return ((position & positionMask) >> 2) | (((position >> 16) >> 2) << renderCoordinateBits) |
           (color << renderColorShift);
}

inline uint slotPriority(uint index, uint frameSeed) {
    uint hash = index ^ frameSeed;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash | 1u;
}

inline int slotForPacked(uint packed, VisibilityGrid grid) {
    const int column = min((int)((float)(packed & renderCoordinateMax) * grid.scaleX), grid.width - 1);
    const int row = min((int)((float)((packed >> renderCoordinateBits) & renderCoordinateMax) * grid.scaleY), grid.height - 1);
    return row * grid.width + column;
}

inline void bidForSlot(uint packed, uint index, VisibilityGrid grid) {
    const uint priority = slotPriority(index, grid.frameSeed);
    if ((priority >> 1) & grid.bidderMask) return;
    const int slot = slotForPacked(packed, grid);
    if (priority > (uint)(grid.slots[slot] >> 32)) {
        atom_max((__global long*)&grid.slots[slot], (long)(((ulong)priority << 32) | packed));
    }
}

inline bool tryConsumeHit(__global int* hitsLeft) {
    int current = *hitsLeft;
    while (current > 0) {
        int previous = atomic_cmpxchg(hitsLeft, current, current - 1);
        if (previous == current) return true;
        current = previous;
    }
    return false;
}

inline void stepOneBall(uint target, uint startPosition, uint startState,
                        __global uint* outPosition, __global uint* outState, bool writeAllState,
                        __global uint* packedOut, __global const int* cellWords,
                        __global const int* targetCells, StepParams params,
                        __global int* brickHitsLeft, __global uint* killedIndices,
                        uint killedCapacity, __global FrameCounters* counters,
                        VisibilityGrid grid, uint packedCapacity) {
    const uint color = stateColor(startState);
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    decodePosition(startPosition, &x, &y, params);
    decodeVelocity(startState, &vx, &vy, params);
    const int startBounces = (int)((startState >> bounceShift) & bounceMask);
    int bounces = startBounces;
    const int brick = stepBallSubsteps(&x, &y, &vx, &vy, (int)color, &bounces, cellWords, targetCells, params);
    const uint position = encodeSteppedPosition(x, y, startPosition, params);
    outPosition[target] = position;
    if (bounces != startBounces) {
        outState[target] = encodeState(vx, vy, color, bounces, params);
    } else if (writeAllState) {
        outState[target] = startState;
    }
    const uint packed = packFromPosition(position, color);
    if (grid.slots) {
        bidForSlot(packed, target, grid);
    } else if (target < packedCapacity) {
        packedOut[target] = packed;
    }
    bool killed = (brick != noBrickHit && tryConsumeHit(&brickHitsLeft[brick]));
    uint kill_mask = __builtin_amdgcn_ballot_w32(killed);
    if (killed) {
        uint wave_rank = __builtin_amdgcn_mbcnt_lo(kill_mask, 0u);
        uint leader = __builtin_ctz(kill_mask);
        uint base_slot = 0;
        if ((get_local_id(0) & 31) == leader) {
            base_slot = atomic_add((volatile __global uint*)&counters->killedCount, (uint)__builtin_popcount(kill_mask));
        }
        base_slot = __builtin_amdgcn_readfirstlane(base_slot);
        uint slot = base_slot + wave_rank;
        if (slot < killedCapacity) {
            outState[target] = deadColor << colorShift;
            killedIndices[slot] = target;
            killedIndices[killedCapacity + slot] = (uint)brick;
            atomic_add((volatile __global uint*)&counters->killsByColor[color], 1u);
        } else {
            atomic_add((volatile __global int*)&brickHitsLeft[brick], 1);
        }
    }
}

__attribute__((reqd_work_group_size(stepThreads, 1, 1)))
__kernel void stepBallsKernel(DeviceBalls balls, int count,
                              __global const int* cellWords, __global const int* targetCells,
                              StepParams params, __global int* brickHitsLeft,
                              __global uint* killedIndices, uint killedCapacity,
                              __global FrameCounters* counters, VisibilityGrid grid,
                              uint packedCapacity) {
    const int blockStart = get_group_id(0) * ballsPerStepThread * stepThreads;
    uint positions[ballsPerStepThread];
    uint states[ballsPerStepThread];
    for (int item = 0; item < ballsPerStepThread; ++item) {
        const int index = blockStart + item * stepThreads + get_local_id(0);
        positions[item] = index < count ? balls.position[index] : 0u;
        states[item] = index < count ? balls.state[index] : 0u;
    }
    for (int item = 0; item < ballsPerStepThread; ++item) {
        const int index = blockStart + item * stepThreads + get_local_id(0);
        if (index < count) {
            stepOneBall((uint)index, positions[item], states[item], balls.position, balls.state,
                        false, balls.packed, cellWords, targetCells, params, brickHitsLeft,
                        killedIndices, killedCapacity, counters, grid, packedCapacity);
        }
    }
}

inline int sortCellOf(uint position, int columns, int rows) {
    const int column = min((int)(((position & positionMask) * (uint)columns) >> 16), columns - 1);
    const int row = min((int)(((position >> 16) * (uint)rows) >> 16), rows - 1);
    return row * columns + column;
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void countCellsKernel(__global const uint* position, int count, int columns, int rows,
                               __global uint* cellCounts) {
    __local uint sharedCells[maxSortCells];
    const int cellCount = columns * rows;
    for (int cell = get_local_id(0); cell < cellCount; cell += 256) {
        sharedCells[cell] = 0;
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    const int blockStart = get_group_id(0) * sortBallsPerBlock;
    for (int item = 0; item < sortItemsPerThread; ++item) {
        const int index = blockStart + item * 256 + get_local_id(0);
        if (index < count) {
            const int cell = sortCellOf(position[index], columns, rows);
            atomic_add(&sharedCells[cell], 1);
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for (int cell = get_local_id(0); cell < cellCount; cell += 256) {
        if (sharedCells[cell] != 0) {
            atomic_add(&cellCounts[cell], sharedCells[cell]);
        }
    }
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void scanCellsKernel(__global uint* cellCounts, int cellCount) {
    __local uint sharedCells[maxSortCells];
    for (int cell = get_local_id(0); cell < cellCount; cell += 256) {
        sharedCells[cell] = cellCounts[cell];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    if (get_local_id(0) == 0) {
        uint running = 0;
        for (int cell = 0; cell < cellCount; ++cell) {
            uint cellTotal = sharedCells[cell];
            sharedCells[cell] = running;
            running += cellTotal;
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for (int cell = get_local_id(0); cell < cellCount; cell += 256) {
        cellCounts[cell] = sharedCells[cell];
    }
}

__attribute__((reqd_work_group_size(stepSortThreads, 1, 1)))
__kernel void stepAndSortBallsKernel(DeviceBalls balls, int count,
                                     __global const int* cellWords, __global const int* targetCells,
                                     StepParams params, __global int* brickHitsLeft,
                                     __global uint* killedIndices, uint killedCapacity,
                                     __global FrameCounters* counters, VisibilityGrid grid,
                                     uint packedCapacity, SortedOutput sorted) {
    __local uint blockCells[maxSortCells];
    __local uint cellAndRank[stepSortBallsPerBlock];
    const int cellCount = sorted.columns * sorted.rows;
    for (int cell = get_local_id(0); cell < cellCount; cell += stepSortThreads) {
        blockCells[cell] = 0;
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    const int blockStart = get_group_id(0) * stepSortBallsPerBlock;
    for (int item = 0; item < stepSortItemsPerThread; ++item) {
        const int slot = item * stepSortThreads + get_local_id(0);
        const int index = blockStart + slot;
        const bool valid = (index < count);
        const int cell = valid ? sortCellOf(balls.position[index], sorted.columns, sorted.rows) : 0;
        const uint rank = valid ? atomic_add(&blockCells[cell], 1) : 0;
        cellAndRank[slot] = ((uint)cell << rankBits) | rank;
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for (int cell = get_local_id(0); cell < cellCount; cell += stepSortThreads) {
        if (blockCells[cell] != 0) {
            blockCells[cell] = atomic_add(&sorted.cellCursors[cell], blockCells[cell]);
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for (int item = 0; item < stepSortItemsPerThread; ++item) {
        const int slot = item * stepSortThreads + get_local_id(0);
        const int index = blockStart + slot;
        if (index >= count) break;
        const uint packedSlot = cellAndRank[slot];
        const uint target = blockCells[packedSlot >> rankBits] + (packedSlot & rankMask);
        stepOneBall(target, balls.position[index], balls.state[index], sorted.position, sorted.state, true,
                    balls.packed, cellWords, targetCells, params, brickHitsLeft, killedIndices, killedCapacity,
                    counters, grid, packedCapacity);
    }
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void collectHolesKernel(__global const uint* killedIndices, uint killedCount,
                                 uint survivorCount, __global uint* holes,
                                 __global FrameCounters* counters) {
    uint index = get_group_id(0) * 256 + get_local_id(0);
    bool valid = false;
    uint killed = 0;
    if (index < killedCount) {
        killed = killedIndices[index];
        valid = (killed < survivorCount);
    }
    uint mask = __builtin_amdgcn_ballot_w32(valid);
    if (valid) {
        uint wave_rank = __builtin_amdgcn_mbcnt_lo(mask, 0u);
        uint leader = __builtin_ctz(mask);
        uint base = 0;
        if ((get_local_id(0) & 31) == leader) {
            base = atomic_add((volatile __global uint*)&counters->holeCount, (uint)__builtin_popcount(mask));
        }
        base = __builtin_amdgcn_readfirstlane(base);
        holes[base + wave_rank] = killed;
    }
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void fillHolesKernel(DeviceBalls balls, uint survivorCount, uint oldCount,
                              __global const uint* holes, __global FrameCounters* counters,
                              uint packedCapacity) {
    uint source = survivorCount + get_group_id(0) * 256 + get_local_id(0);
    bool valid = (source < oldCount && stateColor(balls.state[source]) != deadColor);
    uint mask = __builtin_amdgcn_ballot_w32(valid);
    if (valid) {
        uint wave_rank = __builtin_amdgcn_mbcnt_lo(mask, 0u);
        uint leader = __builtin_ctz(mask);
        uint base = 0;
        if ((get_local_id(0) & 31) == leader) {
            base = atomic_add((volatile __global uint*)&counters->holeCursor, (uint)__builtin_popcount(mask));
        }
        base = __builtin_amdgcn_readfirstlane(base);
        uint target = holes[base + wave_rank];
        balls.position[target] = balls.position[source];
        balls.state[target] = balls.state[source];
        if (source < packedCapacity) {
            balls.packed[target] = balls.packed[source];
        }
    }
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void gridToRenderKernel(__global const ulong* slots, int cells, __global uint* renderSlots) {
    int index = get_group_id(0) * 256 + get_local_id(0);
    if (index >= cells) return;
    ulong key = slots[index];
    renderSlots[index] = key == 0ul ? emptyRenderSlot : (uint)key;
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void applyCellWordsKernel(__global const int* updates, int updateCount, __global int* cellWords) {
    int index = get_group_id(0) * 256 + get_local_id(0);
    if (index < updateCount) {
        cellWords[updates[2 * index]] = updates[2 * index + 1];
    }
}

inline float freeFlightSpan(float extent, float radius) {
    const float span = extent - 2.0f * radius;
    return span > 1e-6f ? span : 1e-6f;
}

inline uint freeFlightPhase(float position, float velocity, float extent, float radius) {
    const float span = freeFlightSpan(extent, radius);
    const float along = clampFloat(position - radius, 0.0f, span);
    const float unfolded = velocity < 0.0f ? 2.0f * span - along : along;
    const float fraction = unfolded / (2.0f * span);
    const float wrapped = fraction >= 1.0f ? 0.0f : fraction;
    return (uint)(wrapped * freeFlightPhaseUnits);
}

inline uint freeFlightAdvance(float velocity, float deltaTime, float extent, float radius) {
    const float perTick = fabs(velocity) * deltaTime / (2.0f * freeFlightSpan(extent, radius));
    return (uint)(perTick * freeFlightPhaseUnits + 0.5f);
}

inline float foldFreeFlightPhase(uint phase, float extent, float radius, float* direction) {
    const float span = freeFlightSpan(extent, radius);
    const float unfolded = (float)(phase >> 8) * (1.0f / 16777216.0f) * 2.0f * span;
    const bool outbound = phase < freeFlightPhaseHalf;
    *direction = outbound ? 1.0f : -1.0f;
    return radius + (outbound ? unfolded : 2.0f * span - unfolded);
}

inline uint freeFlightWallCrossings(uint startPhase, uint advance, uint ticks) {
    const ulong travelled = (ulong)startPhase + (ulong)advance * (ulong)ticks;
    return (uint)((travelled >> 31) - ((ulong)startPhase >> 31));
}

inline FreeFlightPosition flyFreely(float x, float y, float vx, float vy, uint ticks, StepParams params) {
    const uint phaseX = freeFlightPhase(x, vx, params.fieldWidth, params.radius);
    const uint phaseY = freeFlightPhase(y, vy, params.fieldHeight, params.radius);
    const uint advanceX = freeFlightAdvance(vx, params.deltaTime, params.fieldWidth, params.radius);
    const uint advanceY = freeFlightAdvance(vy, params.deltaTime, params.fieldHeight, params.radius);
    FreeFlightPosition flown;
    float directionX = 1.0f;
    float directionY = 1.0f;
    flown.x = foldFreeFlightPhase(phaseX + advanceX * ticks, params.fieldWidth, params.radius, &directionX);
    flown.y = foldFreeFlightPhase(phaseY + advanceY * ticks, params.fieldHeight, params.radius, &directionY);
    flown.vx = fabs(vx) * directionX;
    flown.vy = fabs(vy) * directionY;
    flown.wallCrossings = freeFlightWallCrossings(phaseX, advanceX, ticks) +
                          freeFlightWallCrossings(phaseY, advanceY, ticks);
    return flown;
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void freeFlightRenderKernel(DeviceBalls balls, int count, StepParams params,
                                     uint ticksSinceEpoch, VisibilityGrid grid,
                                     uint packedCapacity, uint chunkStride, uint chunkOffset) {
    const ulong chunk = (ulong)chunkOffset + (ulong)get_group_id(0) * (ulong)chunkStride;
    const ulong wide = chunk * 256 + get_local_id(0);
    if (wide >= (ulong)count) return;
    const uint index = (uint)wide;
    const uint state = balls.state[index];
    const uint color = stateColor(state);
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    decodePosition(balls.position[index], &x, &y, params);
    decodeVelocity(state, &vx, &vy, params);
    const FreeFlightPosition flown = flyFreely(x, y, vx, vy, ticksSinceEpoch, params);
    const uint packed = packBallForRender(flown.x, flown.y, (int)color, params.packScaleX, params.packScaleY);
    if (grid.slots) {
        bidForSlot(packed, index, grid);
    } else if (index < packedCapacity) {
        balls.packed[index] = packed;
    }
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void materializeFreeFlightKernel(DeviceBalls balls, int count, StepParams params,
                                          uint ticksSinceEpoch) {
    const int index = get_group_id(0) * 256 + get_local_id(0);
    if (index >= count) return;
    const uint state = balls.state[index];
    const uint color = stateColor(state);
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    decodePosition(balls.position[index], &x, &y, params);
    decodeVelocity(state, &vx, &vy, params);
    const FreeFlightPosition flown = flyFreely(x, y, vx, vy, ticksSinceEpoch, params);
    const int bounces = (int)((state >> bounceShift) & bounceMask) +
                        (int)min(flown.wallCrossings, (uint)savedBounceLimit);
    balls.position[index] = encodePosition(flown.x, flown.y, params);
    balls.state[index] = encodeState(flown.vx, flown.vy, color, bounces, params);
}
