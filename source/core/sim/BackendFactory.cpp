#include "sim/BackendFactory.h"

#include "sim/BackendSelection.h"
#include "sim/DeviceInventory.h"
#include "sim/HybridBackend.h"

#include <algorithm>
#include <atomic>
#include <cctype>
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
int ballsDeviceCount(const char* kindName);
int ballsDeviceInfo(const char* kindName, int device, PhysicsDeviceInfo* info);
const char* ballsDeviceUnavailableReason(const char* kindName, int device);
const char* ballsDeviceDetail(const char* kindName, int device);
PhysicsBackend* ballsCreateBackendOnDevice(const char* kindName, int device);
}
#endif

namespace {

#if defined(_WIN32)
constexpr std::string_view cpuPluginFile = "balls_physics_cpu.dll";
constexpr std::string_view cudaPluginFile = "balls_physics_cuda.dll";
constexpr std::string_view vulkanPluginFile = "balls_physics_vulkan.dll";
constexpr std::string_view rocmPluginFile = "balls_physics_rocm.dll";
constexpr bool usesShadowCopies = true;
#elif defined(__APPLE__)
constexpr std::string_view cpuPluginFile = "libballs_physics_cpu.dylib";
constexpr std::string_view cudaPluginFile = "libballs_physics_cuda.dylib";
constexpr std::string_view vulkanPluginFile = "libballs_physics_vulkan.dylib";
constexpr std::string_view rocmPluginFile = "libballs_physics_rocm.dylib";
constexpr bool usesShadowCopies = false;
#else
constexpr std::string_view cpuPluginFile = "libballs_physics_cpu.so";
constexpr std::string_view cudaPluginFile = "libballs_physics_cuda.so";
constexpr std::string_view vulkanPluginFile = "libballs_physics_vulkan.so";
constexpr std::string_view rocmPluginFile = "libballs_physics_rocm.so";
constexpr bool usesShadowCopies = false;
#endif
constexpr std::string_view shadowFolder = "balls/physics-plugins";

struct PluginModule {
    using ReleaseIdleDeviceFunction = void (*)();

