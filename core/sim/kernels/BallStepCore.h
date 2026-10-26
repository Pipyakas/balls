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
};

struct BoardView {
    const int* paddedCells;
    const int* brickColors;
    const int* targetCells;
};

inline constexpr int noBrickHit = -1;
inline constexpr unsigned int renderCoordinateMax = 16383u;
inline constexpr unsigned int renderCoordinateBits = 14u;
inline constexpr unsigned int renderColorShift = 28u;

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

BALLS_HOST_DEVICE void retargetTowardNearestBrick(float x, float y, float& vx, float& vy, int color,
                                                  float normalX, float normalY, const BoardView& board,
                                                  const StepParams& params) {
    const int column = static_cast<int>(floorf(x));
    const int row = static_cast<int>(floorf(y));
    const int target =
        board.targetCells[color * params.paddedCellCount + paddedCellIndex(column, row, params.paddedWidth)];
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

BALLS_HOST_DEVICE void resolveEmbeddedContact(float& x, float& y, float& vx, float& vy, int color, int& bounces,
                                              int column, int row, const BoardView& board,
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

BALLS_HOST_DEVICE int stepBall(float& x, float& y, float& vx, float& vy, int color, int& bounces,
                               const BoardView& board, const StepParams& params) {
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
    for (int candidate = 0; candidate < 4; ++candidate) {
        const int cellColumn = column + ((candidate & 1) ? neighborColumn : 0);
        const int cellRow = row + ((candidate & 2) ? neighborRow : 0);
        const int brick = board.paddedCells[paddedCellIndex(cellColumn, cellRow, params.paddedWidth)];
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
        }
    }

    if (bestBrick >= 0) {
        if (board.brickColors[bestBrick] == color) {
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

BALLS_HOST_DEVICE int stepBallSubsteps(float& x, float& y, float& vx, float& vy, int color, int& bounces,
                                       const BoardView& board, const StepParams& params) {
    for (int substep = 0; substep < params.substeps; ++substep) {
        const int brick = stepBall(x, y, vx, vy, color, bounces, board, params);
        if (brick != noBrickHit) {
            return brick;
        }
    }
    return noBrickHit;
}
