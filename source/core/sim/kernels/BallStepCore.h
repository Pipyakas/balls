#pragma once

#if defined(__CUDACC_RTC__)
#define BALLS_HOST_DEVICE __device__ __forceinline__
#else
#include <cmath>
#define BALLS_HOST_DEVICE inline
#endif

struct StepParams {
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
    // Ticks since the simulation started, at the start of this step; wraps after about a year at 120 Hz.
    unsigned int tick;
    // Nonzero when nothing on the board can change a ball's path (no bricks left, no ball-vs-ball collisions):
    // every ball then flies a closed-form path between the walls and backends may stop stepping it.
    int freeFlight;
};

struct BoardView {
    const int* paddedCells;
    const int* brickColors;
    const int* targetCells;
    const int* nearBrickCells;

    BALLS_HOST_DEVICE bool isNearBrick(int index) const { return nearBrickCells[index] != 0; }

    BALLS_HOST_DEVICE int occupant(int index, int& color) const {
        const int brick = paddedCells[index];
        color = brick < 0 ? -1 : brickColors[brick];
        return brick;
    }

    BALLS_HOST_DEVICE int targetCell(int color, int index, int cellCount) const {
        return targetCells[color * cellCount + index];
    }
};

inline constexpr int noBrickHit = -1;
inline constexpr unsigned int renderCoordinateMax = 16383u;
inline constexpr unsigned int renderCoordinateBits = 14u;
inline constexpr unsigned int renderColorShift = 28u;
inline constexpr unsigned int emptyRenderSlot = 0xFFFFFFFFu;

BALLS_HOST_DEVICE unsigned int packRenderCoordinate(float value, float scale) {
    const float scaled = value * scale;
    const float clamped = scaled < 0.0f ? 0.0f : (scaled > static_cast<float>(renderCoordinateMax) ? static_cast<float>(renderCoordinateMax) : scaled);
    return static_cast<unsigned int>(clamped + 0.5f);
}

BALLS_HOST_DEVICE unsigned int packBallForRender(float x, float y, int color, float scaleX, float scaleY) {
    return packRenderCoordinate(x, scaleX) | (packRenderCoordinate(y, scaleY) << renderCoordinateBits) |
           (static_cast<unsigned int>(color) << renderColorShift);
}

