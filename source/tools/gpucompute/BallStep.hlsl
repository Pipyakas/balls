cbuffer StepConstants : register(b0) {
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
    uint ballCount;
    uint killedCapacity;
    uint groupsPerRow;
    uint packedCapacity;
    uint toSorted;
    int sortColumns;
    int sortRows;
    uint padding0;
    uint padding1;
};

StructuredBuffer<int> cellWords : register(t0);
StructuredBuffer<int> targetCells : register(t1);
RWStructuredBuffer<uint> positions : register(u0);
RWStructuredBuffer<uint> states : register(u1);
RWStructuredBuffer<uint> packedBalls : register(u2);
RWStructuredBuffer<int> brickHitsLeft : register(u3);
RWStructuredBuffer<uint> killedIndices : register(u4);
RWStructuredBuffer<uint> counters : register(u5);
RWStructuredBuffer<uint> cellCursors : register(u6);
RWStructuredBuffer<uint> sortedPositions : register(u7);
RWStructuredBuffer<uint> sortedStates : register(u8);

#ifndef THREADS_PER_GROUP
#define THREADS_PER_GROUP 256
#endif
#ifndef BALLS_PER_THREAD
#define BALLS_PER_THREAD 4
#endif
#define NO_BRICK_HIT -1
#define EMPTY_FAR_CELL -1
#define CELL_COLOR_MASK 7
#define CELL_BRICK_SHIFT 3
#define POSITION_SCALE 65535.0f
#define POSITION_MASK 0xFFFFu
#define DIRECTION_BITS 13
#define DIRECTION_MASK 0x1FFFu
#define DIRECTION_SCALE 4095.0f
#define COLOR_SHIFT 26u
#define COLOR_MASK 7u
#define BOUNCE_SHIFT 29u
#define BOUNCE_MASK 3u
#define DEAD_COLOR 7u
#define SAVED_BOUNCE_LIMIT 3
#define RENDER_COORDINATE_BITS 14u
#define RENDER_COLOR_SHIFT 28u
#define KILLED_COUNTER 0
#define KILLS_BY_COLOR_COUNTER 3
#define MAX_SORT_CELLS 4096
#ifndef COUNT_THREADS
#define COUNT_THREADS 256
#endif
#ifndef COUNT_ITEMS
#define COUNT_ITEMS 16
#endif
#ifndef SORT_THREADS
#define SORT_THREADS 512
#endif
#ifndef SORT_ITEMS
#define SORT_ITEMS 8
#endif
#define SORT_BALLS_PER_GROUP (SORT_THREADS * SORT_ITEMS)
#define RANK_BITS 12u
#define RANK_MASK 0xFFFu

int paddedCellIndex(int column, int row) { return (row + 1) * paddedWidth + column + 1; }

bool isNearBrick(int index) { return cellWords[index] != EMPTY_FAR_CELL; }

int occupant(int index, out int color) {
    const int word = cellWords[index];
    color = word < 0 ? -1 : (word & CELL_COLOR_MASK);
    return word < 0 ? -1 : (word >> CELL_BRICK_SHIFT);
}

void pushOutOfCell(inout float x, inout float y, out float normalX, out float normalY, int column, int row) {
    const float toLeft = x - (float)column;
    const float toRight = (float)(column + 1) - x;
    const float toTop = y - (float)row;
    const float toBottom = (float)(row + 1) - y;
    const float nearest = min(min(toLeft, toRight), min(toTop, toBottom));
    normalX = 0.0f;
    normalY = 0.0f;
    if (nearest == toBottom) {
        normalY = 1.0f;
        y = (float)(row + 1) + radius;
    } else if (nearest == toTop) {
        normalY = -1.0f;
        y = (float)row - radius;
    } else if (nearest == toLeft) {
        normalX = -1.0f;
        x = (float)column - radius;
    } else {
        normalX = 1.0f;
        x = (float)(column + 1) + radius;
    }
}

// See targetInSight in source/core/sim/kernels/BallStepCore.h: only aim at a target no other brick hides.
bool targetInSight(float x, float y, int targetColumn, int targetRow) {
    const float unreachable = 1e30f;
    const float toX = (float)targetColumn + 0.5f - x;
    const float toY = (float)targetRow + 0.5f - y;
    int column = (int)floor(x);
    int row = (int)floor(y);
    const int stepColumn = toX > 0.0f ? 1 : -1;
    const int stepRow = toY > 0.0f ? 1 : -1;
    const float deltaX = toX != 0.0f ? abs(1.0f / toX) : unreachable;
    const float deltaY = toY != 0.0f ? abs(1.0f / toY) : unreachable;
    float nextX = toX != 0.0f ? (stepColumn > 0 ? (float)(column + 1) - x : x - (float)column) * deltaX : unreachable;
    float nextY = toY != 0.0f ? (stepRow > 0 ? (float)(row + 1) - y : y - (float)row) * deltaY : unreachable;
    const int cellsToCross = abs(column - targetColumn) + abs(row - targetRow);
    for (int crossed = 0; crossed < cellsToCross; ++crossed) {
        if (nextX < nextY) {
            nextX += deltaX;
            column += stepColumn;
        } else {
            nextY += deltaY;
            row += stepRow;
        }
        if (column == targetColumn && row == targetRow) {
            return true;
        }
        if (cellWords[paddedCellIndex(column, row)] >= 0) {
            return false;
        }
    }
    return column == targetColumn && row == targetRow;
}