    std::shared_ptr<DynamicLibrary> library;
    CreateBackendFunction create = nullptr;
    CreateBackendOnDeviceFunction createOnDevice = nullptr;
    CreateBackendOnDevicesFunction createOnDevices = nullptr;
    DestroyBackendFunction destroy = nullptr;
    DeviceCountFunction deviceCount = nullptr;
    DeviceInfoFunction deviceInfo = nullptr;
    DeviceTextFunction deviceUnavailableReason = nullptr;
    DeviceTextFunction deviceDetail = nullptr;
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
        module->createOnDevice = ballsCreateBackendOnDevice;
        module->deviceCount = ballsDeviceCount;
        module->deviceInfo = ballsDeviceInfo;
        module->deviceUnavailableReason = ballsDeviceUnavailableReason;
        module->deviceDetail = ballsDeviceDetail;
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
                          library.resolve("ballsBackendDetail", module->detail) &&
                          library.resolve("ballsCreateBackendOnDevice", module->createOnDevice) &&
                          library.resolve("ballsDeviceCount", module->deviceCount) &&
                          library.resolve("ballsDeviceInfo", module->deviceInfo) &&
                          library.resolve("ballsDeviceUnavailableReason", module->deviceUnavailableReason) &&
                          library.resolve("ballsDeviceDetail", module->deviceDetail);
    library.resolve("ballsMeasureRoofline", module->measureRoofline);
    library.resolve("ballsStreamedBytesPerBall", module->streamedBytes);
    library.resolve("ballsSetComputeGpu", module->setComputeGpu);
    library.resolve("ballsReleaseIdleDevice", module->releaseIdleDevice);
    library.resolve("ballsCreateBackendOnDevices", module->createOnDevices);
    if (interfaceVersion && interfaceVersion() != physicsPluginInterfaceVersion) {
        module->error = std::format("{} was built for plugin interface {} (expected {})", fileName, interfaceVersion(),
                                    physicsPluginInterfaceVersion);
    } else if (!complete) {
        module->error = std::format("{} is missing plugin entry points", fileName);
    }
    return module;
}

const PluginModule& queryPlugin(BackendKind kind) {
    static std::mutex mutex;
    // Never destroyed: unloading a GPU plugin from a static destructor at exit closes its devices after the
    // drivers' own exit handlers ran, which crashes (seen with RADV and NVIDIA together).
    static auto& cache = *new std::map<std::string_view, std::shared_ptr<PluginModule>>();
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

namespace {

std::string normalizedKindName(std::string_view name) {
    std::string normalized;
    for (const char character : name) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
    }
    return normalized;
}

}

std::optional<BackendKind> backendKindFromName(std::string_view name) {
    const std::string wanted = normalizedKindName(name);
    const auto match = std::ranges::find_if(
        allBackendKinds, [&](BackendKind kind) { return normalizedKindName(backendKindName(kind)) == wanted; });
    return match == allBackendKinds.end() ? std::nullopt : std::optional(*match);
}

std::string_view backendPluginFileName(BackendKind kind) {
    if (kind == BackendKind::Cuda) {
        return cudaPluginFile;
    }
    if (kind == BackendKind::Vulkan || kind == BackendKind::VulkanMulti) {
        return vulkanPluginFile;
    }
    if (kind == BackendKind::Rocm) {
        return rocmPluginFile;
    }
    return cpuPluginFile;
}

bool isBackendAvailable(BackendKind kind) { return backendUnavailableReason(kind).empty(); }

namespace {

// The plugin device a picked kind runs on, or nullopt when the kind is not picked and runs on its plugin's default.
std::optional<int> pickedPluginDevice(BackendKind kind) {
    const std::vector<BackendPick> picks = backendPicks();
    if (std::ranges::none_of(picks, [kind](const BackendPick& pick) { return pick.kind == kind; })) {
        return std::nullopt;
    }
    return pluginDeviceFor(kind);
}

}

std::string backendUnavailableReason(BackendKind kind) {
    if (kind == BackendKind::Hybrid) {
        const std::vector<BackendPick> picks = hybridPicks();
        return picks.size() < 2 ? std::string("Hybrid needs two or more devices with a usable backend")
                                : backendPicksProblem(picks);
    }
    if (const std::optional<int> device = pickedPluginDevice(kind)) {
        return backendUnavailableReasonOnDevice(kind, *device);
    }
    if (kind == BackendKind::CudaAvx2) {
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
        return backendPicksLabel(hybridPicks());
    }
    if (const std::optional<int> device = pickedPluginDevice(kind)) {
        return backendDetailOnDevice(kind, *device);
    }
    if (kind == BackendKind::CudaAvx2) {
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
        const std::vector<BackendPick> picks = hybridPicks();
        return picks.size() < 2 ? BackendHandle(nullptr, BackendRelease{}) : createComposedBackend(picks);
    }
    if (const std::optional<int> device = pickedPluginDevice(kind)) {
        return createBackendOnDevice(kind, *device);
    }
    if (kind == BackendKind::CudaAvx2) {
        return HybridBackend::create(createBackend(BackendKind::Cuda), createBackend(BackendKind::Avx2));
    }
    if (kind == BackendKind::VulkanMulti) {
        const std::vector<BackendPick> picks = backendPicks();
        if (picks.size() == 1 && picks.front().kind == BackendKind::VulkanMulti) {
            // claimed devices -> plugin device indices
            std::vector<int> pluginDevices;
            std::string claimed = picks.front().device;
            size_t pos = 0;
            while (true) {
                const size_t next = claimed.find('|', pos);
                const std::string token = claimed.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
                const PhysicalDevice* device = findPhysicalDevice(token);
                if (device && device->option(BackendKind::Vulkan)) {
                    pluginDevices.push_back(device->option(BackendKind::Vulkan)->pluginDevice);
                }
                if (next == std::string::npos) {
                    break;
                }
                pos = next + 1;
            }
            if (!pluginDevices.empty()) {
                return createBackendOnDevices(BackendKind::VulkanMulti, pluginDevices.data(),
                                             static_cast<int>(pluginDevices.size()));
            }
        }
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

std::vector<PhysicsDeviceInfo> backendDevices(BackendKind kind) {
    std::vector<PhysicsDeviceInfo> devices;
    if (kind == BackendKind::CudaAvx2 || kind == BackendKind::Hybrid) {
        return devices;
    }
    const PluginModule& plugin = queryPlugin(kind);
    if (!plugin.error.empty()) {
        return devices;
    }
    const std::string name = kindArgument(kind);
    const int count = plugin.deviceCount(name.c_str());
    for (int device = 0; device < count; ++device) {
        PhysicsDeviceInfo info;
        if (plugin.deviceInfo(name.c_str(), device, &info) != 0) {
            info.name[sizeof(info.name) - 1] = 0;
            devices.push_back(info);
        }
    }
    return devices;
}

std::string backendUnavailableReasonOnDevice(BackendKind kind, int device) {
    if (kind == BackendKind::CudaAvx2 || kind == BackendKind::Hybrid || kind == BackendKind::VulkanMulti) {
        return "not a single-device backend";
    }
    const PluginModule& plugin = queryPlugin(kind);
    return plugin.error.empty() ? std::string(plugin.deviceUnavailableReason(kindArgument(kind).c_str(), device))
                                : plugin.error;
}

std::string backendDetailOnDevice(BackendKind kind, int device) {
    if (kind == BackendKind::CudaAvx2 || kind == BackendKind::Hybrid || kind == BackendKind::VulkanMulti) {
        return {};
    }
    const PluginModule& plugin = queryPlugin(kind);
    return plugin.error.empty() ? std::string(plugin.deviceDetail(kindArgument(kind).c_str(), device)) : std::string{};
}

BackendHandle createBackendOnDevice(BackendKind kind, int device) {
    if (kind == BackendKind::CudaAvx2 || kind == BackendKind::Hybrid || kind == BackendKind::VulkanMulti) {
        return BackendHandle(nullptr, BackendRelease{});
    }
    const std::shared_ptr<PluginModule> plugin = loadPlugin(backendPluginFileName(kind), true);
    const std::string name = kindArgument(kind);
    if (!plugin->error.empty() || std::string_view(plugin->deviceUnavailableReason(name.c_str(), device)) != "") {
        return BackendHandle(nullptr, BackendRelease{});
    }
    PhysicsBackend* backend = plugin->createOnDevice(name.c_str(), device);
    return BackendHandle(backend, BackendRelease{plugin->destroy, plugin->library});
}

BackendHandle createBackendOnDevices(BackendKind kind, const int* devices, int count) {
    if (kind == BackendKind::CudaAvx2 || kind == BackendKind::Hybrid) {
        return BackendHandle(nullptr, BackendRelease{});
    }
    const std::shared_ptr<PluginModule> plugin = loadPlugin(backendPluginFileName(kind), true);
    const std::string name = kindArgument(kind);
    if (!plugin->error.empty() || !plugin->createOnDevices) {
        return BackendHandle(nullptr, BackendRelease{});
    }
    applyComputeGpuPreference(*plugin);
    if (std::string_view(plugin->unavailableReason(name.c_str())) != "") {
        return BackendHandle(nullptr, BackendRelease{});
    }
    PhysicsBackend* backend = plugin->createOnDevices(name.c_str(), devices, count);
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
    if (isBackendAvailable(BackendKind::Zen4)) {
        return BackendKind::Zen4;
    }
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