BALLS_HOST_DEVICE float clampFloat(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

BALLS_HOST_DEVICE int paddedCellIndex(int column, int row, int paddedWidth) {
    return (row + 1) * paddedWidth + column + 1;
}

BALLS_HOST_DEVICE void pushOutOfCell(float& x, float& y, float& normalX, float& normalY, int column, int row,
                                     float radius) {
    const float toLeft = x - static_cast<float>(column);
    const float toRight = static_cast<float>(column + 1) - x;
    const float toTop = y - static_cast<float>(row);
    const float toBottom = static_cast<float>(row + 1) - y;
    const float nearest = fminf(fminf(toLeft, toRight), fminf(toTop, toBottom));
    normalX = 0.0f;
    normalY = 0.0f;
    if (nearest == toBottom) {
        normalY = 1.0f;
        y = static_cast<float>(row + 1) + radius;
    } else if (nearest == toTop) {
        normalY = -1.0f;
        y = static_cast<float>(row) - radius;
    } else if (nearest == toLeft) {
        normalX = -1.0f;
        x = static_cast<float>(column) - radius;
    } else {
        normalX = 1.0f;
        x = static_cast<float>(column + 1) + radius;
    }
}

// Whether the straight line from (x, y) to the centre of the target cell crosses no other brick. The nearest
// same-color brick is found by distance alone, so it is often behind other bricks; aiming at it anyway sends the
// ball into the blocking brick, back off the wall behind it, and at the same target again, forever, and every
// ball near that column ends up on the same line. Cells are walked with a grid DDA (Amanatides and Woo).
// Stepped balls are stored back to 16-bit positions every frame, and a frame moves a ball a fixed ~255.4 units,
// so rounding to nearest dropped the same fraction every frame: a steady speed error of up to 0.2% per axis and
// 0.12 cells of drift in 10 s against an exact engine. Adding a pseudo-random fraction before truncating makes the
// rounding unbiased. The fraction comes from a hash of the ball's previous stored position, which every stepper
// has and which changes every frame; low 16 bits for x, high 16 bits for y.
BALLS_HOST_DEVICE unsigned int roundingDither(unsigned int previousPosition) {
    unsigned int hash = previousPosition * 0x9E3779B1u;
    hash ^= hash >> 15;
    hash *= 0x85EBCA77u;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE3Du;
    hash ^= hash >> 16;
    return hash;
}

BALLS_HOST_DEVICE float ditherFraction(unsigned int dither16) {
    constexpr float sixteenBitUnits = 1.0f / 65536.0f;
    return static_cast<float>(dither16 & 0xFFFFu) * sixteenBitUnits;
}

template <typename Board>
BALLS_HOST_DEVICE bool targetInSight(float x, float y, int targetColumn, int targetRow, const Board& board,
                                     int paddedWidth) {
    constexpr float unreachable = 1e30f;
    const float toX = static_cast<float>(targetColumn) + 0.5f - x;
    const float toY = static_cast<float>(targetRow) + 0.5f - y;
    int column = static_cast<int>(floorf(x));
    int row = static_cast<int>(floorf(y));
    const int stepColumn = toX > 0.0f ? 1 : -1;
    const int stepRow = toY > 0.0f ? 1 : -1;
    const float deltaX = toX != 0.0f ? fabsf(1.0f / toX) : unreachable;
    const float deltaY = toY != 0.0f ? fabsf(1.0f / toY) : unreachable;
    float nextX = toX != 0.0f ? (stepColumn > 0 ? static_cast<float>(column + 1) - x : x - static_cast<float>(column)) * deltaX
                              : unreachable;
    float nextY = toY != 0.0f ? (stepRow > 0 ? static_cast<float>(row + 1) - y : y - static_cast<float>(row)) * deltaY
                              : unreachable;
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
        if (column == targetColumn && row == targetRow) {
            return true;
        }
        int color = -1;
        if (board.occupant(paddedCellIndex(column, row, paddedWidth), color) >= 0) {
            return false;
        }
    }
    return column == targetColumn && row == targetRow;
}

template <typename Board>
BALLS_HOST_DEVICE void retargetTowardNearestBrick(float x, float y, float& vx, float& vy, int color,
                                                  float normalX, float normalY, const Board& board,
                                                  const StepParams& params) {
    const int column = static_cast<int>(floorf(x));
    const int row = static_cast<int>(floorf(y));
    const int target =
        board.targetCell(color, paddedCellIndex(column, row, params.paddedWidth), params.paddedCellCount);
    if (target < 0) {
        return;
    }
    const float targetX = static_cast<float>(target % params.paddedWidth) - 0.5f;
    const float targetY = static_cast<float>(target / params.paddedWidth) - 0.5f;
    const float toTargetX = targetX - x;
    const float toTargetY = targetY - y;
    const float distance = sqrtf(toTargetX * toTargetX + toTargetY * toTargetY);
    if (distance < 1e-4f) {
        return;
    }
    const float directionX = toTargetX / distance;
    const float directionY = toTargetY / distance;
    if (directionX * normalX + directionY * normalY < 0.0f) {
        return;
    }
    const int targetColumn = target % params.paddedWidth - 1;
    const int targetRow = target / params.paddedWidth - 1;
    if (!targetInSight(x, y, targetColumn, targetRow, board, params.paddedWidth)) {
        return;
    }
    vx = directionX * params.speed;
    vy = directionY * params.speed;
}

BALLS_HOST_DEVICE void reflectVelocity(float& vx, float& vy, float normalX, float normalY) {
    const float velocityAlongNormal = vx * normalX + vy * normalY;
    if (velocityAlongNormal < 0.0f) {
        vx -= 2.0f * velocityAlongNormal * normalX;
        vy -= 2.0f * velocityAlongNormal * normalY;
    }
}

template <typename Board>
BALLS_HOST_DEVICE void resolveEmbeddedContact(float& x, float& y, float& vx, float& vy, int color, int& bounces,
                                              int column, int row, const Board& board,
                                              const StepParams& params) {
    float normalX = 0.0f;
    float normalY = 0.0f;
    pushOutOfCell(x, y, normalX, normalY, column, row, params.radius);
    reflectVelocity(vx, vy, normalX, normalY);
    bounces += 1;
    if (bounces >= params.retargetBounceThreshold) {
        retargetTowardNearestBrick(x, y, vx, vy, color, normalX, normalY, board, params);
    }
}

