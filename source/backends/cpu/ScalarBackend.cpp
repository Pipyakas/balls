#include "cpu/ScalarBackend.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int collisionCellNeighborRange = 1;
constexpr int maximumCollisionPasses = 8;
constexpr float coincidentDistanceSquared = 1e-12f;

int cellCoordinate(float coordinate, float cellSize, int extent) {
    const int cell = static_cast<int>(std::floor(coordinate / cellSize));
    return std::min(std::max(cell, 0), extent - 1);
}

} // namespace

void ScalarBackend::step(const StepParams& params, std::vector<BallHit>& hits) {
    if (!ballCollisions || store.count < 2) {
        stepWithoutCollisions(params, hits);
        return;
    }
    stepWithCollisions(params, boardView(), hits);
}

void ScalarBackend::stepWithoutCollisions(const StepParams& params, std::vector<BallHit>& hits) {
    const BoardView board = boardView();
    for (size_t i = 0; i < store.count; ++i) {
        const int brick = stepBallSubsteps(store.x[i], store.y[i], store.vx[i], store.vy[i], store.color[i],
                                           store.bounces[i], board, params);
        store.packed[i] =
            packBallForRender(store.x[i], store.y[i], store.color[i], params.packScaleX, params.packScaleY);
        if (brick != noBrickHit) {
            hits.push_back({static_cast<uint32_t>(i), brick});
        }
    }
}

void ScalarBackend::stepWithCollisions(const StepParams& params, const BoardView& board,
                                       std::vector<BallHit>& hits) {
    active.assign(store.count, uint8_t{1});
    stoppedBricks.assign(store.count, noBrickHit);
    size_t activeCount = store.count;
    for (int substep = 0; substep < params.substeps && activeCount != 0; ++substep) {
        for (size_t i = 0; i < store.count; ++i) {
            if (active[i] == 0) {
                continue;
            }
            const int brick = stepBall(store.x[i], store.y[i], store.vx[i], store.vy[i], store.color[i],
                                       store.bounces[i], board, params);
            if (brick != noBrickHit) {
                active[i] = 0;
                stoppedBricks[i] = brick;
                --activeCount;
            }
        }
        // Resolve after every substep; otherwise a fast ball can tunnel into another one between substeps.
        resolveBallCollisions(params);
    }
    repackBalls(params);
    for (size_t i = 0; i < store.count; ++i) {
        if (active[i] == 0) {
            hits.push_back({static_cast<uint32_t>(i), stoppedBricks[i]});
        }
    }
}

void ScalarBackend::resolveBallCollisions(const StepParams& params) {
    touched.assign(store.count, uint8_t{0});
    bool collided = false;
    for (int pass = 0; pass < maximumCollisionPasses; ++pass) {
        buildCollisionGrid(params);
        bool resolved = false;
        for (size_t i = 0; i < store.count; ++i) {
            if (active[i] == 0) {
                continue;
            }
            resolved = collideWithNeighbors(i, params) || resolved;
        }
        if (!resolved) {
            break;
        }
        collided = true;
    }
    if (!collided) {
        return;
    }
    // Every touched ball exchanged normal velocity components, which can drift from the exact speed
    // through rounding; restore it so the constant-speed invariant still holds after a collision.
    for (size_t i = 0; i < store.count; ++i) {
        if (touched[i] != 0) {
            rescaleSpeed(i, params.speed);
        }
    }
}

void ScalarBackend::buildCollisionGrid(const StepParams& params) {
    grid.cellSize = 2.0f * params.radius;
    grid.width = std::max(1, static_cast<int>(std::ceil(params.fieldWidth / grid.cellSize)));
    grid.height = std::max(1, static_cast<int>(std::ceil(params.fieldHeight / grid.cellSize)));
    grid.heads.assign(static_cast<size_t>(grid.width) * static_cast<size_t>(grid.height), -1);
    grid.next.assign(store.count, -1);
    for (size_t i = 0; i < store.count; ++i) {
        if (active[i] == 0) {
            continue;
        }
        const int column = cellCoordinate(store.x[i], grid.cellSize, grid.width);
        const int row = cellCoordinate(store.y[i], grid.cellSize, grid.height);
        const size_t cell =
            static_cast<size_t>(row) * static_cast<size_t>(grid.width) + static_cast<size_t>(column);
        grid.next[i] = grid.heads[cell];
        grid.heads[cell] = static_cast<int>(i);
    }
}

