#include "sim/ReferenceBackend.h"

#include <algorithm>
#include <cmath>

void ReferenceBackend::uploadBoard(const BoardUpload& board) {
    columns = board.columns;
    rows = board.rows;
    cells.assign(board.paddedCells.begin(), board.paddedCells.end());
    if (!board.onlyChangedCells || colors.empty()) {
        colors.assign(board.brickColors.begin(), board.brickColors.end());
    }
    if (!board.targetCells.empty()) {
        targets.assign(board.targetCells.begin(), board.targetCells.end());
    }
}

void ReferenceBackend::addBalls(std::span<const BallSpawn> spawns) {
    for (const BallSpawn& spawn : spawns) {
        balls.push_back({spawn.x, spawn.y, spawn.vx, spawn.vy, spawn.color, 0});
    }
}

int ReferenceBackend::brickAt(int column, int row) const {
    if (column < 0 || row < 0 || column >= columns || row >= rows) {
        return -1;
    }
    const size_t index = static_cast<size_t>(row + 1) * static_cast<size_t>(columns + 2) + static_cast<size_t>(column + 1);
    return index < cells.size() ? cells[index] : -1;
}

int ReferenceBackend::brickColor(int brick) const {
    return brick >= 0 && static_cast<size_t>(brick) < colors.size() ? colors[static_cast<size_t>(brick)] : -1;
}

bool ReferenceBackend::targetVisible(double x, double y, int targetColumn, int targetRow) const {
    const double targetX = targetColumn + 0.5;
    const double targetY = targetRow + 0.5;
    const double distance = std::hypot(targetX - x, targetY - y);
    const int samples = static_cast<int>(std::ceil(distance / longestSubStep));
    for (int sample = 1; sample < samples; ++sample) {
        const double along = static_cast<double>(sample) / samples;
        const int column = static_cast<int>(std::floor(x + (targetX - x) * along));
        const int row = static_cast<int>(std::floor(y + (targetY - y) * along));
        if (column == targetColumn && row == targetRow) {
            return true;
        }
        if (brickAt(column, row) >= 0) {
            return false;
        }
    }
    return true;
}

void ReferenceBackend::retarget(Ball& ball, double normalX, double normalY, const StepParams& params) const {
    const int column = static_cast<int>(std::floor(ball.x));
    const int row = static_cast<int>(std::floor(ball.y));
    const size_t index = static_cast<size_t>(ball.color) * static_cast<size_t>(params.paddedCellCount) +
                         static_cast<size_t>(row + 1) * static_cast<size_t>(params.paddedWidth) +
                         static_cast<size_t>(column + 1);
    if (index >= targets.size() || targets[index] < 0) {
        return;
    }
    const int target = targets[index];
    const int targetColumn = target % params.paddedWidth - 1;
    const int targetRow = target / params.paddedWidth - 1;
    const double toX = targetColumn + 0.5 - ball.x;
    const double toY = targetRow + 0.5 - ball.y;
    const double distance = std::hypot(toX, toY);
    if (distance < 1e-9 || (toX * normalX + toY * normalY) < 0.0 || !targetVisible(ball.x, ball.y, targetColumn, targetRow)) {
        return;
    }
    ball.vx = toX / distance * params.speed;
    ball.vy = toY / distance * params.speed;
}

// Finds the brick whose square comes closest to the ball centre within one radius. A brick of the ball's color is
// a hit; any other pushes the ball out along the contact normal and reflects it.
bool ReferenceBackend::resolveContacts(Ball& ball, const StepParams& params, int& sameColorBrick) const {
    const double radius = params.radius;
    double closestDistance = radius;
    double normalX = 0.0;
    double normalY = 0.0;
    int closestBrick = -1;
    const int firstColumn = static_cast<int>(std::floor(ball.x - radius));
    const int firstRow = static_cast<int>(std::floor(ball.y - radius));
    for (int row = firstRow; row <= static_cast<int>(std::floor(ball.y + radius)); ++row) {
        for (int column = firstColumn; column <= static_cast<int>(std::floor(ball.x + radius)); ++column) {
            const int brick = brickAt(column, row);
            if (brick < 0) {
                continue;
            }
            const double nearestX = std::clamp(ball.x, static_cast<double>(column), static_cast<double>(column + 1));
            const double nearestY = std::clamp(ball.y, static_cast<double>(row), static_cast<double>(row + 1));
            const double offsetX = ball.x - nearestX;
            const double offsetY = ball.y - nearestY;
            const double distance = std::hypot(offsetX, offsetY);
            if (distance < closestDistance) {
                closestDistance = distance;
                closestBrick = brick;
                if (distance > 1e-12) {
                    normalX = offsetX / distance;
                    normalY = offsetY / distance;
                } else {
                    normalX = 0.0;
                    normalY = 1.0;
                }
            }
        }
    }
    if (closestBrick < 0) {
        return false;
    }
    if (brickColor(closestBrick) == ball.color) {
        sameColorBrick = closestBrick;
        return true;
    }
    ball.x += normalX * (radius - closestDistance);
    ball.y += normalY * (radius - closestDistance);
    const double alongNormal = ball.vx * normalX + ball.vy * normalY;
    if (alongNormal < 0.0) {
        ball.vx -= 2.0 * alongNormal * normalX;
        ball.vy -= 2.0 * alongNormal * normalY;
    }
    ball.bounces += 1;
    if (ball.bounces >= params.retargetBounceThreshold) {
        retarget(ball, normalX, normalY, params);
    }
    return true;
}