template <typename Board>
BALLS_HOST_DEVICE int stepBall(float& x, float& y, float& vx, float& vy, int color, int& bounces,
                               const Board& board, const StepParams& params) {
    const float radius = params.radius;
    x += vx * params.deltaTime;
    y += vy * params.deltaTime;

    float normalX = 0.0f;
    float normalY = 0.0f;
    bool bounced = false;
    if (x < radius) {
        x = 2.0f * radius - x;
        vx = fabsf(vx);
        normalX = 1.0f;
        bounced = true;
    } else if (x > params.fieldWidth - radius) {
        x = 2.0f * (params.fieldWidth - radius) - x;
        vx = -fabsf(vx);
        normalX = -1.0f;
        bounced = true;
    }
    if (y < radius) {
        y = 2.0f * radius - y;
        vy = fabsf(vy);
        normalY = 1.0f;
        bounced = true;
    } else if (y > params.fieldHeight - radius) {
        y = 2.0f * (params.fieldHeight - radius) - y;
        vy = -fabsf(vy);
        normalY = -1.0f;
        bounced = true;
    }

    const int column = static_cast<int>(floorf(x));
    const int row = static_cast<int>(floorf(y));
    if (!board.isNearBrick(paddedCellIndex(column, row, params.paddedWidth))) {
        if (bounced) {
            bounces += 1;
            if (bounces >= params.retargetBounceThreshold) {
                retargetTowardNearestBrick(x, y, vx, vy, color, normalX, normalY, board, params);
            }
        }
        return noBrickHit;
    }
    const float fractionX = x - static_cast<float>(column);
    const float fractionY = y - static_cast<float>(row);
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
        const int brick = board.occupant(paddedCellIndex(cellColumn, cellRow, params.paddedWidth), cellColor);
        if (brick < 0) {
            continue;
        }
        const float closestX = clampFloat(x, static_cast<float>(cellColumn), static_cast<float>(cellColumn + 1));
        const float closestY = clampFloat(y, static_cast<float>(cellRow), static_cast<float>(cellRow + 1));
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

    if (bestBrick >= 0) {
        if (bestColor == color) {
            return bestBrick;
        }
        const float distance = sqrtf(bestDistanceSquared);
        if (distance > 1e-6f) {
            normalX = bestOffsetX / distance;
            normalY = bestOffsetY / distance;
            x += normalX * (radius - distance);
            y += normalY * (radius - distance);
        } else {
            pushOutOfCell(x, y, normalX, normalY, bestColumn, bestRow, radius);
        }
        reflectVelocity(vx, vy, normalX, normalY);
        bounced = true;
    }

    if (bounced) {
        bounces += 1;
        if (bounces >= params.retargetBounceThreshold) {
            retargetTowardNearestBrick(x, y, vx, vy, color, normalX, normalY, board, params);
        }
    }
    return noBrickHit;
}

template <typename Board>
BALLS_HOST_DEVICE int stepBallSubsteps(float& x, float& y, float& vx, float& vy, int color, int& bounces,
                                       const Board& board, const StepParams& params) {
    for (int substep = 0; substep < params.substeps; ++substep) {
        const int brick = stepBall(x, y, vx, vy, color, bounces, board, params);
        if (brick != noBrickHit) {
            return brick;
        }
    }
    return noBrickHit;
}

// Free flight. Between two walls a ball's coordinate is a triangle wave of time. Unfolding the reflections turns it
// into a straight line over a period of twice the travel span, so a ball's state at any tick is its unfolded phase
// at an epoch tick plus a constant per-tick advance. Phases are 32-bit fixed point over one period, so the advance
// wraps exactly and the position at tick t needs no stepping in between: phase(t) = phase(E) + advance * (t - E).
inline constexpr float freeFlightPhaseUnits = 4294967296.0f;
inline constexpr unsigned int freeFlightPhaseHalf = 0x80000000u;

