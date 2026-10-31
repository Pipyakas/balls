#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <vector>

class HybridBackend final : public PhysicsBackend {
public:
    HybridBackend(BackendHandle gpu, BackendHandle cpu);

    std::string_view name() const override { return "Hybrid"; }
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t> descendingIndices) override;
    size_t ballCount() const override { return gpu->ballCount() + cpu->ballCount(); }
    size_t ballCapacityLimit() const override;
    size_t ballHardLimit() const override;
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    std::vector<BallSpawn> takeTail(size_t count) override;
    void clear() override;
    BallTally truncate(size_t newCount) override;
    std::string_view faultMessage() const override;
    bool resolvesHitsOnDevice() const override { return true; }
    // Both halves take changed cells, target and hit-count changes as lists. Without this the pair was sent every brick's
    // hit count each tick its CPU half hit something (16.8M counts, 20-50 ms at 4096x4096) and the whole target field
    // after every rebuild (590 MB, 160 ms).
    bool acceptsSparseBoardUpdates() const override {
        return gpu->acceptsSparseBoardUpdates() && cpu->acceptsSparseBoardUpdates();
    }
    DeviceHitResults deviceHitResults() const override { return gpu->deviceHitResults(); }
    void setVisibilityGrid(int width, int height) override;
    double deviceStepMs() const override { return gpu->deviceStepMs(); }

    size_t cpuBallCount() const { return cpu->ballCount(); }
    double cpuStepMs() const { return lastCpuMs; }
    double gpuStepMs() const { return lastGpuMs; }

    static BackendHandle create(BackendHandle gpu, BackendHandle cpu);

private:
    size_t targetCpuCount(size_t total) const;
    void rebalance();
    void updateShare(double cpuMs, size_t cpuCount, double gpuMs, size_t gpuCount);
    void overlay(const BallSnapshot& balls, uint32_t replaceBelow);
    size_t gridCellCount() const { return static_cast<size_t>(stepGridWidth) * static_cast<size_t>(stepGridHeight); }

    BackendHandle gpu;
    BackendHandle cpu;
    std::vector<BallHit> cpuHits;
    std::vector<BallHit> unusedHits;
    std::vector<uint32_t> merged;
    CompactBalls movedBalls;
    double measuredCpuShare;
    int shareMeasurements = 0;
    double lastCpuMs = 0.0;
    double lastGpuMs = 0.0;
    int gridWidth = 0;
    int gridHeight = 0;
    int stepGridWidth = 0;
    int stepGridHeight = 0;
};
