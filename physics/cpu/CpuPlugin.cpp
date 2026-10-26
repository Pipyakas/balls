#include "cpu/Avx2Backend.h"
#include "cpu/ScalarBackend.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view scalarName = "Scalar";
constexpr std::string_view avx2Name = "AVX2";
constexpr const char* noReason = "";
constexpr const char* missingAvx2 = "CPU lacks AVX2/FMA";
constexpr const char* unknownKind = "unknown backend";

unsigned int threadCount() { return std::max(1u, std::thread::hardware_concurrency()); }

}

BALLS_PLUGIN_EXPORT int ballsPhysicsInterfaceVersion() { return physicsPluginInterfaceVersion; }

BALLS_PLUGIN_EXPORT const char* ballsBackendUnavailableReason(const char* kindName) {
    const std::string_view kind(kindName);
    if (kind == scalarName) {
        return noReason;
    }
    if (kind == avx2Name) {
        return Avx2Backend::isSupportedByCpu() ? noReason : missingAvx2;
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
    if (kind == avx2Name && Avx2Backend::isSupportedByCpu()) {
        return new Avx2Backend(threadCount());
    }
    return nullptr;
}

BALLS_PLUGIN_EXPORT void ballsDestroyBackend(PhysicsBackend* backend) { delete backend; }
