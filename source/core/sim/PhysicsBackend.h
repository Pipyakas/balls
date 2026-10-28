#pragma once

#include "common/BallCount.h"
#include "sim/kernels/BallStepCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define BALLS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define BALLS_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

inline constexpr int physicsPluginInterfaceVersion = 3;

struct BallSpawn {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    int32_t color = 0;
};

struct BallHit {
    uint32_t ball = 0;
    int32_t brick = 0;
};

struct CompactBalls {
    std::vector<uint32_t> position;
    std::vector<uint32_t> state;
};

inline constexpr unsigned int compactColorShift = 26u;
inline constexpr unsigned int compactColorMask = 7u;

inline uint32_t packCompactForRender(uint32_t position, uint32_t state) {
    constexpr uint32_t axisMask = 0xFFFFu;
    constexpr uint32_t positionToRenderShift = 2u;
    return ((position & axisMask) >> positionToRenderShift) |
           ((position >> 16 >> positionToRenderShift) << renderCoordinateBits) |
           (((state >> compactColorShift) & compactColorMask) << renderColorShift);
}

struct BallSnapshot {
    const uint32_t* packed = nullptr;
    size_t count = 0;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    bool residentInRenderBuffer = false;
    bool visibilityGrid = false;
};

struct BoardUpload {
    int columns = 0;
    int rows = 0;
    std::span<const int32_t> paddedCells;
    std::span<const int32_t> brickColors;
    std::span<const int32_t> targetCells;
    std::span<const int32_t> nearBrickCells;
    std::span<const int32_t> brickHitsLeft;
    std::span<const int32_t> changedCells;
    bool onlyChangedCells = false;
    // (brick, hits left) pairs for the bricks the host hit, sent instead of brickHitsLeft to backends that accept them.
    std::span<const int32_t> brickHitUpdates;
};

struct GpuRoofline {
    double theoreticalGigabytesPerSecond = 0.0;
    double copyGigabytesPerSecond = 0.0;
    int multiprocessors = 0;
    double coreClockMegahertz = 0.0;
    double memoryClockMegahertz = 0.0;
    int memoryBusBits = 0;
};

struct DeviceHitResults {
    std::span<const int32_t> brickHitsLeft;
    std::span<const uint32_t> hitBricks;
    BallTally killsByColor{};
    BallCount killed = 0;
};

// The visibility grid has one slot per this many ball radii: front ends size it this way, and the machine
// benchmark sizes it the same, so balls fly freely there exactly as they do in a game.
inline constexpr float visibilitySlotsPerRadius = 1.6f;
// GPU backends record each step's kills in a buffer sized for the most a step can make. Bricks with huge hit counts
// would ask for gigabytes, so the buffer stops here; a kill past it is refunded and the ball bounces, to land a later
// step.
inline constexpr size_t maximumKillsPerStep = size_t{1} << 22;

// A slot is never smaller than a pixel: a board bigger than the screen draws at most one ball per pixel.
inline float visibilitySlotSize(float radiusCells, float cellPixels) {
    return std::max(radiusCells * visibilitySlotsPerRadius, cellPixels > 0.0f ? 1.0f / cellPixels : 0.0f);
}

inline int visibilitySlots(float fieldExtent, float slotSize) {
    return static_cast<int>(std::ceil(fieldExtent / (slotSize > 0.0f ? slotSize : 1.0f)));
}

inline float renderPackScale(float fieldExtent) {
    return static_cast<float>(renderCoordinateMax) / (fieldExtent > 0.0f ? fieldExtent : 1.0f);
}

class PhysicsBackend {
public:
    virtual ~PhysicsBackend() = default;

    virtual std::string_view name() const = 0;
    virtual void uploadBoard(const BoardUpload& board) = 0;
    virtual void addBalls(std::span<const BallSpawn> spawns) = 0;
    virtual void step(const StepParams& params, std::vector<BallHit>& hits) = 0;
    virtual void removeBalls(std::span<const uint32_t> descendingIndices) = 0;
    virtual size_t ballCount() const = 0;
    // How many balls fit in this backend's memory; without free flight the frame rate runs out long before.
    virtual size_t ballCapacityLimit() const { return std::numeric_limits<size_t>::max(); }
    virtual BallSnapshot snapshot() = 0;
    virtual std::vector<BallSpawn> exportBalls() = 0;
    virtual std::vector<BallSpawn> takeTail(size_t count) = 0;
    virtual void clear() = 0;
    virtual BallTally truncate(size_t newCount) = 0;
    virtual std::string_view faultMessage() const { return {}; }
    virtual bool resolvesHitsOnDevice() const { return false; }
    virtual DeviceHitResults deviceHitResults() const { return {}; }
    virtual bool shareRenderBuffer(unsigned int, size_t) { return false; }
    virtual void setVisibilityGrid(int, int) {}
    // Optional ball-vs-ball collisions, a testing aid outside the game rules. Backends that do not
    // implement them keep the default and ignore setBallCollisions.
    virtual bool supportsBallCollisions() const { return false; }
    virtual void setBallCollisions(bool) {}
    virtual double deviceStepMs() const { return 0.0; }
    virtual void shareCoresWithGpu() {}
    virtual bool takeCompactTail(size_t, CompactBalls&) { return false; }
    virtual bool addCompact(const CompactBalls&) { return false; }
    // The share of the visibility grid this backend's balls have to fill. Hybrid draws the CPU's balls over the GPU's
    // full grid, so the CPU side samples only in proportion to the balls it holds.
    virtual void setRenderSampleShare(double) {}
    virtual bool acceptsBrickHitUpdates() const { return false; }
};

using CreateBackendFunction = PhysicsBackend* (*)(const char* kindName);
using DestroyBackendFunction = void (*)(PhysicsBackend* backend);
using BackendTextFunction = const char* (*)(const char* kindName);
using SetComputeGpuFunction = void (*)(const char* preference);
using InterfaceVersionFunction = int (*)();
using MeasureRooflineFunction = GpuRoofline (*)(size_t bufferBytes);
using StreamedBytesFunction = size_t (*)(bool gridActive);
