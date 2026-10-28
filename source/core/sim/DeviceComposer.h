#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <cstdint>
#include <string>
#include <vector>

struct DeviceComposerTuning {
    // Below this many balls one part is faster than any split: the others would only add their launch overhead.
    size_t minimumComposedBalls = 262144;
    size_t minimumBallsToMeasure = 65536;
    size_t minimumRebalanceBalls = 131072;
};

// The Hybrid backend: one backend per chosen physical device (the CPU, each GPU), all stepping at the same time on
// their own share of the balls. Balls never collide with each other, so the set splits freely; each part's share
// follows its measured cost per ball, and balls move between parts as the shares settle.
//
// Ball indices: the parts' balls are numbered one after another in part order, device-resolving parts first, so
// host-reported hits and removals map to a part by offset.
//
// Bricks: a device-resolving part (CUDA, Vulkan) consumes brick hits on the device against its own copy of the hit
// counts. With two or more such parts each brick's remaining hits are split between them every tick as a budget
// (proportional to their ball counts), so the parts together never consume more hits than the brick has; the
// composer adds up what each consumed and reports the true counts. Host-reporting parts (CPU) hand their hits to
// Simulation, which accepts them against what the devices left.
class DeviceComposer final : public PhysicsBackend {
public:
    struct PartSpec {
        BackendHandle backend;
        std::string label;
        // Starting guess at relative throughput, until timings replace it (a GPU 1, the CPU about 0.1).
        double initialWeight = 1.0;
        // Balls the device's memory holds, for a backend that reports no limit of its own (Vulkan); 0 trusts
        // ballCapacityLimit. Without it one unlimited part makes the whole composer look unlimited.
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
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    std::vector<BallSpawn> takeTail(size_t count) override;
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
    double partShare(size_t part) const { return parts[part].share; }
    bool partOnHost(size_t part) const { return !parts[part].resolvesOnDevice; }
    // Host time this step spent splitting and merging brick hit budgets (0 with fewer than two device parts).
    double brickBudgetMs() const { return lastBudgetMs; }
    // Mean of brickBudgetMs over the steps that resolved hits on two or more device parts.
    // Balls moved between parts so far, and the steps that moved any.
    size_t movedBallCount() const { return ballsMoved; }
    size_t movingStepCount() const { return movingSteps; }
    double averageBrickBudgetMs() const { return budgetSteps > 0 ? budgetMsTotal / static_cast<double>(budgetSteps) : 0.0; }

private:
    struct Part {
        BackendHandle backend;
        std::string label;
        size_t memoryCapacity = 0;
        bool resolvesOnDevice = false;
        double share = 0.0;
        double lastMs = 0.0;
        size_t steppedBalls = 0;
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
    size_t gridCellCount() const { return static_cast<size_t>(stepGridWidth) * static_cast<size_t>(stepGridHeight); }

    Tuning settings;
    std::vector<Part> parts;
    size_t deviceParts = 0;
    int shareMeasurements = 0;
    // Brick budgets, used when two or more parts resolve hits on the device.
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
