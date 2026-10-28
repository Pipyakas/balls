#pragma once

#include "cpu/CompactBallStore.h"
#include "cpu/CpuBackendBase.h"
#include "common/WorkerPool.h"

#include <memory>

struct FlightRenderLanes;

class Avx2Backend final : public CpuBackendBase<CompactBallStore> {
public:
    explicit Avx2Backend(size_t threadCount);

    std::string_view name() const override { return "AVX2"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void setVisibilityGrid(int width, int height) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void setRenderSampleShare(double share) override { store.renderSampleShare = share; }
    void setDrawing(bool drawing) override;
    void shareCoresWithGpu() override;
    bool takeCompactTail(size_t count, CompactBalls& balls) override {
        materializeFreeFlight();
        store.takeCompactTail(count, balls);
        return true;
    }
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

    static bool isSupportedByCpu();

private:
    void stepRange(const StepParams& params, size_t begin, size_t end, std::vector<BallHit>& hits);
    void materializeFreeFlight();

    static void renderSampledChunks(CompactBallStore& store, const FlightRenderLanes& lanes, size_t first,
                                    size_t end);
    std::unique_ptr<WorkerPool> pool;
    size_t stepChunkBalls;
    size_t renderSamplesPerTask;
    std::vector<std::vector<BallHit>> chunkHits;
};
