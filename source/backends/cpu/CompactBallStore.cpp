#include "cpu/CompactBallStore.h"

#include <algorithm>
#include <atomic>
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

bool CompactBallStore::resize(size_t newCount) {
    const size_t padded = roundUpToLanes(newCount);
    if (!position.resize(padded) || !state.resize(padded)) {
        position.resize(roundUpToLanes(count));
        state.resize(roundUpToLanes(count));
        return false;
    }
    count = newCount;
    if (!gridFilled) {
        packed.resize(padded, 0);
    }
    return true;
}

size_t CompactBallStore::add(std::span<const BallSpawn> spawns, const RangeRunner& runRanges) {
    if (spawns.empty()) {
        return 0;
    }
    speed = std::max(std::hypot(spawns.front().vx, spawns.front().vy), 1e-6f);
    const float inverseSpeed = 1.0f / speed;
    const size_t first = count;
    const unsigned int ticksSinceEpoch = clockTick - freeFlightEpoch;
    if (!resize(count + spawns.size())) {
        return 0;
    }
    const auto encode = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const BallSpawn& spawn = spawns[i];
            const uint32_t color = static_cast<uint32_t>(spawn.color);
            int directionX = signedComponent(encodeComponent(spawn.vx * inverseSpeed));
            int directionY = signedComponent(encodeComponent(spawn.vy * inverseSpeed));
            float x = spawn.x;
            float y = spawn.y;
            if (frozen) {
                // Projected with the quantized direction that decoding gives back, flipping only the signs,
                // so flying forward from the epoch lands on the spawn point.
                const float scale = speed / std::max(std::hypot(static_cast<float>(directionX),
                                                                static_cast<float>(directionY)),
                                                     1.0f);
                const FreeFlightPosition epoch =
                    flyFreelyBackwards(x, y, static_cast<float>(directionX) * scale,
                                       static_cast<float>(directionY) * scale, ticksSinceEpoch, flightParams);
                x = epoch.x;
                y = epoch.y;
                directionX = (epoch.vx < 0.0f) == (directionX < 0) ? directionX : -directionX;
                directionY = (epoch.vy < 0.0f) == (directionY < 0) ? directionY : -directionY;
            }
            position[first + i] = encodeAxis(x, fieldWidth) | (encodeAxis(y, fieldHeight) << 16);
            state[first + i] =
                (static_cast<uint32_t>(directionX) & compact::directionMask) |
                ((static_cast<uint32_t>(directionY) & compact::directionMask) << compact::directionBits) |
                (color << compact::colorShift);
            if (!tracksPacked()) {
                continue;
            }
            packed[first + i] = packBallForRender(spawn.x, spawn.y, spawn.color, renderPackScale(fieldWidth),
                                                  renderPackScale(fieldHeight));
        }
    };
    if (runRanges) {
        runRanges(spawns.size(), encode);
    } else {
        encode(0, spawns.size());
    }
    return spawns.size();
}

