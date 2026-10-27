#pragma once

#include "cpu/SimdVector.h"
#include "sim/PhysicsBackend.h"

struct CpuBallStore {
    SimdVector<float> x;
    SimdVector<float> y;
    SimdVector<float> vx;
    SimdVector<float> vy;
    SimdVector<int32_t> color;
    SimdVector<int32_t> bounces;
    SimdVector<uint32_t> packed;
    size_t count = 0;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;

    void add(std::span<const BallSpawn> spawns);
    void swapRemove(std::span<const uint32_t> descendingIndices);
    void clear() { resize(0); }
    BallTally truncate(size_t newCount);
    BallSnapshot snapshot() const { return {packed.data(), count, fieldWidth, fieldHeight}; }
    std::vector<BallSpawn> exportBalls(size_t first = 0) const;
    std::vector<BallSpawn> takeTail(size_t tailCount);

private:
    void resize(size_t newCount);
};
