#include "cuda/CudaBackend.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view cudaName = "CUDA";
constexpr const char* unknownKind = "unknown backend";

}

BALLS_PLUGIN_EXPORT int ballsPhysicsInterfaceVersion() { return physicsPluginInterfaceVersion; }

BALLS_PLUGIN_EXPORT const char* ballsBackendUnavailableReason(const char* kindName) {
    static std::string reason;
    reason = std::string_view(kindName) == cudaName ? CudaBackend::unavailableReason() : std::string(unknownKind);
    return reason.c_str();
}

BALLS_PLUGIN_EXPORT const char* ballsBackendDetail(const char*) {
    return CudaDriver::instance().deviceName().c_str();
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackend(const char* kindName) {
    if (std::string_view(kindName) != cudaName || !CudaBackend::unavailableReason().empty()) {
        return nullptr;
    }
    return new CudaBackend();
}

BALLS_PLUGIN_EXPORT void ballsDestroyBackend(PhysicsBackend* backend) { delete backend; }

BALLS_PLUGIN_EXPORT GpuRoofline ballsMeasureRoofline(size_t bufferBytes) { return CudaBackend::measureRoofline(bufferBytes); }

BALLS_PLUGIN_EXPORT size_t ballsStreamedBytesPerBall(bool gridActive) { return CudaBackend::streamedBytesPerBall(gridActive); }

BALLS_PLUGIN_EXPORT int ballsDeviceCount(const char* kindName) {
    return std::string_view(kindName) == cudaName ? CudaDriver::deviceCount() : 0;
}

BALLS_PLUGIN_EXPORT int ballsDeviceInfo(const char* kindName, int device, PhysicsDeviceInfo* info) {
    if (!info || device < 0 || device >= ballsDeviceCount(kindName)) {
        return 0;
    }
    const CudaDriver& driver = CudaDriver::forDevice(device);
    *info = PhysicsDeviceInfo{};
    const std::string& name = driver.modelName();
    std::memcpy(info->name, name.data(), std::min(name.size(), sizeof(info->name) - 1));
    std::memcpy(info->uuid, driver.uuid().data(), sizeof(info->uuid));
    info->hasUuid = driver.hasUuid() ? 1 : 0;
    info->type = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_INTEGRATED) != 0 ? PhysicsDeviceType::IntegratedGpu
                                                                            : PhysicsDeviceType::DiscreteGpu;
    if (driver.isReady()) {
        info->pciDomain = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID);
        info->pciBus = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_PCI_BUS_ID);
        info->pciDevice = driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID);
    }
    info->memoryBytes = driver.totalMemoryBytes();
    return 1;
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceUnavailableReason(const char* kindName, int device) {
    static std::string reason;
    reason = std::string_view(kindName) == cudaName ? CudaBackend::unavailableReason(device) : std::string(unknownKind);
    return reason.c_str();
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceDetail(const char*, int device) {
    return CudaDriver::forDevice(device).deviceName().c_str();
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackendOnDevice(const char* kindName, int device) {
    if (std::string_view(kindName) != cudaName || !CudaBackend::unavailableReason(device).empty()) {
        return nullptr;
    }
    return new CudaBackend(device);
}