void CompactBallStore::swapRemove(std::span<const uint32_t> descendingIndices) {
    size_t last = count;
    for (uint32_t index : descendingIndices) {
        --last;
        position[index] = position[last];
        state[index] = state[last];
        if (tracksPacked()) {
            packed[index] = packed[last];
        }
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

size_t CompactBallStore::addCompact(const CompactBalls& balls) {
    const size_t first = count;
    const size_t added = std::min(balls.position.size(), balls.state.size());
    if (!resize(count + added)) {
        return 0;
    }
    std::copy_n(balls.position.begin(), added, position.begin() + static_cast<std::ptrdiff_t>(first));
    std::copy_n(balls.state.begin(), added, state.begin() + static_cast<std::ptrdiff_t>(first));
    for (size_t i = 0; tracksPacked() && i < added; ++i) {
        packed[first + i] = packCompactForRender(balls.position[i], balls.state[i]);
    }
    return added;
}

bool CompactBallStore::prepareGrid() {
    const size_t cells = gridCellCount();
    gridFilled = cells > 0 && count > cells;
    if (gridFilled) {
        packed = SimdVector<uint32_t>{};
        grid.assign(cells, emptyRenderSlot);
        bidderMask = bidderMaskFor(count, cells);
        ++frameSeed;
    }
    if (!gridFilled && !tracksPacked()) {
        packed.resize(roundUpToLanes(count), 0);
    }
    return gridFilled;
}

BallSnapshot CompactBallStore::snapshot() const {
    if (gridFilled) {
        return {grid.data(), grid.size(), fieldWidth, fieldHeight, false, true};
    }
    if (!tracksPacked()) {
        return {};
    }
    return {packed.data(), count, fieldWidth, fieldHeight};
}

void CompactBallStore::freeze(unsigned int epoch) {
    if (!frozen) {
        frozen = true;
        freeFlightEpoch = epoch;
    }
}

uint32_t CompactBallStore::packedInFlight(size_t index, unsigned int ticks) const {
    const BallSpawn ball = decode(index);
    const FreeFlightPosition flown = flyFreely(ball.x, ball.y, ball.vx, ball.vy, ticks, flightParams);
    return packBallForRender(flown.x, flown.y, ball.color, flightParams.packScaleX, flightParams.packScaleY);
}

namespace {

// The Android NDK's libc++ has no std::atomic_ref yet; its clang provides the same relaxed store as a builtin.
void storeRelaxed(uint32_t& slot, uint32_t value) {
#if defined(__cpp_lib_atomic_ref)
    std::atomic_ref<uint32_t>(slot).store(value, std::memory_order_relaxed);
#else
    __atomic_store_n(&slot, value, __ATOMIC_RELAXED);
#endif
}

} // namespace

void CompactBallStore::placeInGrid(uint32_t value) {
    constexpr float coordinateSlots = static_cast<float>(renderCoordinateMax + 1);
    const int column = std::min(static_cast<int>(static_cast<float>(value & renderCoordinateMax) *
                                                 static_cast<float>(gridWidth) / coordinateSlots),
                                gridWidth - 1);
    const int row = std::min(static_cast<int>(static_cast<float>((value >> renderCoordinateBits) & renderCoordinateMax) *
                                              static_cast<float>(gridHeight) / coordinateSlots),
                             gridHeight - 1);
    storeRelaxed(grid[static_cast<size_t>(row) * static_cast<size_t>(gridWidth) + static_cast<size_t>(column)], value);
}

// Only the bidders are needed to draw a frame, so whole chunks are sampled with the bidder stride: the cost
// follows the visibility grid, not the ball count.
size_t CompactBallStore::sampledChunkCount() const {
    const size_t chunks = (count + freeFlightChunkBalls - 1) / freeFlightChunkBalls;
    const size_t stride = static_cast<size_t>(bidderMask) + 1;
    const size_t offset = frameSeed % stride;
    return chunks > offset ? (chunks - offset + stride - 1) / stride : 0;
}

// Only the bidders are needed to draw a frame, so whole chunks are sampled with the bidder stride: the cost
// follows the visibility grid, not the ball count. Samples [first, end) can run on any thread.
void CompactBallStore::renderSampledChunks(size_t first, size_t end) {
    const unsigned int ticks = clockTick - freeFlightEpoch;
    const size_t stride = static_cast<size_t>(bidderMask) + 1;
    const size_t offset = frameSeed % stride;
    for (size_t sample = first; sample < end; ++sample) {
        const size_t chunk = offset + sample * stride;
        const size_t last = std::min(count, (chunk + 1) * freeFlightChunkBalls);
        for (size_t i = chunk * freeFlightChunkBalls; i < last; ++i) {
            placeInGrid(packedInFlight(i, ticks));
        }
    }
}

void CompactBallStore::materializeRange(size_t begin, size_t end) {
    const unsigned int ticks = clockTick - freeFlightEpoch;
    for (size_t i = begin; i < end; ++i) {
        const BallSpawn ball = decode(i);
        const FreeFlightPosition flown = flyFreely(ball.x, ball.y, ball.vx, ball.vy, ticks, flightParams);
        const uint32_t bits = state[i];
        int directionX = signedComponent(bits & compact::directionMask);
        int directionY = signedComponent((bits >> compact::directionBits) & compact::directionMask);
        directionX = (flown.vx < 0.0f) == (directionX < 0) ? directionX : -directionX;
        directionY = (flown.vy < 0.0f) == (directionY < 0) ? directionY : -directionY;
        const uint32_t bounces = std::min<uint32_t>(((bits >> compact::bounceShift) & compact::bounceMask) +
                                                        std::min(flown.wallCrossings, 3u),
                                                    compact::savedBounceLimit);
        position[i] = encodeAxis(flown.x, fieldWidth) | (encodeAxis(flown.y, fieldHeight) << 16);
        state[i] = (static_cast<uint32_t>(directionX) & compact::directionMask) |
                   ((static_cast<uint32_t>(directionY) & compact::directionMask) << compact::directionBits) |
                   (bits & (compact::colorMask << compact::colorShift)) | (bounces << compact::bounceShift);
    }
}
