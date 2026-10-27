#include "cpu/ScalarBackend.h"

#if defined(BALLS_HAS_AVX2_BACKEND)
#include "cpu/Avx2Backend.h"
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
constexpr const char* noReason = "";
constexpr const char* missingAvx2 = "CPU lacks AVX2/FMA";
constexpr const char* avx2NotBuilt = "AVX2 backend not built for this CPU architecture";
constexpr const char* unknownKind = "unknown backend";

constexpr const char* threadCountVariable = "BALLS_CPU_THREADS";

unsigned int requestedThreadCount() {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, threadCountVariable) != 0 || !value) {
        return 0;
    }
    const int parsed = std::atoi(value);
    std::free(value);
#else
    const char* value = std::getenv(threadCountVariable);
    const int parsed = value ? std::atoi(value) : 0;
#endif
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
    return unknownKind;
}

BALLS_PLUGIN_EXPORT const char* ballsBackendDetail(const char* kindName) {
    static std::string detail;
    detail = std::string_view(kindName) == avx2Name ? std::format("{} threads", threadCount()) : std::string("1 thread");
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
    return nullptr;
}

BALLS_PLUGIN_EXPORT void ballsDestroyBackend(PhysicsBackend* backend) { delete backend; }
