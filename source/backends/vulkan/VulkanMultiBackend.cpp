#include "vulkan/VulkanMultiBackend.h"

#include "sim/DeviceComposer.h"
#include "vulkan/VulkanBackend.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

VulkanMultiBackend::VulkanMultiBackend(std::vector<std::shared_ptr<VulkanComputeDevice>> devices)
    : DeviceComposer(makeParts(devices), DeviceComposerTuning{}) {}

std::vector<DeviceComposer::PartSpec> VulkanMultiBackend::makeParts(
    const std::vector<std::shared_ptr<VulkanComputeDevice>>& devices) {
    std::vector<DeviceComposer::PartSpec> parts;
    for (const auto& device : devices) {
        PhysicsBackend* backend = new VulkanBackend(device);
        BackendHandle handle(backend, BackendRelease{[](PhysicsBackend* p) { delete p; }, nullptr});
        const size_t memoryCapacity = device ? device->adapter().deviceLocalBytes / 16 : 0;
        parts.push_back({std::move(handle), device ? device->adapter().name : std::string("Vulkan"),
                         1.0, memoryCapacity});
    }
    return parts;
}

void VulkanMultiBackend::step(const StepParams& params, std::vector<BallHit>& hits) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point stepStart = Clock::now();

    if (!params.freeFlight) {
        rebalance();
    }
    stepGridWidth = gridWidth;
    stepGridHeight = gridHeight;
    lastBudgetMs = 0.0;
    if (deviceParts >= 2 && !brickHits.empty()) {
        const Clock::time_point start = Clock::now();
        splitBrickBudgets();
        lastBudgetMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    for (Part& part : parts) {
        part.hits.clear();
        part.steppedBalls = part.backend->ballCount();
        part.lastMs = 0.0;
    }

    // Submit every device's step before waiting on any, so the GPUs run together.
    for (Part& part : parts) {
        if (part.steppedBalls == 0) {
            continue;
        }
        if (auto* vulkan = dynamic_cast<VulkanBackend*>(part.backend.get())) {
            vulkan->beginStep(params);
        } else {
            part.backend->step(params, part.hits);
        }
    }
    for (Part& part : parts) {
        if (part.steppedBalls == 0) {
            continue;
        }
        if (auto* vulkan = dynamic_cast<VulkanBackend*>(part.backend.get())) {
            vulkan->finishStep(part.hits);
            part.lastMs = vulkan->deviceStepMs();
        }
    }

    if (!params.freeFlight) {
        updateShares();
        const double wallSoFar = std::chrono::duration<double, std::milli>(Clock::now() - stepStart).count();
        parkLosingParts(wallSoFar);
    }
    if (deviceParts >= 2) {
        const Clock::time_point start = Clock::now();
        mergeDeviceHits();
        lastBudgetMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        budgetMsTotal += lastBudgetMs;
        ++budgetSteps;
    }

    uint32_t offset = 0;
    for (Part& part : parts) {
        for (const BallHit& hit : part.hits) {
            hits.push_back({hit.ball + offset, hit.brick});
        }
        offset += static_cast<uint32_t>(part.backend->ballCount());
    }
}
