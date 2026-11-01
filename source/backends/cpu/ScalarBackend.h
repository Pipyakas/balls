#pragma once

#include "cpu/CpuBackendBase.h"
#include "cpu/CpuBallStore.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class ScalarBackend final : public CpuBackendBase<CpuBallStore> {
public:
    std::string_view name() const override { return "Scalar"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    bool supportsBallCollisions() const override { return true; }
    void setBallCollisions(bool enabled) override { ballCollisions = enabled; }

private:
    struct CollisionGrid {
        float cellSize = 0.0f;
        int width = 0;
        int height = 0;
        std::vector<int> heads;
        std::vector<int> next;
    };

    void stepWithoutCollisions(const StepParams& params, std::vector<BallHit>& hits);
    void stepWithCollisions(const StepParams& params, const BoardView& board, std::vector<BallHit>& hits);
    void resolveBallCollisions(const StepParams& params);
    void buildCollisionGrid(const StepParams& params);
    bool collideWithNeighbors(size_t index, const StepParams& params);
    bool resolvePair(size_t first, size_t second, const StepParams& params);
    void rescaleSpeed(size_t index, float speed);
    void clampInsideField(size_t index, const StepParams& params);
    void repackBalls(const StepParams& params);

    bool ballCollisions = false;
    std::vector<uint8_t> active;
    std::vector<uint8_t> touched;
    std::vector<int> stoppedBricks;
    CollisionGrid grid;
};
