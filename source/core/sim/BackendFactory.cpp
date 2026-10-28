#include "sim/BackendFactory.h"

#include "sim/HybridBackend.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#if defined(BALLS_STATIC_CPU_BACKEND)
extern "C" {
int ballsPhysicsInterfaceVersion();
const char* ballsBackendUnavailableReason(const char* kindName);
const char* ballsBackendDetail(const char* kindName);
PhysicsBackend* ballsCreateBackend(const char* kindName);
void ballsDestroyBackend(PhysicsBackend* backend);
}
#endif

namespace {

#if defined(_WIN32)
constexpr std::string_view cpuPluginFile = "balls_physics_cpu.dll";
constexpr std::string_view cudaPluginFile = "balls_physics_cuda.dll";
constexpr std::string_view vulkanPluginFile = "balls_physics_vulkan.dll";
constexpr bool usesShadowCopies = true;
#elif defined(__APPLE__)
constexpr std::string_view cpuPluginFile = "libballs_physics_cpu.dylib";
constexpr std::string_view cudaPluginFile = "libballs_physics_cuda.dylib";
constexpr std::string_view vulkanPluginFile = "libballs_physics_vulkan.dylib";
constexpr bool usesShadowCopies = false;
#else
constexpr std::string_view cpuPluginFile = "libballs_physics_cpu.so";
constexpr std::string_view cudaPluginFile = "libballs_physics_cuda.so";
constexpr std::string_view vulkanPluginFile = "libballs_physics_vulkan.so";
constexpr bool usesShadowCopies = false;
#endif
constexpr std::string_view shadowFolder = "balls/physics-plugins";

struct PluginModule {
    using ReleaseIdleDeviceFunction = void (*)();

    std::shared_ptr<DynamicLibrary> library;
    CreateBackendFunction create = nullptr;
    DestroyBackendFunction destroy = nullptr;
    BackendTextFunction unavailableReason = nullptr;
    BackendTextFunction detail = nullptr;
    MeasureRooflineFunction measureRoofline = nullptr;
    StreamedBytesFunction streamedBytes = nullptr;
    SetComputeGpuFunction setComputeGpu = nullptr;
    ReleaseIdleDeviceFunction releaseIdleDevice = nullptr;
    std::string error;

    ~PluginModule() {
        if (releaseIdleDevice) {
            releaseIdleDevice();
        }
    }
};

std::mutex& computeGpuMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string& computeGpuText() {
    static std::string text;
    return text;
}

void applyComputeGpuPreference(const PluginModule& module) {
    if (module.setComputeGpu) {
        const std::string preference = computeGpuPreference();
        module.setComputeGpu(preference.c_str());
    }
}

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
    #if defined(BALLS_STATIC_CPU_BACKEND)
    if (fileName == cpuPluginFile) {
        auto module = std::make_shared<PluginModule>();
        module->create = ballsCreateBackend;
        module->destroy = ballsDestroyBackend;
        module->unavailableReason = ballsBackendUnavailableReason;
        module->detail = ballsBackendDetail;
        if (ballsPhysicsInterfaceVersion() != physicsPluginInterfaceVersion) {
            module->error = std::format("static CPU backend interface mismatch");
        }
        return module;
    }
#endif
    auto module = std::make_shared<PluginModule>();
    const std::filesystem::path besideExecutable = DynamicLibrary::executableDirectory() / fileName;
    const bool foundBesideExecutable = std::filesystem::exists(besideExecutable);
    const std::filesystem::path source = foundBesideExecutable ? besideExecutable : std::filesystem::path(fileName);
    const bool shadow = freshCopy && usesShadowCopies && foundBesideExecutable;
    module->library = std::make_shared<DynamicLibrary>(shadow ? makeShadowCopy(source) : source);
    if (!module->library->isLoaded()) {
        module->error = std::format("cannot load {} (looked next to the executable and on the library path)", fileName);
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
    library.resolve("ballsSetComputeGpu", module->setComputeGpu);
    library.resolve("ballsReleaseIdleDevice", module->releaseIdleDevice);
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
    case BackendKind::Hybrid:
        return "Hybrid";
    case BackendKind::Vulkan:
        return "Vulkan";
    }
    return "?";
}

std::string_view backendPluginFileName(BackendKind kind) {
    if (kind == BackendKind::Cuda) {
        return cudaPluginFile;
    }
    if (kind == BackendKind::Vulkan) {
        return vulkanPluginFile;
    }
    return cpuPluginFile;
}

bool isBackendAvailable(BackendKind kind) { return backendUnavailableReason(kind).empty(); }

std::string backendUnavailableReason(BackendKind kind) {
    if (kind == BackendKind::Hybrid) {
        const std::string gpuReason = backendUnavailableReason(BackendKind::Cuda);
        return gpuReason.empty() ? backendUnavailableReason(BackendKind::Avx2) : gpuReason;
    }
    const PluginModule& plugin = queryPlugin(kind);
    if (!plugin.error.empty()) {
        return plugin.error;
    }
    applyComputeGpuPreference(plugin);
    return plugin.unavailableReason(kindArgument(kind).c_str());
}

std::string backendDetail(BackendKind kind) {
    if (kind == BackendKind::Hybrid) {
        return std::format("{} + {}", backendDetail(BackendKind::Cuda), backendDetail(BackendKind::Avx2));
    }
    const PluginModule& plugin = queryPlugin(kind);
    if (!plugin.error.empty()) {
        return {};
    }
    applyComputeGpuPreference(plugin);
    return std::string(plugin.detail(kindArgument(kind).c_str()));
}

BackendHandle createBackend(BackendKind kind) {
    if (kind == BackendKind::Hybrid) {
        return HybridBackend::create(createBackend(BackendKind::Cuda), createBackend(BackendKind::Avx2));
    }
    const std::shared_ptr<PluginModule> plugin = loadPlugin(backendPluginFileName(kind), true);
    if (plugin->error.empty()) {
        applyComputeGpuPreference(*plugin);
    }
    if (!plugin->error.empty() || std::string_view(plugin->unavailableReason(kindArgument(kind).c_str())) != "") {
        return BackendHandle(nullptr, BackendRelease{});
    }
    PhysicsBackend* backend = plugin->create(kindArgument(kind).c_str());
    return BackendHandle(backend, BackendRelease{plugin->destroy, plugin->library});
}

void setComputeGpuPreference(std::string_view preference) {
    std::lock_guard lock(computeGpuMutex());
    computeGpuText() = preference;
}

std::string computeGpuPreference() {
    std::lock_guard lock(computeGpuMutex());
    return computeGpuText().empty() ? std::string("auto") : computeGpuText();
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
