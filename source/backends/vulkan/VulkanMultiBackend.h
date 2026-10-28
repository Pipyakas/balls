#pragma once

#include "sim/DeviceComposer.h"
#include "sim/PhysicsBackend.h"
#include "vulkan/VulkanComputeDevice.h"

#include <memory>
#include <string>
#include <vector>

// One Vulkan backend spanning several GPUs. Reuses DeviceComposer's coordination (rebalancing, brick budgets,
// snapshot merge, compaction, visibility grid) but drives every device from a single submitting thread: record and
// submit each device's step, then wait on all of them.
class VulkanMultiBackend final : public DeviceComposer {
public:
    explicit VulkanMultiBackend(std::vector<std::shared_ptr<VulkanComputeDevice>> devices);
    ~VulkanMultiBackend() override = default;

    std::string_view name() const override { return "VulkanMulti"; }
    void step(const StepParams& params, std::vector<BallHit>& hits) override;

private:
    static std::vector<PartSpec> makeParts(const std::vector<std::shared_ptr<VulkanComputeDevice>>& devices);
};
