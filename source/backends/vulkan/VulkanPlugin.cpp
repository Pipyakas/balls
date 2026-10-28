#include "vulkan/VulkanBackend.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

constexpr std::string_view vulkanName = "Vulkan";
constexpr const char* unknownKind = "unknown backend";

bool isVulkanKind(const char* kindName) { return std::string_view(kindName ? kindName : "") == vulkanName; }

// Every device the plugin opened, by its index in computeAdapters(), plus the shared instance. A device is kept
// while a backend uses it and released by ballsReleaseIdleDevice once nothing does.
struct DeviceCache {
    std::mutex mutex;
    std::string preference = "auto";
    std::shared_ptr<VulkanInstance> instance;
    std::string instanceError;
    std::map<int, std::shared_ptr<VulkanComputeDevice>> devices;
    std::map<int, std::string> errors;
};

// Never destroyed: at process exit the GPU drivers may already be torn down when static destructors run, and
// destroying a device then crashes. Idle devices are released during the run by ballsReleaseIdleDevice.
DeviceCache& cache() {
    static DeviceCache* shared = new DeviceCache();
    return *shared;
}

// The adapters the plugin offers, in Vulkan enumeration order: everything with a compute queue that is not a
// software rasterizer.
const std::vector<GpuAdapterInfo>& computeAdapters() {
    static const std::vector<GpuAdapterInfo> adapters = [] {
        std::vector<GpuAdapterInfo> usable;
        for (const GpuAdapterInfo& adapter : enumerateGpuAdapters(GpuApi::Vulkan)) {
            if (adapter.usableForCompute()) {
                usable.push_back(adapter);
            }
        }
        return usable;
    }();
    return adapters;
}

int defaultDevice(std::string_view preference) {
    const std::vector<GpuAdapterInfo> all = enumerateGpuAdapters(GpuApi::Vulkan);
    const int selected = selectGpuAdapter(all, preference, GpuRole::Compute);
    if (selected < 0) {
        return -1;
    }
    const std::vector<GpuAdapterInfo>& usable = computeAdapters();
    const auto match = std::ranges::find_if(usable, [&](const GpuAdapterInfo& adapter) {
        return adapter.index == all[static_cast<size_t>(selected)].index;
    });
    return match == usable.end() ? -1 : static_cast<int>(match - usable.begin());
}

// Opens (or returns the open) device; on failure fills error. Caller holds the cache lock.
std::shared_ptr<VulkanComputeDevice> openDevice(DeviceCache& state, int index, std::string& error) {
    const std::vector<GpuAdapterInfo>& adapters = computeAdapters();
    if (adapters.empty()) {
        error = "no usable Vulkan compute adapter was found";
        return {};
    }
    if (index < 0 || index >= static_cast<int>(adapters.size())) {
        error = std::format("compute adapter preference '{}' did not match a usable Vulkan adapter", state.preference);
        return {};
    }
    if (const auto found = state.devices.find(index); found != state.devices.end()) {
        return found->second;
    }
    if (const auto failed = state.errors.find(index); failed != state.errors.end()) {
        error = failed->second;
        return {};
    }
    if (!state.instance && state.instanceError.empty()) {
        state.instance = VulkanInstance::create(state.instanceError);
    }
    if (!state.instance) {
        error = state.instanceError;
        return {};
    }
    auto device = VulkanComputeDevice::create(state.instance, adapters[static_cast<size_t>(index)], error);
    if (!device) {
        state.errors[index] = error;
        return {};
    }
    state.devices[index] = device;
    return device;
}

std::shared_ptr<VulkanComputeDevice> deviceAt(int index, std::string& error) {
    DeviceCache& state = cache();
    std::lock_guard lock(state.mutex);
    return openDevice(state, index, error);
}

int currentDefaultDevice() {
    DeviceCache& state = cache();
    std::lock_guard lock(state.mutex);
    return defaultDevice(state.preference);
}

void releaseIdleDevices() {
    DeviceCache& state = cache();
    std::lock_guard lock(state.mutex);
    std::erase_if(state.devices, [](const auto& entry) { return entry.second.use_count() == 1; });
    if (state.devices.empty()) {
        state.instance.reset();
    }
}

