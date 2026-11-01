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

inline constexpr int physicsPluginInterfaceVersion = 5;

enum class PhysicsDeviceType : int32_t { Cpu, DiscreteGpu, IntegratedGpu, OtherGpu, Npu };

inline constexpr size_t physicsDeviceNameBytes = 256;
inline constexpr size_t physicsDeviceUuidBytes = 16;

struct PhysicsDeviceInfo {
    char name[physicsDeviceNameBytes] = {};
    uint8_t uuid[physicsDeviceUuidBytes] = {};
    int32_t hasUuid = 0;
    PhysicsDeviceType type = PhysicsDeviceType::Cpu;
    int32_t pciDomain = -1;
    int32_t pciBus = -1;
    int32_t pciDevice = -1;
    uint64_t memoryBytes = 0;
};

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

    std::span<const int32_t> brickHitUpdates;
    std::span<const int32_t> targetUpdates;
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

inline constexpr float visibilitySlotsPerRadius = 1.0f;

inline constexpr size_t maximumKillsPerStep = size_t{1} << 22;

inline float visibilitySlotSize(float radiusCells, float cellPixels) {
    return std::max(radiusCells * visibilitySlotsPerRadius, cellPixels > 0.0f ? 1.0f / cellPixels : 0.0f);
}

inline constexpr float minimumBallRadiusPixels = 0.75f;

inline float referenceCellPixels(int columns, int rows) {
    constexpr float boardWidthPixels = 1920.0f - 2.0f * 16.0f - 2.0f * 310.0f - 2.0f * 8.0f - 2.0f * 10.0f;
    constexpr float boardHeightPixels = 1080.0f - 2.0f * 16.0f - 2.0f * 10.0f;
    return std::min(boardWidthPixels / static_cast<float>(std::max(columns, 1)),
                    boardHeightPixels / static_cast<float>(std::max(rows, 1)));
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

    virtual size_t ballCapacityLimit() const { return std::numeric_limits<size_t>::max(); }

    virtual size_t ballHardLimit() const { return ballCapacityLimit(); }
    virtual BallSnapshot snapshot() = 0;
    virtual std::vector<BallSpawn> exportBalls() = 0;
    virtual std::vector<BallSpawn> takeTail(size_t count) = 0;
    virtual void clear() = 0;
    virtual BallTally truncate(size_t newCount) = 0;
    virtual std::string_view faultMessage() const { return {}; }
    virtual bool resolvesHitsOnDevice() const { return false; }
    virtual DeviceHitResults deviceHitResults() const { return {}; }

    virtual void uploadBrickHits(std::span<const int32_t>) {}
    virtual bool shareRenderBuffer(unsigned int, size_t) { return false; }
    virtual void setVisibilityGrid(int, int) {}

    virtual bool supportsBallCollisions() const { return false; }
    virtual void setBallCollisions(bool) {}
    virtual double deviceStepMs() const { return 0.0; }
    virtual void shareCoresWithGpu() {}
    virtual bool takeCompactTail(size_t, CompactBalls&) { return false; }
    virtual bool addCompact(const CompactBalls&) { return false; }

    virtual void setRenderSampleShare(double) {}
    virtual bool acceptsSparseBoardUpdates() const { return false; }

    virtual void setDrawing(bool) {}

    virtual size_t preallocateBalls() { return 0; }
};

using CreateBackendFunction = PhysicsBackend* (*)(const char* kindName);
using DeviceCountFunction = int (*)(const char* kindName);
using DeviceInfoFunction = int (*)(const char* kindName, int device, PhysicsDeviceInfo* info);
using DeviceTextFunction = const char* (*)(const char* kindName, int device);
using CreateBackendOnDeviceFunction = PhysicsBackend* (*)(const char* kindName, int device);
using CreateBackendOnDevicesFunction = PhysicsBackend* (*)(const char* kindName, const int* devices, int count);
using DestroyBackendFunction = void (*)(PhysicsBackend* backend);
using BackendTextFunction = const char* (*)(const char* kindName);
using SetComputeGpuFunction = void (*)(const char* preference);
using InterfaceVersionFunction = int (*)();
using MeasureRooflineFunction = GpuRoofline (*)(size_t bufferBytes);
using StreamedBytesFunction = size_t (*)(bool gridActive);
