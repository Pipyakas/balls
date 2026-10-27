#pragma once

#include "cpu/SimdVector.h"
#include "sim/PhysicsBackend.h"

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
    SimdVector<uint32_t> position;
    SimdVector<uint32_t> state;
    SimdVector<uint32_t> packed;
    std::vector<uint32_t> grid;
    int gridWidth = 0;
    int gridHeight = 0;
    bool gridFilled = false;
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

    void add(std::span<const BallSpawn> spawns);
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
    void addCompact(const CompactBalls& balls);
    size_t gridCellCount() const { return static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight); }
    bool prepareGrid();
    void freeze(unsigned int epoch);
    void renderFreeFlight();
    void materializeRange(size_t begin, size_t end);

private:
    void resize(size_t newCount);
    BallSpawn decode(size_t index) const;
    uint32_t packedInFlight(size_t index, unsigned int ticks) const;
    void placeInGrid(uint32_t packed);
};
