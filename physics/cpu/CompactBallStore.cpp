#include "cpu/CompactBallStore.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr size_t biddersPerSlot = 8;
constexpr uint32_t largestBidderMask = 0x3FFFFFFFu;

uint32_t bidderMaskFor(size_t balls, size_t cells) {
    uint32_t mask = 0;
    while (mask < largestBidderMask && (static_cast<size_t>(mask) + 1) * 2 * cells * biddersPerSlot <= balls) {
        mask = mask * 2 + 1;
    }
    return mask;
}

size_t roundUpToLanes(size_t count) { return (count + simdLaneCount - 1) / simdLaneCount * simdLaneCount; }

uint32_t encodeAxis(float value, float extent) {
    const float scaled = value / extent * compact::positionScale + 0.5f;
    return static_cast<uint32_t>(std::clamp(scaled, 0.0f, compact::positionScale));
}

uint32_t encodeComponent(float value) {
    const int quantized = static_cast<int>(std::lrint(std::clamp(value, -1.0f, 1.0f) * compact::directionScale));
    return static_cast<uint32_t>(quantized) & compact::directionMask;
}

int signedComponent(uint32_t bits) {
    return static_cast<int32_t>(bits << (32 - compact::directionBits)) >> (32 - compact::directionBits);
}

uint32_t stateColor(uint32_t state) { return (state >> compact::colorShift) & compact::colorMask; }

}

void CompactBallStore::resize(size_t newCount) {
    count = newCount;
    const size_t padded = roundUpToLanes(newCount);
    position.resize(padded, 0);
    state.resize(padded, 0);
    packed.resize(padded, 0);
}

void CompactBallStore::add(std::span<const BallSpawn> spawns) {
    if (spawns.empty()) {
        return;
    }
    speed = std::max(std::hypot(spawns.front().vx, spawns.front().vy), 1e-6f);
    const float inverseSpeed = 1.0f / speed;
    const size_t first = count;
    resize(count + spawns.size());
    for (size_t i = 0; i < spawns.size(); ++i) {
        const BallSpawn& spawn = spawns[i];
        const uint32_t color = static_cast<uint32_t>(spawn.color);
        position[first + i] = encodeAxis(spawn.x, fieldWidth) | (encodeAxis(spawn.y, fieldHeight) << 16);
        state[first + i] = encodeComponent(spawn.vx * inverseSpeed) |
                           (encodeComponent(spawn.vy * inverseSpeed) << compact::directionBits) |
                           (color << compact::colorShift);
        packed[first + i] = packBallForRender(spawn.x, spawn.y, spawn.color, renderPackScale(fieldWidth),
                                              renderPackScale(fieldHeight));
    }
}

void CompactBallStore::swapRemove(std::span<const uint32_t> descendingIndices) {
    size_t last = count;
    for (uint32_t index : descendingIndices) {
        --last;
        position[index] = position[last];
        state[index] = state[last];
        packed[index] = packed[last];
    }
    resize(last);
}

BallTally CompactBallStore::truncate(size_t newCount) {
    BallTally removed{};
    for (size_t i = newCount; i < count; ++i) {
        ++removed[stateColor(state[i])];
    }
    if (newCount < count) {
        resize(newCount);
    }
    return removed;
}

BallSpawn CompactBallStore::decode(size_t index) const {
    const uint32_t bits = state[index];
    const float dx = static_cast<float>(signedComponent(bits & compact::directionMask));
    const float dy = static_cast<float>(signedComponent((bits >> compact::directionBits) & compact::directionMask));
    const float length = std::max(std::hypot(dx, dy), 1.0f);
    return {static_cast<float>(position[index] & compact::positionMask) * fieldWidth / compact::positionScale,
            static_cast<float>(position[index] >> 16) * fieldHeight / compact::positionScale, dx / length * speed,
            dy / length * speed, static_cast<int32_t>(stateColor(bits))};
}

std::vector<BallSpawn> CompactBallStore::exportBalls(size_t first) const {
    std::vector<BallSpawn> balls;
    balls.reserve(count - std::min(first, count));
    for (size_t i = first; i < count; ++i) {
        balls.push_back(decode(i));
    }
    return balls;
}

std::vector<BallSpawn> CompactBallStore::takeTail(size_t tailCount) {
    const size_t first = count - std::min(tailCount, count);
    std::vector<BallSpawn> tail = exportBalls(first);
    resize(first);
    return tail;
}

void CompactBallStore::takeCompactTail(size_t tailCount, CompactBalls& balls) {
    const size_t first = count - std::min(tailCount, count);
    balls.position.assign(position.begin() + static_cast<std::ptrdiff_t>(first),
                          position.begin() + static_cast<std::ptrdiff_t>(count));
    balls.state.assign(state.begin() + static_cast<std::ptrdiff_t>(first), state.begin() + static_cast<std::ptrdiff_t>(count));
    resize(first);
}

void CompactBallStore::addCompact(const CompactBalls& balls) {
    const size_t first = count;
    const size_t added = std::min(balls.position.size(), balls.state.size());
    resize(count + added);
    std::copy_n(balls.position.begin(), added, position.begin() + static_cast<std::ptrdiff_t>(first));
    std::copy_n(balls.state.begin(), added, state.begin() + static_cast<std::ptrdiff_t>(first));
    for (size_t i = 0; i < added; ++i) {
        packed[first + i] = packCompactForRender(balls.position[i], balls.state[i]);
    }
}

bool CompactBallStore::prepareGrid() {
    const size_t cells = gridCellCount();
    gridFilled = cells > 0 && count > cells;
    if (gridFilled) {
        grid.assign(cells, emptyRenderSlot);
        bidderMask = bidderMaskFor(count, cells);
        ++frameSeed;
    }
    return gridFilled;
}

BallSnapshot CompactBallStore::snapshot() const {
    if (gridFilled) {
        return {grid.data(), grid.size(), fieldWidth, fieldHeight, false, true};
    }
    return {packed.data(), count, fieldWidth, fieldHeight};
}
