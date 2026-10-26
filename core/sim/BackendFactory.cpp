#include "sim/BackendFactory.h"

#include "sim/backends/Avx2Backend.h"
#include "sim/backends/ScalarBackend.h"

#if defined(BALLS_HAS_CUDA)
#include "sim/backends/CudaBackend.h"
#endif

#include <algorithm>
#include <format>
#include <thread>

std::string_view backendKindName(BackendKind kind) {
    switch (kind) {
    case BackendKind::Scalar:
        return "Scalar";
    case BackendKind::Avx2:
        return "AVX2";
    case BackendKind::Cuda:
        return "CUDA";
    }
    return "?";
}

bool isBackendAvailable(BackendKind kind) { return backendUnavailableReason(kind).empty(); }

std::string backendUnavailableReason(BackendKind kind) {
    switch (kind) {
    case BackendKind::Scalar:
        return {};
    case BackendKind::Avx2:
        return Avx2Backend::isSupportedByCpu() ? std::string{} : std::string("CPU lacks AVX2/FMA");
    case BackendKind::Cuda:
#if defined(BALLS_HAS_CUDA)
        return CudaBackend::unavailableReason();
#else
        return "built without CUDA";
#endif
    }
    return "unknown backend";
}

std::unique_ptr<PhysicsBackend> createBackend(BackendKind kind) {
    if (!isBackendAvailable(kind)) {
        return nullptr;
    }
    switch (kind) {
    case BackendKind::Scalar:
        return std::make_unique<ScalarBackend>();
    case BackendKind::Avx2:
        return std::make_unique<Avx2Backend>(std::max(1u, std::thread::hardware_concurrency()));
    case BackendKind::Cuda:
#if defined(BALLS_HAS_CUDA)
        return std::make_unique<CudaBackend>();
#else
        return nullptr;
#endif
    }
    return nullptr;
}

std::string backendDetail(BackendKind kind) {
    switch (kind) {
    case BackendKind::Scalar:
        return "1 thread";
    case BackendKind::Avx2:
        return std::format("{} threads", std::max(1u, std::thread::hardware_concurrency()));
    case BackendKind::Cuda:
#if defined(BALLS_HAS_CUDA)
        return CudaDriver::instance().deviceName();
#else
        return {};
#endif
    }
    return {};
}

BackendKind fastestAvailableCpuBackend() {
    return isBackendAvailable(BackendKind::Avx2) ? BackendKind::Avx2 : BackendKind::Scalar;
}
