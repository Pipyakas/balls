#pragma once

#include "cpu/CpuBackendBase.h"
#include "cpu/CpuBallStore.h"

class ScalarBackend final : public CpuBackendBase<CpuBallStore> {
public:
    std::string_view name() const override { return "Scalar"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
};
