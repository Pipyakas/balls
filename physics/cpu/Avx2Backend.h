#pragma once

#include "cpu/CpuBackendBase.h"
#include "cpu/WorkerPool.h"

#include <memory>

class Avx2Backend final : public CpuBackendBase {
public:
    explicit Avx2Backend(size_t threadCount);

    std::string_view name() const override { return "AVX2"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;

    static bool isSupportedByCpu();

private:
    void stepRange(const StepParams& params, size_t begin, size_t end, std::vector<BallHit>& hits);

    std::unique_ptr<WorkerPool> pool;
    std::vector<std::vector<BallHit>> chunkHits;
};