std::string deviceDetail(int index) {
    std::string error;
    const auto device = deviceAt(index, error);
    return device ? device->adapter().summary() + " (profile " + std::string(device->tuning().variant) + ")"
                  : std::string{};
}

std::string deviceFailure(int index) {
    std::string error;
    deviceAt(index, error);
    return error;
}

}

BALLS_PLUGIN_EXPORT int ballsPhysicsInterfaceVersion() { return physicsPluginInterfaceVersion; }

BALLS_PLUGIN_EXPORT void ballsSetComputeGpu(const char* preference) {
    DeviceCache& state = cache();
    std::lock_guard lock(state.mutex);
    state.preference = preference && preference[0] ? preference : "auto";
}

BALLS_PLUGIN_EXPORT void ballsReleaseIdleDevice() { releaseIdleDevices(); }

BALLS_PLUGIN_EXPORT const char* ballsBackendUnavailableReason(const char* kindName) {
    static std::string reason;
    reason = isVulkanKind(kindName) ? deviceFailure(currentDefaultDevice()) : std::string(unknownKind);
    return reason.c_str();
}

BALLS_PLUGIN_EXPORT const char* ballsBackendDetail(const char* kindName) {
    static std::string detail;
    detail = isVulkanKind(kindName) ? deviceDetail(currentDefaultDevice()) : std::string{};
    return detail.c_str();
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackendOnDevice(const char* kindName, int device) {
    if (!isVulkanKind(kindName)) {
        return nullptr;
    }
    std::string error;
    auto computeDevice = deviceAt(device, error);
    return computeDevice ? new VulkanBackend(std::move(computeDevice)) : nullptr;
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackend(const char* kindName) {
    return ballsCreateBackendOnDevice(kindName, currentDefaultDevice());
}

BALLS_PLUGIN_EXPORT void ballsDestroyBackend(PhysicsBackend* backend) {
    delete backend;
    releaseIdleDevices();
}

BALLS_PLUGIN_EXPORT int ballsDeviceCount(const char* kindName) {
    return isVulkanKind(kindName) ? static_cast<int>(computeAdapters().size()) : 0;
}

BALLS_PLUGIN_EXPORT int ballsDeviceInfo(const char* kindName, int device, PhysicsDeviceInfo* info) {
    if (!info || device < 0 || device >= ballsDeviceCount(kindName)) {
        return 0;
    }
    const GpuAdapterInfo& adapter = computeAdapters()[static_cast<size_t>(device)];
    *info = PhysicsDeviceInfo{};
    std::memcpy(info->name, adapter.name.data(), std::min(adapter.name.size(), sizeof(info->name) - 1));
    std::copy(adapter.uuid.begin(), adapter.uuid.end(), info->uuid);
    info->hasUuid = adapter.hasUuid ? 1 : 0;
    info->type = adapter.kind == GpuKind::Discrete     ? PhysicsDeviceType::DiscreteGpu
                 : adapter.kind == GpuKind::Integrated ? PhysicsDeviceType::IntegratedGpu
                                                       : PhysicsDeviceType::OtherGpu;
    info->pciDomain = adapter.pciDomain;
    info->pciBus = adapter.pciBus;
    info->pciDevice = adapter.pciDevice;
    info->memoryBytes = adapter.deviceLocalBytes;
    return 1;
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceUnavailableReason(const char* kindName, int device) {
    static std::string reason;
    reason = isVulkanKind(kindName) ? deviceFailure(device) : std::string(unknownKind);
    return reason.c_str();
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceDetail(const char* kindName, int device) {
    static std::string detail;
    detail = isVulkanKind(kindName) ? deviceDetail(device) : std::string{};
    return detail.c_str();
}

BALLS_PLUGIN_EXPORT size_t ballsStreamedBytesPerBall(bool gridActive) {
    constexpr size_t readBytes = 2 * sizeof(uint32_t);
    constexpr size_t positionWriteBytes = sizeof(uint32_t);
    constexpr size_t packedWriteBytes = sizeof(uint32_t);
    return readBytes + positionWriteBytes + (gridActive ? 0 : packedWriteBytes);
}
