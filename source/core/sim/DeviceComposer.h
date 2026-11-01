#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <cstdint>
#include <string>
#include <vector>

struct DeviceComposerTuning {

    size_t minimumComposedBalls = 262144;
    size_t minimumBallsToMeasure = 65536;
    size_t minimumRebalanceBalls = 131072;
};

class DeviceComposer : public PhysicsBackend {
public:
    struct PartSpec {
        BackendHandle backend;
        std::string label;

        double initialWeight = 1.0;

        size_t memoryCapacity = 0;
    };

    using Tuning = DeviceComposerTuning;

    explicit DeviceComposer(std::vector<PartSpec> parts, Tuning tuning = {});
    ~DeviceComposer() override;
    static BackendHandle create(std::vector<PartSpec> parts, Tuning tuning = {});

    std::string_view name() const override { return "Hybrid"; }
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t> descendingIndices) override;
    size_t ballCount() const override;
    size_t ballCapacityLimit() const override;
    size_t ballHardLimit() const override;
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    std::vector<BallSpawn> takeTail(size_t count) override;
    bool takeCompactTail(size_t count, CompactBalls& balls) override;
    bool addCompact(const CompactBalls& balls) override;
    void clear() override;
    BallTally truncate(size_t newCount) override;
    std::string_view faultMessage() const override;
    bool resolvesHitsOnDevice() const override { return deviceParts > 0; }
    DeviceHitResults deviceHitResults() const override;
    void setVisibilityGrid(int width, int height) override;
    double deviceStepMs() const override;
    bool acceptsSparseBoardUpdates() const override;
    void setDrawing(bool drawing) override;

    size_t partCount() const { return parts.size(); }
    const std::string& partLabel(size_t part) const { return parts[part].label; }
    size_t partBallCount(size_t part) const { return parts[part].backend->ballCount(); }
    double partStepMs(size_t part) const { return parts[part].lastMs; }
    const PhysicsBackend& partBackend(size_t part) const { return *parts[part].backend; }
    double partShare(size_t part) const { return parts[part].share; }
    bool partOnHost(size_t part) const { return !parts[part].resolvesOnDevice; }

    double brickBudgetMs() const { return lastBudgetMs; }

    size_t movedBallCount() const { return ballsMoved; }
    size_t movingStepCount() const { return movingSteps; }
    double averageBrickBudgetMs() const { return budgetSteps > 0 ? budgetMsTotal / static_cast<double>(budgetSteps) : 0.0; }

protected:
    struct Part {
        BackendHandle backend;
        std::string label;
        size_t memoryCapacity = 0;
        bool resolvesOnDevice = false;
        double share = 0.0;
        double lastMs = 0.0;
        size_t steppedBalls = 0;
        size_t windowBalls = 0;
        double windowMs = 0.0;
        std::vector<BallHit> hits;
        std::vector<int32_t> budget;
    };

    std::vector<size_t> targetCounts(size_t total) const;
    void rebalance();
    void updateShares();
    void splitBrickBudgets();
    void mergeDeviceHits();
    void overlay(const BallSnapshot& balls, uint32_t replaceBelow, uint32_t salt);
    void shareRenderSamples();
    size_t partCapacity(const Part& part) const;
    size_t partHardLimit(const Part& part) const;
    size_t gridCellCount() const { return static_cast<size_t>(stepGridWidth) * static_cast<size_t>(stepGridHeight); }

    Tuning settings;
    std::vector<Part> parts;
    size_t deviceParts = 0;
    int shareMeasurements = 0;
    int windowSteps = 0;

    std::vector<int32_t> brickHits;
    uint32_t budgetEpoch = 0;
    std::vector<int32_t> mergedHitsLeft;
    DeviceHitResults mergedHits;
    double lastBudgetMs = 0.0;
    double budgetMsTotal = 0.0;
    size_t ballsMoved = 0;
    size_t movingSteps = 0;
    int stepsSinceMove = 0;
    size_t budgetSteps = 0;
    std::vector<uint32_t> merged;
    CompactBalls movedBalls;
    int gridWidth = 0;
    int gridHeight = 0;
    int stepGridWidth = 0;
    int stepGridHeight = 0;
};