void retargetTowardNearestBrick(float x, float y, inout float vx, inout float vy, int color, float normalX,
                                float normalY) {
    const int column = (int)floor(x);
    const int row = (int)floor(y);
    const int target = targetCells[color * paddedCellCount + paddedCellIndex(column, row)];
    if (target < 0) {
        return;
    }
    const float targetX = (float)(target % paddedWidth) - 0.5f;
    const float targetY = (float)(target / paddedWidth) - 0.5f;
    const float toTargetX = targetX - x;
    const float toTargetY = targetY - y;
    const float distance = sqrt(toTargetX * toTargetX + toTargetY * toTargetY);
    if (distance < 1e-4f) {
        return;
    }
    const float directionX = toTargetX / distance;
    const float directionY = toTargetY / distance;
    if (directionX * normalX + directionY * normalY < 0.0f) {
        return;
    }
    if (!targetInSight(x, y, target % paddedWidth - 1, target / paddedWidth - 1)) {
        return;
    }
    vx = directionX * speed;
    vy = directionY * speed;
}

void reflectVelocity(inout float vx, inout float vy, float normalX, float normalY) {
    const float velocityAlongNormal = vx * normalX + vy * normalY;
    if (velocityAlongNormal < 0.0f) {
        vx -= 2.0f * velocityAlongNormal * normalX;
        vy -= 2.0f * velocityAlongNormal * normalY;
    }
}

void countBounce(float x, float y, inout float vx, inout float vy, int color, inout int bounces, float normalX,
                 float normalY) {
    bounces += 1;
    if (bounces >= retargetBounceThreshold) {
        retargetTowardNearestBrick(x, y, vx, vy, color, normalX, normalY);
    }
}

