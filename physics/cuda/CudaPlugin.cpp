#include "cuda/CudaBackend.h"

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