bool ScalarBackend::collideWithNeighbors(size_t index, const StepParams& params) {
    const int column = cellCoordinate(store.x[index], grid.cellSize, grid.width);
    const int row = cellCoordinate(store.y[index], grid.cellSize, grid.height);
    bool resolved = false;
    for (int rowOffset = -collisionCellNeighborRange; rowOffset <= collisionCellNeighborRange; ++rowOffset) {
        const int neighborRow = row + rowOffset;
        if (neighborRow < 0 || neighborRow >= grid.height) {
            continue;
        }
        for (int columnOffset = -collisionCellNeighborRange; columnOffset <= collisionCellNeighborRange;
             ++columnOffset) {
            const int neighborColumn = column + columnOffset;
            if (neighborColumn < 0 || neighborColumn >= grid.width) {
                continue;
            }
            const size_t cell = static_cast<size_t>(neighborRow) * static_cast<size_t>(grid.width) +
                                static_cast<size_t>(neighborColumn);
            for (int other = grid.heads[cell]; other >= 0; other = grid.next[other]) {
                // Visit each pair once, and never bounce a ball that already died on a brick this tick.
                if (static_cast<size_t>(other) <= index || active[other] == 0) {
                    continue;
                }
                resolved = resolvePair(index, static_cast<size_t>(other), params) || resolved;
            }
        }
    }
    return resolved;
}

bool ScalarBackend::resolvePair(size_t first, size_t second, const StepParams& params) {
    const float contactDistance = 2.0f * params.radius;
    const float offsetX = store.x[second] - store.x[first];
    const float offsetY = store.y[second] - store.y[first];
    const float distanceSquared = offsetX * offsetX + offsetY * offsetY;
    if (distanceSquared >= contactDistance * contactDistance) {
        return false;
    }

    float normalX = 0.0f;
    float normalY = 0.0f;
    float distance = std::sqrt(distanceSquared);
    if (distanceSquared > coincidentDistanceSquared) {
        normalX = offsetX / distance;
        normalY = offsetY / distance;
    } else {
        // Stacked balls have no geometric normal, so use their closing direction, or an arbitrary axis at
        // rest.
        const float relativeX = store.vx[second] - store.vx[first];
        const float relativeY = store.vy[second] - store.vy[first];
        const float relativeSquared = relativeX * relativeX + relativeY * relativeY;
        if (relativeSquared > coincidentDistanceSquared) {
            const float relativeLength = std::sqrt(relativeSquared);
            normalX = relativeX / relativeLength;
            normalY = relativeY / relativeLength;
        } else {
            normalX = 1.0f;
        }
        distance = 0.0f;
    }

    const float push = 0.5f * (contactDistance - distance);
    store.x[first] -= push * normalX;
    store.y[first] -= push * normalY;
    store.x[second] += push * normalX;
    store.y[second] += push * normalY;
    clampInsideField(first, params);
    clampInsideField(second, params);

    const float closingSpeed =
        (store.vx[second] - store.vx[first]) * normalX + (store.vy[second] - store.vy[first]) * normalY;
    if (closingSpeed < 0.0f) {
        // Equal-mass elastic contact: exchange the velocity components along the contact normal.
        store.vx[first] += closingSpeed * normalX;
        store.vy[first] += closingSpeed * normalY;
        store.vx[second] -= closingSpeed * normalX;
        store.vy[second] -= closingSpeed * normalY;
    }
    touched[first] = 1;
    touched[second] = 1;
    return true;
}

void ScalarBackend::rescaleSpeed(size_t index, float speed) {
    const float speedSquared = store.vx[index] * store.vx[index] + store.vy[index] * store.vy[index];
    if (speedSquared <= 0.0f) {
        return;
    }
    const float currentSpeed = std::sqrt(speedSquared);
    const float scale = speed / currentSpeed;
    store.vx[index] *= scale;
    store.vy[index] *= scale;
}

void ScalarBackend::clampInsideField(size_t index, const StepParams& params) {
    store.x[index] = std::min(std::max(store.x[index], params.radius), params.fieldWidth - params.radius);
    store.y[index] = std::min(std::max(store.y[index], params.radius), params.fieldHeight - params.radius);
}

void ScalarBackend::repackBalls(const StepParams& params) {
    for (size_t i = 0; i < store.count; ++i) {
        store.packed[i] =
            packBallForRender(store.x[i], store.y[i], store.color[i], params.packScaleX, params.packScaleY);
    }
}
