#include "sim/BackendFactory.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace {

constexpr std::string_view cpuPluginFile = "balls_physics_cpu.dll";
constexpr std::string_view cudaPluginFile = "balls_physics_cuda.dll";
constexpr std::string_view shadowFolder = "ColorBlastBalls/physics-plugins";

struct PluginModule {
    std::shared_ptr<DynamicLibrary> library;
    CreateBackendFunction create = nullptr;
    DestroyBackendFunction destroy = nullptr;
    BackendTextFunction unavailableReason = nullptr;
    BackendTextFunction detail = nullptr;
    MeasureRooflineFunction measureRoofline = nullptr;
    StreamedBytesFunction streamedBytes = nullptr;
    std::string error;
};

std::filesystem::path shadowDirectory() { return std::filesystem::temp_directory_path() / shadowFolder; }

void removeStaleShadowCopies() {
    static std::once_flag cleaned;
    std::call_once(cleaned, [] {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(shadowDirectory(), error)) {
            std::filesystem::remove(entry.path(), error);
        }
    });
}

std::filesystem::path makeShadowCopy(const std::filesystem::path& source) {
    static std::atomic<unsigned int> copyCounter = 0;
    removeStaleShadowCopies();
    std::error_code error;
    std::filesystem::create_directories(shadowDirectory(), error);
    const auto stamp = std::filesystem::last_write_time(source, error).time_since_epoch().count();
    const std::filesystem::path target =
        shadowDirectory() / std::format("{}-{}-{}{}", source.stem().string(), stamp, copyCounter++,
                                        source.extension().string());
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
    return error ? source : target;
}

std::shared_ptr<PluginModule> loadPlugin(std::string_view fileName, bool freshCopy) {
    auto module = std::make_shared<PluginModule>();
    const std::filesystem::path source = DynamicLibrary::executableDirectory() / fileName;
    if (!std::filesystem::exists(source)) {
        module->error = std::format("{} not found next to the executable", fileName);
        return module;
    }
    module->library = std::make_shared<DynamicLibrary>(freshCopy ? makeShadowCopy(source) : source);
    if (!module->library->isLoaded()) {
        module->error = std::format("cannot load {}", fileName);
        return module;
    }
    InterfaceVersionFunction interfaceVersion = nullptr;
    const DynamicLibrary& library = *module->library;
    const bool complete = library.resolve("ballsPhysicsInterfaceVersion", interfaceVersion) &&
                          library.resolve("ballsCreateBackend", module->create) &&
                          library.resolve("ballsDestroyBackend", module->destroy) &&
                          library.resolve("ballsBackendUnavailableReason", module->unavailableReason) &&
                          library.resolve("ballsBackendDetail", module->detail);
    library.resolve("ballsMeasureRoofline", module->measureRoofline);
    library.resolve("ballsStreamedBytesPerBall", module->streamedBytes);
    if (!complete) {
        module->error = std::format("{} is missing plugin entry points", fileName);
    } else if (interfaceVersion() != physicsPluginInterfaceVersion) {
        module->error = std::format("{} was built for plugin interface {} (expected {})", fileName, interfaceVersion(),
                                    physicsPluginInterfaceVersion);
    }
    return module;
}

const PluginModule& queryPlugin(BackendKind kind) {
    static std::mutex mutex;
    static std::map<std::string_view, std::shared_ptr<PluginModule>> cache;
    std::lock_guard lock(mutex);
    const std::string_view fileName = backendPluginFileName(kind);
    auto& module = cache[fileName];
    if (!module) {
        module = loadPlugin(fileName, true);
    }
    return *module;
}

std::string kindArgument(BackendKind kind) { return std::string(backendKindName(kind)); }

}

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

std::string_view backendPluginFileName(BackendKind kind) {
    return kind == BackendKind::Cuda ? cudaPluginFile : cpuPluginFile;
}

bool isBackendAvailable(BackendKind kind) { return backendUnavailableReason(kind).empty(); }

std::string backendUnavailableReason(BackendKind kind) {
    const PluginModule& plugin = queryPlugin(kind);
    if (!plugin.error.empty()) {
        return plugin.error;
    }
    return plugin.unavailableReason(kindArgument(kind).c_str());
}

std::string backendDetail(BackendKind kind) {
    const PluginModule& plugin = queryPlugin(kind);
    return plugin.error.empty() ? std::string(plugin.detail(kindArgument(kind).c_str())) : std::string{};
}

BackendHandle createBackend(BackendKind kind) {
    const std::shared_ptr<PluginModule> plugin = loadPlugin(backendPluginFileName(kind), true);
    if (!plugin->error.empty() || std::string_view(plugin->unavailableReason(kindArgument(kind).c_str())) != "") {
        return BackendHandle(nullptr, BackendRelease{});
    }
    PhysicsBackend* backend = plugin->create(kindArgument(kind).c_str());
    return BackendHandle(backend, BackendRelease{plugin->destroy, plugin->library});
}

BackendKind fastestAvailableCpuBackend() {
    return isBackendAvailable(BackendKind::Avx2) ? BackendKind::Avx2 : BackendKind::Scalar;
}

GpuRoofline measureGpuRoofline(size_t bufferBytes) {
    const PluginModule& plugin = queryPlugin(BackendKind::Cuda);
    return plugin.error.empty() && plugin.measureRoofline ? plugin.measureRoofline(bufferBytes) : GpuRoofline{};
}

size_t gpuStreamedBytesPerBall(bool gridActive) {
    const PluginModule& plugin = queryPlugin(BackendKind::Cuda);
    return plugin.error.empty() && plugin.streamedBytes ? plugin.streamedBytes(gridActive) : 0;
}
