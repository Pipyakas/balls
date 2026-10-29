#pragma once

#include "cpu/CompactBallStore.h"
#include "cpu/CpuBackendBase.h"
#include "common/WorkerPool.h"

#include <memory>

class Zen4Backend final : public CpuBackendBase<CompactBallStore> {
public:
    explicit Zen4Backend(size_t threadCount);

    std::string_view name() const override { return "Zen4"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void setVisibilityGrid(int width, int height) override;
    void shareCoresWithGpu() override;
    bool takeCompactTail(size_t count, CompactBalls& balls) override {
        materializeFreeFlight();
        store.takeCompactTail(count, balls);
        return true;
    }
    void addBalls(std::span<const BallSpawn> spawns) override { store.add(spawns, pool.get()); }
    bool addCompact(const CompactBalls& balls) override {
        materializeFreeFlight();
        return store.addCompact(balls) == std::min(balls.position.size(), balls.state.size());
    }
    std::vector<BallSpawn> exportBalls() override {
        materializeFreeFlight();
        return store.exportBalls();
    }
    std::vector<BallSpawn> takeTail(size_t count) override {
        materializeFreeFlight();
        return store.takeTail(count);
    }
    size_t ballCapacityLimit() const override;
    size_t preallocateBalls() override;

    static bool isSupportedByCpu();

private:
    void stepRange(const StepParams& params, size_t begin, size_t end, std::vector<BallHit>& hits);
    void materializeFreeFlight();

    std::unique_ptr<WorkerPool> pool;
    std::vector<std::vector<BallHit>> chunkHits;
};
