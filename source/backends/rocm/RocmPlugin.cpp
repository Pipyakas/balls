#include "rocm/RocmBackend.h"
#include "rocm/RocmDriver.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view rocmName = "ROCm";
constexpr const char* unknownKind = "unknown backend";
}

BALLS_PLUGIN_EXPORT int ballsPhysicsInterfaceVersion() { return physicsPluginInterfaceVersion; }

BALLS_PLUGIN_EXPORT const char* ballsBackendUnavailableReason(const char* kindName) {
    static std::string reason;
    reason = std::string_view(kindName) == rocmName ? RocmBackend::unavailableReason() : std::string(unknownKind);
    return reason.c_str();
}

BALLS_PLUGIN_EXPORT const char* ballsBackendDetail(const char*) {
    return RocmDriver::instance().deviceName().c_str();
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackend(const char* kindName) {
    if (std::string_view(kindName) != rocmName || !RocmBackend::unavailableReason().empty()) {
        return nullptr;
    }
    return new RocmBackend();
}

BALLS_PLUGIN_EXPORT void ballsDestroyBackend(PhysicsBackend* backend) { delete backend; }

BALLS_PLUGIN_EXPORT GpuRoofline ballsMeasureRoofline(size_t) { return GpuRoofline{}; }
BALLS_PLUGIN_EXPORT size_t ballsStreamedBytesPerBall(bool) { return RocmBackend::streamedBytesPerBall(false); }

BALLS_PLUGIN_EXPORT int ballsDeviceCount(const char* kindName) {
    return std::string_view(kindName) == rocmName ? RocmDriver::deviceCount() : 0;
}

BALLS_PLUGIN_EXPORT int ballsDeviceInfo(const char* kindName, int device, PhysicsDeviceInfo* info) {
    if (!info || device < 0 || device >= ballsDeviceCount(kindName)) {
        return 0;
    }
    const RocmDriver& driver = RocmDriver::forDevice(device);
    *info = PhysicsDeviceInfo{};
    const std::string& name = driver.deviceName();
    std::memcpy(info->name, name.data(), std::min(name.size(), sizeof(info->name) - 1));
    // HSA's agent UUID is not the one Vulkan reports; DeviceInventory matches this GPU by PCI location instead.
    info->hasUuid = 0;
    info->type = driver.isIntegrated() ? PhysicsDeviceType::IntegratedGpu : PhysicsDeviceType::DiscreteGpu;
    info->pciDomain = driver.pciDomain();
    info->pciBus = driver.pciBus();
    info->pciDevice = driver.pciDevice();
    info->memoryBytes = driver.totalMemoryBytes();
    return 1;
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceUnavailableReason(const char* kindName, int device) {
    static std::string reason;
    reason = std::string_view(kindName) == rocmName ? RocmBackend::unavailableReason(device) : std::string(unknownKind);
    return reason.c_str();
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceDetail(const char*, int device) {
    static std::string detail;
    const RocmDriver& driver = RocmDriver::forDevice(device);
    detail = std::format("{} ({})", driver.deviceName(), driver.modelName());
    return detail.c_str();
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackendOnDevice(const char* kindName, int device) {
    if (std::string_view(kindName) != rocmName || !RocmBackend::unavailableReason(device).empty()) {
        return nullptr;
    }
    return new RocmBackend(device);
}
