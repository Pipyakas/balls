#include "common/Environment.h"
#include "cpu/ScalarBackend.h"

#if defined(BALLS_HAS_AVX2_BACKEND)
#include "cpu/Avx2Backend.h"
#endif
#if defined(BALLS_HAS_ZEN4_BACKEND)
#include "cpu/Zen4Backend.h"
#endif

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view scalarName = "Scalar";
constexpr std::string_view avx2Name = "AVX2";
constexpr std::string_view zen4Name = "Zen4";
constexpr const char* noReason = "";
constexpr const char* missingAvx2 = "CPU lacks AVX2/FMA";
constexpr const char* missingZen4 = "CPU lacks AVX-512";
constexpr const char* avx2NotBuilt = "AVX2 backend not built for this CPU architecture";
constexpr const char* zen4NotBuilt = "Zen4 backend not built for this CPU architecture";
constexpr const char* unknownKind = "unknown backend";

constexpr const char* threadCountVariable = "BALLS_CPU_THREADS";

unsigned int requestedThreadCount() {
    const std::optional<std::string> value = environmentVariable(threadCountVariable);
    const int parsed = value ? std::atoi(value->c_str()) : 0;
    return parsed > 0 ? static_cast<unsigned int>(parsed) : 0;
}

unsigned int threadCount() {
    const unsigned int requested = requestedThreadCount();
    return requested > 0 ? requested : std::max(1u, std::thread::hardware_concurrency());
}

}

BALLS_PLUGIN_EXPORT int ballsPhysicsInterfaceVersion() { return physicsPluginInterfaceVersion; }

BALLS_PLUGIN_EXPORT const char* ballsBackendUnavailableReason(const char* kindName) {
    const std::string_view kind(kindName);
    if (kind == scalarName) {
        return noReason;
    }
    if (kind == avx2Name) {
#if defined(BALLS_HAS_AVX2_BACKEND)
        return Avx2Backend::isSupportedByCpu() ? noReason : missingAvx2;
#else
        return avx2NotBuilt;
#endif
    }
    if (kind == zen4Name) {
#if defined(BALLS_HAS_ZEN4_BACKEND)
        return Zen4Backend::isSupportedByCpu() ? noReason : missingZen4;
#else
        return zen4NotBuilt;
#endif
    }
    return unknownKind;
}

BALLS_PLUGIN_EXPORT const char* ballsBackendDetail(const char* kindName) {
    static std::string detail;
    detail = (std::string_view(kindName) == avx2Name || std::string_view(kindName) == zen4Name) ? std::format("{} threads", threadCount()) : std::string("1 thread");
    return detail.c_str();
}

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackend(const char* kindName) {
    const std::string_view kind(kindName);
    if (kind == scalarName) {
        return new ScalarBackend();
    }
#if defined(BALLS_HAS_AVX2_BACKEND)
    if (kind == avx2Name && Avx2Backend::isSupportedByCpu()) {
        return new Avx2Backend(threadCount());
    }
#endif
#if defined(BALLS_HAS_ZEN4_BACKEND)
    if (kind == zen4Name && Zen4Backend::isSupportedByCpu()) {
        return new Zen4Backend(threadCount());
    }
#endif
    return nullptr;
}

BALLS_PLUGIN_EXPORT void ballsDestroyBackend(PhysicsBackend* backend) { delete backend; }

BALLS_PLUGIN_EXPORT int ballsDeviceCount(const char* kindName) {
    const std::string_view kind(kindName);
    return kind == scalarName || kind == avx2Name || kind == zen4Name ? 1 : 0;
}

BALLS_PLUGIN_EXPORT int ballsDeviceInfo(const char* kindName, int device, PhysicsDeviceInfo* info) {
    if (device != 0 || ballsDeviceCount(kindName) == 0 || !info) {
        return 0;
    }
    *info = PhysicsDeviceInfo{};
    info->type = PhysicsDeviceType::Cpu;
    return 1;
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceUnavailableReason(const char* kindName, int device) {
    return device == 0 ? ballsBackendUnavailableReason(kindName) : unknownKind;
}

BALLS_PLUGIN_EXPORT const char* ballsDeviceDetail(const char* kindName, int) { return ballsBackendDetail(kindName); }

BALLS_PLUGIN_EXPORT PhysicsBackend* ballsCreateBackendOnDevice(const char* kindName, int device) {
    return device == 0 ? ballsCreateBackend(kindName) : nullptr;
}
