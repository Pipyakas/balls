#pragma once

#include "cpu/ReservedArray.h"
#include "cpu/SimdVector.h"
#include "sim/PhysicsBackend.h"

#include <functional>

namespace compact {

inline constexpr float positionScale = 65535.0f;
inline constexpr uint32_t positionMask = 0xFFFFu;
inline constexpr int directionBits = 13;
inline constexpr uint32_t directionMask = (1u << directionBits) - 1u;
inline constexpr float directionScale = 4095.0f;
inline constexpr uint32_t colorShift = 26u;
inline constexpr uint32_t colorMask = 7u;
inline constexpr uint32_t bounceShift = 29u;
inline constexpr uint32_t bounceMask = 3u;
inline constexpr int savedBounceLimit = 3;

}

struct CompactBallStore {
    ReservedArray<uint32_t> position;
    ReservedArray<uint32_t> state;
    // One render word per ball, kept only while there are fewer balls than visibility-grid slots; above that the
    // grid is the render output and this array is released, so a held ball costs 8 bytes.
    SimdVector<uint32_t> packed;
    std::vector<uint32_t> grid;
    int gridWidth = 0;
    int gridHeight = 0;
    bool gridFilled = false;
    double renderSampleShare = 1.0;
    bool drawing = true;
    uint32_t bidderMask = 0;
    uint32_t frameSeed = 0;
    size_t count = 0;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    float speed = 1.0f;
    // Free flight (see flyFreely): while frozen, every stored ball is its state at freeFlightEpoch.
    bool frozen = false;
    unsigned int freeFlightEpoch = 0;
    unsigned int clockTick = 0;
    StepParams flightParams{};

    // Calls encode(begin, end) over [0, count), on as many threads as the caller has.
    using RangeRunner = std::function<void(size_t count, const std::function<void(size_t, size_t)>& encode)>;

    // Adds as many spawns as memory takes and returns how many that was.
    size_t add(std::span<const BallSpawn> spawns, const RangeRunner& runRanges = {});
    void swapRemove(std::span<const uint32_t> descendingIndices);
    void clear() {
        resize(0);
        frozen = false;
    }
    BallTally truncate(size_t newCount);
    BallSnapshot snapshot() const;
    std::vector<BallSpawn> exportBalls(size_t first = 0) const;
    std::vector<BallSpawn> takeTail(size_t tailCount);
    void takeCompactTail(size_t tailCount, CompactBalls& balls);
    size_t addCompact(const CompactBalls& balls);
    size_t gridCellCount() const { return static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight); }
    bool prepareGrid();
    void freeze(unsigned int epoch);
    static constexpr size_t freeFlightChunkBalls = 256;
    size_t sampledChunkCount() const;
    void renderSampledChunks(size_t first, size_t end);
    void renderRangeInFlight(size_t begin, size_t end);
    void materializeRange(size_t begin, size_t end);

private:
    bool resize(size_t newCount);
    bool tracksPacked() const { return packed.size() >= count; }
    BallSpawn decode(size_t index) const;
    uint32_t packedInFlight(size_t index, unsigned int ticks) const;
    void placeInGrid(uint32_t packed);
};