BALLS_HOST_DEVICE float freeFlightSpan(float extent, float radius) {
    const float span = extent - 2.0f * radius;
    return span > 1e-6f ? span : 1e-6f;
}

BALLS_HOST_DEVICE unsigned int freeFlightPhase(float position, float velocity, float extent, float radius) {
    const float span = freeFlightSpan(extent, radius);
    const float along = clampFloat(position - radius, 0.0f, span);
    const float unfolded = velocity < 0.0f ? 2.0f * span - along : along;
    const float fraction = unfolded / (2.0f * span);
    const float wrapped = fraction >= 1.0f ? 0.0f : fraction;
    return static_cast<unsigned int>(wrapped * freeFlightPhaseUnits);
}

BALLS_HOST_DEVICE unsigned int freeFlightAdvance(float velocity, float deltaTime, float extent, float radius) {
    const float perTick = fabsf(velocity) * deltaTime / (2.0f * freeFlightSpan(extent, radius));
    return static_cast<unsigned int>(perTick * freeFlightPhaseUnits + 0.5f);
}

// Folds a phase back into the box. Only the top 24 bits reach the float, which is all a float can hold.
BALLS_HOST_DEVICE float foldFreeFlightPhase(unsigned int phase, float extent, float radius, float& direction) {
    constexpr float phaseToFraction = 1.0f / 16777216.0f;
    const float span = freeFlightSpan(extent, radius);
    const float unfolded = static_cast<float>(phase >> 8) * phaseToFraction * 2.0f * span;
    const bool outbound = phase < freeFlightPhaseHalf;
    direction = outbound ? 1.0f : -1.0f;
    return radius + (outbound ? unfolded : 2.0f * span - unfolded);
}

// Wall crossings between two phases on one axis, given the ticks flown; each half period is one wall.
BALLS_HOST_DEVICE unsigned int freeFlightWallCrossings(unsigned int startPhase, unsigned int advance,
                                                       unsigned int ticks) {
    const unsigned long long travelled = static_cast<unsigned long long>(startPhase) +
                                         static_cast<unsigned long long>(advance) * ticks;
    return static_cast<unsigned int>((travelled >> 31) - (startPhase >> 31));
}

struct FreeFlightPosition {
    float x;
    float y;
    float vx;
    float vy;
    unsigned int wallCrossings;
};

// Where a ball that was at (x, y) moving (vx, vy) at some tick is after flying freely for `ticks` more.
BALLS_HOST_DEVICE FreeFlightPosition flyFreely(float x, float y, float vx, float vy, unsigned int ticks,
                                              const StepParams& params) {
    const unsigned int phaseX = freeFlightPhase(x, vx, params.fieldWidth, params.radius);
    const unsigned int phaseY = freeFlightPhase(y, vy, params.fieldHeight, params.radius);
    const unsigned int advanceX = freeFlightAdvance(vx, params.deltaTime, params.fieldWidth, params.radius);
    const unsigned int advanceY = freeFlightAdvance(vy, params.deltaTime, params.fieldHeight, params.radius);
    FreeFlightPosition flown{};
    float directionX = 1.0f;
    float directionY = 1.0f;
    flown.x = foldFreeFlightPhase(phaseX + advanceX * ticks, params.fieldWidth, params.radius, directionX);
    flown.y = foldFreeFlightPhase(phaseY + advanceY * ticks, params.fieldHeight, params.radius, directionY);
    flown.vx = fabsf(vx) * directionX;
    flown.vy = fabsf(vy) * directionY;
    flown.wallCrossings = freeFlightWallCrossings(phaseX, advanceX, ticks) +
                          freeFlightWallCrossings(phaseY, advanceY, ticks);
    return flown;
}

// The state that reaches (x, y, vx, vy) after flying freely for `ticks`: how a ball added while a backend holds
// every ball at an older epoch is stored without stepping anything.
BALLS_HOST_DEVICE FreeFlightPosition flyFreelyBackwards(float x, float y, float vx, float vy, unsigned int ticks,
                                                       const StepParams& params) {
    FreeFlightPosition reversed = flyFreely(x, y, -vx, -vy, ticks, params);
    reversed.vx = -reversed.vx;
    reversed.vy = -reversed.vy;
    return reversed;
}
