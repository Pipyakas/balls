#include "sim/BackendFactory.h"

#include "cpu/ScalarBackend.h"

#include <algorithm>
#include <cctype>

namespace {

std::string normalizedKindName(std::string_view name) {
    std::string normalized;
    for (const char character : name) {
        if (character == '+' || character == '-' || character == '_' || character == ' ') {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return normalized;
}

void destroyBackend(PhysicsBackend* backend) { delete backend; }

constexpr std::string_view webUnavailableReason = "not available in the WebAssembly build";

}

std::string_view backendKindName(BackendKind kind) {
    switch (kind) {
    case BackendKind::Scalar:
        return "Scalar";
    case BackendKind::Avx2:
        return "AVX2";
    case BackendKind::Cuda:
        return "CUDA";
    case BackendKind::CudaAvx2:
        return "CUDA+AVX2";
    case BackendKind::Vulkan:
        return "Vulkan";
    case BackendKind::Hybrid:
        return "Hybrid";
    case BackendKind::Zen4:
        return "Zen4";
    case BackendKind::Rocm:
        return "ROCm";
    case BackendKind::VulkanMulti:
        return "VulkanMulti";
    }
    return "?";
}

std::optional<BackendKind> backendKindFromName(std::string_view name) {
    const std::string wanted = normalizedKindName(name);
    const auto match = std::ranges::find_if(
        allBackendKinds, [&](BackendKind kind) { return normalizedKindName(backendKindName(kind)) == wanted; });
    return match == allBackendKinds.end() ? std::nullopt : std::optional(*match);
}

std::string_view backendPluginFileName(BackendKind kind) { return kind == BackendKind::Scalar ? "linked-in" : ""; }

bool isBackendAvailable(BackendKind kind) { return kind == BackendKind::Scalar; }

std::string backendUnavailableReason(BackendKind kind) {
    return kind == BackendKind::Scalar ? std::string() : std::string(webUnavailableReason);
}

std::string backendDetail(BackendKind kind) { return kind == BackendKind::Scalar ? "single-threaded" : std::string(); }

BackendHandle createBackend(BackendKind kind) {
    if (kind != BackendKind::Scalar) {
        return BackendHandle(nullptr, BackendRelease{});
    }
    return BackendHandle(new ScalarBackend(), BackendRelease{destroyBackend, {}});
}

BackendKind fastestAvailableCpuBackend() { return BackendKind::Scalar; }

std::vector<PhysicsDeviceInfo> backendDevices(BackendKind) { return {}; }

std::string backendUnavailableReasonOnDevice(BackendKind kind, int) { return backendUnavailableReason(kind); }

std::string backendDetailOnDevice(BackendKind kind, int) { return backendDetail(kind); }

BackendHandle createBackendOnDevice(BackendKind kind, int) { return createBackend(kind); }

BackendHandle createBackendOnDevices(BackendKind kind, const int*, int) { return createBackend(kind); }

void setPluginSearchDirectory(std::filesystem::path) {}

std::filesystem::path pluginSearchDirectory() { return {}; }

void setComputeGpuPreference(std::string_view) {}

std::string computeGpuPreference() { return "auto"; }

GpuRoofline measureGpuRoofline(size_t) { return {}; }

size_t gpuStreamedBytesPerBall(bool) { return 0; }
