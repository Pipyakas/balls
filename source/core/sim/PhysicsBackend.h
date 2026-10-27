#pragma once

#include "common/BallCount.h"
#include "sim/kernels/BallStepCore.h"

#include <cstddef>
#include <cstdint>
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
};

using CreateBackendFunction = PhysicsBackend* (*)(const char* kindName);
using DestroyBackendFunction = void (*)(PhysicsBackend* backend);
using BackendTextFunction = const char* (*)(const char* kindName);
using InterfaceVersionFunction = int (*)();
using MeasureRooflineFunction = GpuRoofline (*)(size_t bufferBytes);
using StreamedBytesFunction = size_t (*)(bool gridActive);