int stepBall(inout float x, inout float y, inout float vx, inout float vy, int color, inout int bounces) {
    x += vx * deltaTime;
    y += vy * deltaTime;
    float normalX = 0.0f;
    float normalY = 0.0f;
    bool bounced = false;
    if (x < radius) {
        x = 2.0f * radius - x;
        vx = abs(vx);
        normalX = 1.0f;
        bounced = true;
    } else if (x > fieldWidth - radius) {
        x = 2.0f * (fieldWidth - radius) - x;
        vx = -abs(vx);
        normalX = -1.0f;
        bounced = true;
    }
    if (y < radius) {
        y = 2.0f * radius - y;
        vy = abs(vy);
        normalY = 1.0f;
        bounced = true;
    } else if (y > fieldHeight - radius) {
        y = 2.0f * (fieldHeight - radius) - y;
        vy = -abs(vy);
        normalY = -1.0f;
        bounced = true;
    }

    const int column = (int)floor(x);
    const int row = (int)floor(y);
    if (!isNearBrick(paddedCellIndex(column, row))) {
        if (bounced) {
            countBounce(x, y, vx, vy, color, bounces, normalX, normalY);
        }
        return NO_BRICK_HIT;
    }
    const float fractionX = x - (float)column;
    const float fractionY = y - (float)row;
    const int neighborColumn = fractionX < radius ? -1 : (fractionX > 1.0f - radius ? 1 : 0);
    const int neighborRow = fractionY < radius ? -1 : (fractionY > 1.0f - radius ? 1 : 0);

    float bestDistanceSquared = radius * radius;
    float bestOffsetX = 0.0f;
    float bestOffsetY = 0.0f;
    int bestColumn = 0;
    int bestRow = 0;
    int bestBrick = -1;
    int bestColor = -1;
    [unroll] for (int candidate = 0; candidate < 4; ++candidate) {
        const int cellColumn = column + ((candidate & 1) ? neighborColumn : 0);
        const int cellRow = row + ((candidate & 2) ? neighborRow : 0);
        int cellColor = -1;
        const int brick = occupant(paddedCellIndex(cellColumn, cellRow), cellColor);
        if (brick >= 0) {
            const float closestX = clamp(x, (float)cellColumn, (float)(cellColumn + 1));
            const float closestY = clamp(y, (float)cellRow, (float)(cellRow + 1));
            const float offsetX = x - closestX;
            const float offsetY = y - closestY;
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
    }

    if (bestBrick >= 0) {
        if (bestColor == color) {
            return bestBrick;
        }
        const float distance = sqrt(bestDistanceSquared);
        if (distance > 1e-6f) {
            normalX = bestOffsetX / distance;
            normalY = bestOffsetY / distance;
            x += normalX * (radius - distance);
            y += normalY * (radius - distance);
        } else {
            pushOutOfCell(x, y, normalX, normalY, bestColumn, bestRow);
        }
        reflectVelocity(vx, vy, normalX, normalY);
        bounced = true;
    }
    if (bounced) {
        countBounce(x, y, vx, vy, color, bounces, normalX, normalY);
    }
    return NO_BRICK_HIT;
}

uint encodeAxis(float value, float extent) {
    const float scaled = value / extent * POSITION_SCALE + 0.5f;
    return (uint)min(max(scaled, 0.0f), POSITION_SCALE);
}

int signedComponent(uint bits) { return ((int)(bits << (32 - DIRECTION_BITS))) >> (32 - DIRECTION_BITS); }

uint encodeComponent(float value) {
    const int quantized = (int)round(clamp(value, -1.0f, 1.0f) * DIRECTION_SCALE);
    return ((uint)quantized) & DIRECTION_MASK;
}

bool tryConsumeHit(int brick) {
    int current = brickHitsLeft[brick];
    [allow_uav_condition] while (current > 0) {
        int previous = 0;
        InterlockedCompareExchange(brickHitsLeft[brick], current, current - 1, previous);
        if (previous == current) {
            return true;
        }
        current = previous;
    }
    return false;
}

void storeState(uint index, uint value) {
    if (toSorted != 0) {
        sortedStates[index] = value;
    } else {
        states[index] = value;
    }
}

void stepOneBall(uint index, uint startPosition, uint startState) {
    const uint color = (startState >> COLOR_SHIFT) & COLOR_MASK;
    if (color == DEAD_COLOR) {
        if (toSorted != 0) {
            sortedPositions[index] = startPosition;
            sortedStates[index] = startState;
        }
        return;
    }
    float x = (float)(startPosition & POSITION_MASK) * (fieldWidth / POSITION_SCALE);
    float y = (float)(startPosition >> 16) * (fieldHeight / POSITION_SCALE);
    const float dx = (float)signedComponent(startState & DIRECTION_MASK);
    const float dy = (float)signedComponent((startState >> DIRECTION_BITS) & DIRECTION_MASK);
    const float scale = speed * rsqrt(max(dx * dx + dy * dy, 1.0f));
    float vx = dx * scale;
    float vy = dy * scale;
    const int startBounces = (int)((startState >> BOUNCE_SHIFT) & BOUNCE_MASK);
    int bounces = startBounces;
    int brick = NO_BRICK_HIT;
    for (int substep = 0; substep < substeps; ++substep) {
        brick = stepBall(x, y, vx, vy, (int)color, bounces);
        if (brick != NO_BRICK_HIT) {
            break;
        }
    }
    const uint position = encodeAxis(x, fieldWidth) | (encodeAxis(y, fieldHeight) << 16);
    if (toSorted != 0) {
        sortedPositions[index] = position;
    } else {
        positions[index] = position;
    }
    if (bounces != startBounces) {
        const float inverseSpeed = 1.0f / speed;
        const uint savedBounces = (uint)min(bounces, SAVED_BOUNCE_LIMIT);
        storeState(index, encodeComponent(vx * inverseSpeed) | (encodeComponent(vy * inverseSpeed) << DIRECTION_BITS) |
                              (color << COLOR_SHIFT) | (savedBounces << BOUNCE_SHIFT));
    } else if (toSorted != 0) {
        sortedStates[index] = startState;
    }
    if (index < packedCapacity) {
        packedBalls[index] = ((position & POSITION_MASK) >> 2) | (((position >> 16) >> 2) << RENDER_COORDINATE_BITS) |
                             (color << RENDER_COLOR_SHIFT);
    }
    if (brick != NO_BRICK_HIT && tryConsumeHit(brick)) {
        uint slot = 0;
        InterlockedAdd(counters[KILLED_COUNTER], 1u, slot);
        if (slot < killedCapacity) {
            storeState(index, DEAD_COLOR << COLOR_SHIFT);
            killedIndices[slot] = index;
            InterlockedAdd(counters[KILLS_BY_COLOR_COUNTER + color], 1u);
        } else {
            InterlockedAdd(brickHitsLeft[brick], 1);
        }
    }
}

uint sortCellOf(uint position) {
    const int column = min((int)(((position & POSITION_MASK) * (uint)sortColumns) >> 16), sortColumns - 1);
    const int row = min((int)(((position >> 16) * (uint)sortRows) >> 16), sortRows - 1);
    return (uint)(row * sortColumns + column);
}

#if defined(COUNT_PASS)
groupshared uint localCells[MAX_SORT_CELLS];

[numthreads(COUNT_THREADS, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    const uint cellCount = (uint)(sortColumns * sortRows);
    for (uint cell = thread.x; cell < cellCount; cell += COUNT_THREADS) {
        localCells[cell] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint groupIndex = group.y * groupsPerRow + group.x;
    const uint blockStart = groupIndex * COUNT_ITEMS * COUNT_THREADS;
    for (uint item = 0; item < COUNT_ITEMS; ++item) {
        const uint index = blockStart + item * COUNT_THREADS + thread.x;
        if (index < ballCount) {
            InterlockedAdd(localCells[sortCellOf(positions[index])], 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint total = thread.x; total < cellCount; total += COUNT_THREADS) {
        if (localCells[total] != 0u) {
            InterlockedAdd(cellCursors[total], localCells[total]);
        }
    }
}
#elif defined(SCAN_PASS)
groupshared uint scanCells[MAX_SORT_CELLS];

[numthreads(COUNT_THREADS, 1, 1)]
void computeMain(uint3 thread : SV_GroupThreadID) {
    const uint cellCount = (uint)(sortColumns * sortRows);
    for (uint cell = thread.x; cell < cellCount; cell += COUNT_THREADS) {
        scanCells[cell] = cellCursors[cell];
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread.x == 0) {
        uint running = 0u;
        for (uint index = 0; index < cellCount; ++index) {
            const uint cellTotal = scanCells[index];
            scanCells[index] = running;
            running += cellTotal;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint written = thread.x; written < cellCount; written += COUNT_THREADS) {
        cellCursors[written] = scanCells[written];
    }
}
#elif defined(SORT_PASS)
groupshared uint blockCells[MAX_SORT_CELLS];
groupshared uint cellAndRank[SORT_BALLS_PER_GROUP];

[numthreads(SORT_THREADS, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    const uint cellCount = (uint)(sortColumns * sortRows);
    for (uint cell = thread.x; cell < cellCount; cell += SORT_THREADS) {
        blockCells[cell] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint groupIndex = group.y * groupsPerRow + group.x;
    const uint blockStart = groupIndex * SORT_BALLS_PER_GROUP;
    for (uint item = 0; item < SORT_ITEMS; ++item) {
        const uint slot = item * SORT_THREADS + thread.x;
        const uint index = blockStart + slot;
        uint packedSlot = 0u;
        if (index < ballCount) {
            const uint ballCell = sortCellOf(positions[index]);
            uint rank = 0u;
            InterlockedAdd(blockCells[ballCell], 1u, rank);
            packedSlot = (ballCell << RANK_BITS) | rank;
        }
        cellAndRank[slot] = packedSlot;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint claimed = thread.x; claimed < cellCount; claimed += SORT_THREADS) {
        if (blockCells[claimed] != 0u) {
            uint base = 0u;
            InterlockedAdd(cellCursors[claimed], blockCells[claimed], base);
            blockCells[claimed] = base;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    [loop] for (uint ball = 0; ball < SORT_ITEMS; ++ball) {
        const uint slot = ball * SORT_THREADS + thread.x;
        const uint index = blockStart + slot;
        if (index < ballCount) {
            const uint packedSlot = cellAndRank[slot];
            const uint target = blockCells[packedSlot >> RANK_BITS] + (packedSlot & RANK_MASK);
            stepOneBall(target, positions[index], states[index]);
        }
    }
}
#else
[numthreads(THREADS_PER_GROUP, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    const uint groupIndex = group.y * groupsPerRow + group.x;
    const uint blockStart = groupIndex * BALLS_PER_THREAD * THREADS_PER_GROUP;
    uint startPositions[BALLS_PER_THREAD];
    uint startStates[BALLS_PER_THREAD];
    [unroll] for (uint item = 0; item < BALLS_PER_THREAD; ++item) {
        const uint index = blockStart + item * THREADS_PER_GROUP + thread.x;
        startPositions[item] = index < ballCount ? positions[index] : 0u;
        startStates[item] = index < ballCount ? states[index] : 0u;
    }
    [loop] for (uint ball = 0; ball < BALLS_PER_THREAD; ++ball) {
        const uint index = blockStart + ball * THREADS_PER_GROUP + thread.x;
        if (index < ballCount) {
            stepOneBall(index, startPositions[ball], startStates[ball]);
        }
    }
}
#endif
