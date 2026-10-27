#include "vulkan/VulkanBackend.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <format>
#include <mutex>
#include <string>
#include <string_view>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

constexpr std::string_view vulkanName = "Vulkan";
constexpr const char* unknownKind = "unknown backend";

std::mutex& deviceMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string& computePreference() {
    static std::string preference = "auto";
    return preference;
}

std::string& attemptedPreference() {
    static std::string preference;
    return preference;
}

std::shared_ptr<VulkanComputeDevice>& cachedDevice() {
    static std::shared_ptr<VulkanComputeDevice> device;
    return device;
}

std::string& cachedError() {
    static std::string error;
    return error;
}

std::shared_ptr<VulkanComputeDevice> currentDevice() {
    std::lock_guard lock(deviceMutex());
    if (attemptedPreference() != computePreference()) {
        if (cachedDevice() && cachedDevice().use_count() > 1) {
            // NVRHI's static Vulkan build shares Vulkan-Hpp dispatch state inside this plugin.
            cachedError() = std::format(
                "cannot change compute GPU preference while a Vulkan backend is active; requested '{}'",
                computePreference());
            return {};
        }
        cachedDevice().reset();
        cachedError().clear();
        attemptedPreference() = computePreference();
    }
    if (cachedDevice()) {
        cachedError().clear();
    }
    if (!cachedDevice() && cachedError().empty()) {
        cachedDevice() = VulkanComputeDevice::create(computePreference(), cachedError());
    }
    return cachedDevice();
}

std::string failureReason() {
    currentDevice();
    std::lock_guard lock(deviceMutex());
    return cachedError();
}

void releaseIdleDevice() {
    std::lock_guard lock(deviceMutex());
    if (cachedDevice() && cachedDevice().use_count() == 1) {
        cachedDevice().reset();
    }
}

}

BALLS_PLUGIN_EXPORT int ballsPhysicsInterfaceVersion() { return physicsPluginInterfaceVersion; }

BALLS_PLUGIN_EXPORT void ballsSetComputeGpu(const char* preference) {
    std::lock_guard lock(deviceMutex());
    computePreference() = preference && preference[0] ? preference : "auto";
}

BALLS_PLUGIN_EXPORT void ballsReleaseIdleDevice() { releaseIdleDevice(); }

BALLS_PLUGIN_EXPORT const char* ballsBackendUnavailableReason(const char* kindName) {
    static std::string reason;
    if (std::string_view(kindName ? kindName : "") != vulkanName) {
        reason = unknownKind;
    } else {
        reason = failureReason();
    }
    return reason.c_str();
}

BALLS_PLUGIN_EXPORT const char* ballsBackendDetail(const char* kindName) {
    static std::string detail;
    if (std::string_view(kindName ? kindName : "") != vulkanName) {
        detail.clear();
        return detail.c_str();
    }
    if (const auto device = currentDevice()) {
        detail = device->adapter().summary() + " (profile " + std::string(device->tuning().variant) + ")";
    } else {
        detail.clear();
    }
    return detail.c_str();
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackend(const char* kindName) {
    if (std::string_view(kindName ? kindName : "") != vulkanName) {
        return nullptr;
    }
    const auto device = currentDevice();
    if (!device) {
        return nullptr;
    }
    return new VulkanBackend(device);
}

BALLS_PLUGIN_EXPORT void ballsDestroyBackend(PhysicsBackend* backend) {
    delete backend;
    releaseIdleDevice();
}

BALLS_PLUGIN_EXPORT size_t ballsStreamedBytesPerBall(bool gridActive) {
    constexpr size_t readBytes = 2 * sizeof(uint32_t);
    constexpr size_t positionWriteBytes = sizeof(uint32_t);
    constexpr size_t packedWriteBytes = sizeof(uint32_t);
    return readBytes + positionWriteBytes + (gridActive ? 0 : packedWriteBytes);
}

