#pragma once

#include "sim/DeviceComposer.h"
#include "sim/PhysicsBackend.h"
#include "vulkan/VulkanComputeDevice.h"

#include <memory>
#include <string>
#include <vector>

class VulkanMultiBackend final : public DeviceComposer {
public:
    explicit VulkanMultiBackend(std::vector<std::shared_ptr<VulkanComputeDevice>> devices);
    ~VulkanMultiBackend() override = default;

    std::string_view name() const override { return "VulkanMulti"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;

private:
    static std::vector<PartSpec> makeParts(const std::vector<std::shared_ptr<VulkanComputeDevice>>& devices);
};
