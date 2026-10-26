#include "sim/backends/CpuBallStore.h"

namespace {

size_t roundUpToLanes(size_t count) { return (count + simdLaneCount - 1) / simdLaneCount * simdLaneCount; }

}

void CpuBallStore::resize(size_t newCount) {
    count = newCount;
    const size_t padded = roundUpToLanes(newCount);
    x.resize(padded, 0.5f);
    y.resize(padded, 0.5f);
    vx.resize(padded, 0.0f);
    vy.resize(padded, 0.0f);
    color.resize(padded, 0);
    bounces.resize(padded, 0);
}

void CpuBallStore::add(std::span<const BallSpawn> spawns) {
    const size_t first = count;
    resize(count + spawns.size());
    for (size_t i = 0; i < spawns.size(); ++i) {
        const BallSpawn& spawn = spawns[i];
        x[first + i] = spawn.x;
        y[first + i] = spawn.y;
        vx[first + i] = spawn.vx;
        vy[first + i] = spawn.vy;
        color[first + i] = spawn.color;
        bounces[first + i] = 0;
    }
}

void CpuBallStore::swapRemove(std::span<const uint32_t> descendingIndices) {
    size_t last = count;
    for (uint32_t index : descendingIndices) {
        --last;
        x[index] = x[last];
        y[index] = y[last];
        vx[index] = vx[last];
        vy[index] = vy[last];
        color[index] = color[last];
        bounces[index] = bounces[last];
    }
    resize(last);
}

std::vector<BallSpawn> CpuBallStore::exportBalls() const {
    std::vector<BallSpawn> balls(count);
    for (size_t i = 0; i < count; ++i) {
        balls[i] = {x[i], y[i], vx[i], vy[i], color[i]};
    }
    return balls;
}
