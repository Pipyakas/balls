#pragma once

#include "common/BallCount.h"
#include "sim/BrickBoard.h"
#include "sim/TargetField.h"
#include "sim/kernels/BallStepCore.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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

struct BallSnapshot {
    const uint32_t* packed = nullptr;
    size_t count = 0;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    bool residentInRenderBuffer = false;
    bool visibilityGrid = false;
};

struct DeviceHitResults {
    std::span<const int32_t> brickHitsLeft;
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
    virtual void uploadBoard(const BrickBoard& board, const TargetField& field) = 0;
    virtual void addBalls(std::span<const BallSpawn> spawns) = 0;
    virtual void step(const StepParams& params, std::vector<BallHit>& hits) = 0;
    virtual void removeBalls(std::span<const uint32_t> descendingIndices) = 0;
    virtual size_t ballCount() const = 0;
    virtual BallSnapshot snapshot() = 0;
    virtual std::vector<BallSpawn> exportBalls() = 0;
    virtual void clear() = 0;
    virtual BallTally truncate(size_t newCount) = 0;
    virtual std::string_view faultMessage() const { return {}; }
    virtual bool resolvesHitsOnDevice() const { return false; }
    virtual DeviceHitResults deviceHitResults() const { return {}; }
    virtual bool shareRenderBuffer(unsigned int, size_t) { return false; }
    virtual void setVisibilityGrid(int, int) {}
};
