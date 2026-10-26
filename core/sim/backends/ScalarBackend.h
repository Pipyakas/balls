#pragma once

#include "sim/backends/CpuBackendBase.h"

class ScalarBackend final : public CpuBackendBase {
public:
    std::string_view name() const override { return "Scalar"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
};