int ReferenceBackend::advance(Ball& ball, const StepParams& params) const {
    const double radius = params.radius;
    const double travel = std::hypot(ball.vx, ball.vy) * params.deltaTime;
    const int subSteps = std::max(1, static_cast<int>(std::ceil(travel / longestSubStep)));
    const double subTime = params.deltaTime / subSteps;
    for (int subStep = 0; subStep < subSteps; ++subStep) {
        ball.x += ball.vx * subTime;
        ball.y += ball.vy * subTime;
        double normalX = 0.0;
        double normalY = 0.0;
        if (ball.x < radius) {
            ball.x = 2.0 * radius - ball.x;
            ball.vx = std::fabs(ball.vx);
            normalX = 1.0;
        } else if (ball.x > params.fieldWidth - radius) {
            ball.x = 2.0 * (params.fieldWidth - radius) - ball.x;
            ball.vx = -std::fabs(ball.vx);
            normalX = -1.0;
        }
        if (ball.y < radius) {
            ball.y = 2.0 * radius - ball.y;
            ball.vy = std::fabs(ball.vy);
            normalY = 1.0;
        } else if (ball.y > params.fieldHeight - radius) {
            ball.y = 2.0 * (params.fieldHeight - radius) - ball.y;
            ball.vy = -std::fabs(ball.vy);
            normalY = -1.0;
        }
        if (normalX != 0.0 || normalY != 0.0) {
            ball.bounces += 1;
            if (ball.bounces >= params.retargetBounceThreshold) {
                retarget(ball, normalX, normalY, params);
            }
        }
        int sameColorBrick = -1;
        resolveContacts(ball, params, sameColorBrick);
        if (sameColorBrick >= 0) {
            return sameColorBrick;
        }
    }
    return -1;
}

void ReferenceBackend::step(const StepParams& params, std::vector<BallHit>& hits) {
    for (int tick = 0; tick < params.substeps; ++tick) {
        for (size_t index = 0; index < balls.size(); ++index) {
            const int brick = advance(balls[index], params);
            if (brick >= 0) {
                hits.push_back({static_cast<uint32_t>(index), brick});
            }
        }
    }
}

void ReferenceBackend::removeBalls(std::span<const uint32_t> descendingIndices) {
    for (const uint32_t index : descendingIndices) {
        balls[index] = balls.back();
        balls.pop_back();
    }
}

BallSnapshot ReferenceBackend::snapshot() {
    packed.resize(balls.size());
    for (size_t i = 0; i < balls.size(); ++i) {
        packed[i] = packBallForRender(static_cast<float>(balls[i].x), static_cast<float>(balls[i].y), balls[i].color,
                                      renderPackScale(static_cast<float>(columns)), renderPackScale(static_cast<float>(rows)));
    }
    return {packed.data(), packed.size(), static_cast<float>(columns), static_cast<float>(rows)};
}

std::vector<BallSpawn> ReferenceBackend::exportBalls() {
    std::vector<BallSpawn> exported;
    exported.reserve(balls.size());
    for (const Ball& ball : balls) {
        exported.push_back({static_cast<float>(ball.x), static_cast<float>(ball.y), static_cast<float>(ball.vx),
                            static_cast<float>(ball.vy), ball.color});
    }
    return exported;
}

std::vector<BallSpawn> ReferenceBackend::takeTail(size_t count) {
    const std::vector<BallSpawn> all = exportBalls();
    const size_t first = all.size() - std::min(count, all.size());
    balls.resize(first);
    return {all.begin() + static_cast<std::ptrdiff_t>(first), all.end()};
}

BallTally ReferenceBackend::truncate(size_t newCount) {
    BallTally removed{};
    for (size_t i = newCount; i < balls.size(); ++i) {
        ++removed[static_cast<size_t>(balls[i].color)];
    }
    balls.resize(std::min(newCount, balls.size()));
    return removed;
}
