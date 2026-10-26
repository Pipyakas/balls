#pragma once

#include "cpu/CompactBallStore.h"
#include "cpu/CpuBackendBase.h"
#include "cpu/WorkerPool.h"

#include <memory>

class Avx2Backend final : public CpuBackendBase<CompactBallStore> {
public:
    explicit Avx2Backend(size_t threadCount);

    std::string_view name() const override { return "AVX2"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void setVisibilityGrid(int width, int height) override;
    void shareCoresWithGpu() override;
    bool takeCompactTail(size_t count, CompactBalls& balls) override {
        store.takeCompactTail(count, balls);
        return true;
    }
    bool addCompact(const CompactBalls& balls) override {
        store.addCompact(balls);
        return true;
    }

    static bool isSupportedByCpu();

private:
    void stepRange(const StepParams& params, size_t begin, size_t end, std::vector<BallHit>& hits);

    std::unique_ptr<WorkerPool> pool;
    std::vector<std::vector<BallHit>> chunkHits;
};
