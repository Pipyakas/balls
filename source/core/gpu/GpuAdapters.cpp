#include "gpu/GpuAdapters.h"

#include "common/DynamicLibrary.h"

#if __has_include(<vulkan/vulkan_core.h>)
#include <vulkan/vulkan_core.h>
#define BALLS_HAS_VULKAN_HEADERS 1
#else
#define BALLS_HAS_VULKAN_HEADERS 0
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#endif

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::string lowercase(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

// The Microsoft Basic Render Driver (WARP) does not always carry DXGI_ADAPTER_FLAG_SOFTWARE.
constexpr uint32_t microsoftVendorId = 0x1414;
constexpr uint32_t warpDeviceId = 0x008c;

bool usable(const GpuAdapterInfo& adapter, GpuRole role) {
    return role == GpuRole::Render ? adapter.usableForRender() : adapter.usableForCompute();
}

#if BALLS_HAS_VULKAN_HEADERS

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return std::string_view(extension.extensionName) == name;
    });
}

bool atLeastVersion(uint32_t version, uint32_t major, uint32_t minor) {
    return VK_API_VERSION_MAJOR(version) > major ||
           (VK_API_VERSION_MAJOR(version) == major && VK_API_VERSION_MINOR(version) >= minor);
}

template <typename Function>
Function loadVulkanFunction(PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkInstance instance, const char* name) {
    return reinterpret_cast<Function>(getInstanceProcAddr(instance, name));
}

class VulkanInstanceGuard {
public:
    VulkanInstanceGuard(VkInstance instance, PFN_vkDestroyInstance destroyInstance)
        : instance(instance), destroyInstance(destroyInstance) {}
    ~VulkanInstanceGuard() {
        if (instance != VK_NULL_HANDLE && destroyInstance) {
            destroyInstance(instance, nullptr);
        }
    }
    VulkanInstanceGuard(const VulkanInstanceGuard&) = delete;
    VulkanInstanceGuard& operator=(const VulkanInstanceGuard&) = delete;

private:
    VkInstance instance;
    PFN_vkDestroyInstance destroyInstance;
};

template <typename Properties>
bool enumerateVulkanProperties(PFN_vkEnumerateInstanceExtensionProperties enumerate,
                               const char* layerName,
                               std::vector<Properties>& properties) {
    uint32_t count = 0;
    if (enumerate(layerName, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        properties.resize(count);
        const VkResult result = enumerate(layerName, &count, properties.data());
        if (result == VK_SUCCESS) {
            properties.resize(count);
            return true;
        }
        if (result != VK_INCOMPLETE) {
            return false;
        }
    }
    return false;
}

template <typename Properties>
bool enumerateDeviceProperties(PFN_vkEnumerateDeviceExtensionProperties enumerate,
                               VkPhysicalDevice device,
                               std::vector<Properties>& properties) {
    uint32_t count = 0;
    if (enumerate(device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        properties.resize(count);
        const VkResult result = enumerate(device, nullptr, &count, properties.data());
        if (result == VK_SUCCESS) {
            properties.resize(count);
            return true;
        }
        if (result != VK_INCOMPLETE) {
            return false;
        }
    }
    return false;
}

std::vector<GpuAdapterInfo> enumerateVulkanAdapters() {
    try {
#if defined(_WIN32)
        DynamicLibrary loader("vulkan-1.dll");
#elif defined(__APPLE__)
        DynamicLibrary loader("libvulkan.1.dylib");
#else
        DynamicLibrary loader("libvulkan.so.1");
#endif
        PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
        if (!loader.resolve("vkGetInstanceProcAddr", getInstanceProcAddr) || !getInstanceProcAddr) {
            return {};
        }

        const auto enumerateInstanceExtensions = loadVulkanFunction<PFN_vkEnumerateInstanceExtensionProperties>(
            getInstanceProcAddr, VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
        const auto createInstance = loadVulkanFunction<PFN_vkCreateInstance>(getInstanceProcAddr, VK_NULL_HANDLE,
                                                                            "vkCreateInstance");
        if (!enumerateInstanceExtensions || !createInstance) {
            return {};
        }

        std::vector<VkExtensionProperties> instanceExtensions;
        if (!enumerateVulkanProperties(enumerateInstanceExtensions, nullptr, instanceExtensions)) {
            return {};
        }
        const bool portabilityEnumeration =
            hasExtension(instanceExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        const char* enabledExtension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;

        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "Balls adapter enumeration";
        applicationInfo.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.flags = portabilityEnumeration ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount = portabilityEnumeration ? 1u : 0u;
        createInfo.ppEnabledExtensionNames = portabilityEnumeration ? &enabledExtension : nullptr;

        VkInstance instance = VK_NULL_HANDLE;
        if (createInstance(&createInfo, nullptr, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
            return {};
        }
        const auto destroyInstance = loadVulkanFunction<PFN_vkDestroyInstance>(getInstanceProcAddr, instance,
                                                                               "vkDestroyInstance");
        VulkanInstanceGuard instanceGuard(instance, destroyInstance);
        if (!destroyInstance) {
            return {};
        }

        const auto enumeratePhysicalDevices = loadVulkanFunction<PFN_vkEnumeratePhysicalDevices>(
            getInstanceProcAddr, instance, "vkEnumeratePhysicalDevices");
        const auto enumerateDeviceExtensions = loadVulkanFunction<PFN_vkEnumerateDeviceExtensionProperties>(
            getInstanceProcAddr, instance, "vkEnumerateDeviceExtensionProperties");
        const auto getProperties2 = loadVulkanFunction<PFN_vkGetPhysicalDeviceProperties2>(
            getInstanceProcAddr, instance, "vkGetPhysicalDeviceProperties2");
        const auto getFeatures2 = loadVulkanFunction<PFN_vkGetPhysicalDeviceFeatures2>(
            getInstanceProcAddr, instance, "vkGetPhysicalDeviceFeatures2");
        const auto getQueueFamilyProperties = loadVulkanFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            getInstanceProcAddr, instance, "vkGetPhysicalDeviceQueueFamilyProperties");
        const auto getMemoryProperties = loadVulkanFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(
            getInstanceProcAddr, instance, "vkGetPhysicalDeviceMemoryProperties");
        if (!enumeratePhysicalDevices || !enumerateDeviceExtensions || !getProperties2 || !getFeatures2 ||
            !getQueueFamilyProperties || !getMemoryProperties) {
            return {};
        }

        uint32_t deviceCount = 0;
        if (enumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS) {
            return {};
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            std::vector<VkPhysicalDevice> devices(deviceCount);
            const VkResult result = enumeratePhysicalDevices(instance, &deviceCount, devices.data());
            if (result == VK_INCOMPLETE) {
                continue;
            }
            if (result != VK_SUCCESS) {
                return {};
            }
            devices.resize(deviceCount);
            std::vector<GpuAdapterInfo> adapters;
            adapters.reserve(devices.size());
            for (uint32_t index = 0; index < devices.size(); ++index) {
                const VkPhysicalDevice device = devices[index];
                std::vector<VkExtensionProperties> deviceExtensions;
                if (!enumerateDeviceProperties(enumerateDeviceExtensions, device, deviceExtensions)) {
                    deviceExtensions.clear();
                }

                VkPhysicalDeviceVulkan11Properties properties11{};
                properties11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
                VkPhysicalDeviceVulkan12Properties properties12{};
                properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
                VkPhysicalDeviceSubgroupSizeControlProperties subgroupProperties{};
                subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;
                VkPhysicalDeviceProperties2 properties{};
                properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

                getProperties2(device, &properties);
                const uint32_t apiVersion = properties.properties.apiVersion;
                const bool hasVulkan11 = atLeastVersion(apiVersion, 1, 1);
                const bool hasSubgroupSizeControl =
                    hasExtension(deviceExtensions, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) ||
                    atLeastVersion(apiVersion, 1, 3);
                const bool hasVulkan12 = atLeastVersion(apiVersion, 1, 2);
                if (hasVulkan11) {
                    properties.pNext = &properties11;
                    properties11.pNext = hasSubgroupSizeControl ? static_cast<void*>(&subgroupProperties)
                                                                : (hasVulkan12 ? static_cast<void*>(&properties12) : nullptr);
                    if (hasSubgroupSizeControl) {
                        subgroupProperties.pNext = hasVulkan12 ? static_cast<void*>(&properties12) : nullptr;
                    }
                } else if (hasSubgroupSizeControl) {
                    properties.pNext = &subgroupProperties;
                } else if (hasVulkan12) {
                    properties.pNext = &properties12;
                }
                if (properties.pNext) {
                    getProperties2(device, &properties);
                }

                VkPhysicalDeviceVulkan12Features features12{};
                features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
                VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFeatures{};
                atomicFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
                VkPhysicalDeviceFeatures2 features{};
                features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                const bool hasAtomicFloat =
                    hasExtension(deviceExtensions, VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
                features.pNext = hasVulkan12 ? static_cast<void*>(&features12)
                                             : (hasAtomicFloat ? static_cast<void*>(&atomicFeatures) : nullptr);
                if (hasVulkan12) {
                    features12.pNext = hasAtomicFloat ? static_cast<void*>(&atomicFeatures) : nullptr;
                }
                getFeatures2(device, &features);

                GpuAdapterInfo adapter;
                adapter.api = GpuApi::Vulkan;
                adapter.index = static_cast<int>(index);
                adapter.name = properties.properties.deviceName;
                if (hasVulkan12) {
                    adapter.driver = properties12.driverName;
                    if (properties12.driverInfo[0] != '\0') {
                        if (!adapter.driver.empty()) {
                            adapter.driver += ' ';
                        }
                        adapter.driver += properties12.driverInfo;
                    }
                }
                adapter.vendorId = properties.properties.vendorID;
                adapter.deviceId = properties.properties.deviceID;
                adapter.vendor = vendorFromPciId(adapter.vendorId);
                adapter.apiVersion = apiVersion;
                switch (properties.properties.deviceType) {
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                    adapter.kind = GpuKind::Discrete;
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                    adapter.kind = GpuKind::Integrated;
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                    adapter.kind = GpuKind::Virtual;
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_CPU:
                    adapter.kind = GpuKind::Software;
                    break;
                default:
                    adapter.kind = GpuKind::Unknown;
                    break;
                }
                adapter.subgroupMin = hasSubgroupSizeControl ? subgroupProperties.minSubgroupSize
                                                              : properties11.subgroupSize;
                adapter.subgroupMax = hasSubgroupSizeControl ? subgroupProperties.maxSubgroupSize
                                                              : properties11.subgroupSize;
                adapter.maxSharedMemoryBytes = properties.properties.limits.maxComputeSharedMemorySize;
                adapter.maxWorkgroupInvocations = properties.properties.limits.maxComputeWorkGroupInvocations;
                adapter.shaderInt64 = features.features.shaderInt64 == VK_TRUE;
                adapter.shaderInt16 = features.features.shaderInt16 == VK_TRUE;
                adapter.shaderFloat16 = hasVulkan12 && features12.shaderFloat16 == VK_TRUE;
                adapter.atomicFloat = hasAtomicFloat && atomicFeatures.shaderBufferFloat32AtomicAdd == VK_TRUE;

                uint32_t familyCount = 0;
                getQueueFamilyProperties(device, &familyCount, nullptr);
                std::vector<VkQueueFamilyProperties> families(familyCount);
                getQueueFamilyProperties(device, &familyCount, families.data());
                for (const VkQueueFamilyProperties& family : families) {
                    adapter.hasGraphicsQueue = adapter.hasGraphicsQueue ||
                                               (family.queueCount > 0 && (family.queueFlags & VK_QUEUE_GRAPHICS_BIT));
                    adapter.hasComputeQueue = adapter.hasComputeQueue ||
                                              (family.queueCount > 0 && (family.queueFlags & VK_QUEUE_COMPUTE_BIT));
                }

                VkPhysicalDeviceMemoryProperties memoryProperties{};
                getMemoryProperties(device, &memoryProperties);
                for (uint32_t heap = 0; heap < memoryProperties.memoryHeapCount; ++heap) {
                    const VkMemoryHeap& candidate = memoryProperties.memoryHeaps[heap];
                    if ((candidate.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                        adapter.deviceLocalBytes = std::max(adapter.deviceLocalBytes,
                                                            static_cast<uint64_t>(candidate.size));
                    }
                }
                adapters.push_back(std::move(adapter));
            }
            return adapters;
        }
    } catch (...) {
        return {};
    }
    return {};
}

#else

std::vector<GpuAdapterInfo> enumerateVulkanAdapters() {
    // Default SDL/NVRHI builds expose their already-fetched Vulkan-Headers target to balls_core.
    // Builds without Vulkan headers cannot describe the Vulkan ABI and therefore report no adapters.
    return {};
}

#endif

#if defined(_WIN32)

using CreateDxgiFactory1 = HRESULT(WINAPI*)(REFIID, void**);
using D3D12CreateDeviceFunction = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

template <typename Interface>
struct ComReleaser {
    void operator()(Interface* object) const {
        if (object) {
            object->Release();
        }
    }
};

std::string utf8FromWide(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::vector<GpuAdapterInfo> enumerateD3d12Adapters() {
    try {
        DynamicLibrary dxgiLibrary("dxgi.dll");
        DynamicLibrary d3d12Library("d3d12.dll");
        CreateDxgiFactory1 createFactory = nullptr;
        D3D12CreateDeviceFunction createDevice = nullptr;
        if (!dxgiLibrary.resolve("CreateDXGIFactory1", createFactory) ||
            !d3d12Library.resolve("D3D12CreateDevice", createDevice)) {
            return {};
        }

        IDXGIFactory1* rawFactory = nullptr;
        const HRESULT factoryResult =
            createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&rawFactory));
        std::unique_ptr<IDXGIFactory1, ComReleaser<IDXGIFactory1>> factory(rawFactory);
        if (FAILED(factoryResult) || !factory) {
            return {};
        }
        std::vector<GpuAdapterInfo> adapters;
        std::vector<DXGI_ADAPTER_DESC1> seenAdapters;
        for (UINT index = 0;; ++index) {
            IDXGIAdapter1* rawAdapter = nullptr;
            const HRESULT enumResult = factory->EnumAdapters1(index, &rawAdapter);
            std::unique_ptr<IDXGIAdapter1, ComReleaser<IDXGIAdapter1>> dxgiAdapter(rawAdapter);
            if (enumResult == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(enumResult) || !dxgiAdapter) {
                break;
            }

            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(dxgiAdapter->GetDesc1(&description))) {
                continue;
            }
            // DXGI can list one GPU twice with different LUIDs (seen on a GTX 1650 desktop with a remote-display
            // driver installed), so duplicates are matched on the PCI identity and memory size instead.
            const bool alreadyListed = std::ranges::any_of(seenAdapters, [&](const DXGI_ADAPTER_DESC1& seen) {
                return seen.VendorId == description.VendorId && seen.DeviceId == description.DeviceId &&
                       seen.SubSysId == description.SubSysId && seen.Revision == description.Revision &&
                       seen.DedicatedVideoMemory == description.DedicatedVideoMemory;
            });
            if (alreadyListed) {
                continue;
            }
            seenAdapters.push_back(description);
            GpuAdapterInfo adapter;
            adapter.api = GpuApi::D3d12;
            adapter.index = static_cast<int>(index);
            adapter.name = utf8FromWide(description.Description);
            adapter.vendorId = description.VendorId;
            adapter.deviceId = description.DeviceId;
            adapter.vendor = vendorFromPciId(adapter.vendorId);
            adapter.deviceLocalBytes = description.DedicatedVideoMemory;
            const bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
                                  (description.VendorId == microsoftVendorId && description.DeviceId == warpDeviceId);
            adapter.kind = software ? GpuKind::Software
                                    : (description.DedicatedVideoMemory > 0 ? GpuKind::Discrete : GpuKind::Integrated);

            {
                constexpr D3D_FEATURE_LEVEL featureLevels[] = {
                    static_cast<D3D_FEATURE_LEVEL>(0xc200),  // 12_2
                    D3D_FEATURE_LEVEL_12_1,
                    D3D_FEATURE_LEVEL_12_0,
                    D3D_FEATURE_LEVEL_11_1,
                    D3D_FEATURE_LEVEL_11_0,
                };
                ID3D12Device* rawDevice = nullptr;
                const HRESULT baseDeviceResult =
                    createDevice(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                 reinterpret_cast<void**>(&rawDevice));
                std::unique_ptr<ID3D12Device, ComReleaser<ID3D12Device>> device(rawDevice);
                if (SUCCEEDED(baseDeviceResult) && device) {
                    adapter.hasGraphicsQueue = true;
                    adapter.hasComputeQueue = true;

                    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
                    architecture.NodeIndex = 0;
                    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture,
                                                              sizeof(architecture))) &&
                        !software) {
                        adapter.kind = architecture.UMA || description.DedicatedVideoMemory == 0
                                           ? GpuKind::Integrated
                                           : GpuKind::Discrete;
                    }
                    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options{};
                    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options,
                                                              sizeof(options)))) {
                        adapter.subgroupMin = options.WaveLaneCountMin;
                        adapter.subgroupMax = options.WaveLaneCountMax;
                    }

                    for (D3D_FEATURE_LEVEL level : featureLevels) {
                        ID3D12Device* featureDevice = nullptr;
                        if (SUCCEEDED(createDevice(dxgiAdapter.get(), level, __uuidof(ID3D12Device),
                                                   reinterpret_cast<void**>(&featureDevice))) && featureDevice) {
                            adapter.apiVersion = static_cast<uint32_t>(level);
                            featureDevice->Release();
                            break;
                        }
                        if (featureDevice) {
                            featureDevice->Release();
                        }
                    }
                }
            }
            adapters.push_back(std::move(adapter));
        }
        return adapters;
    } catch (...) {
        return {};
    }
}

#endif

}

namespace {

std::vector<GpuAdapterInfo> enumerateUncached(GpuApi api) {
    if (api == GpuApi::Vulkan) {
        return enumerateVulkanAdapters();
    }
#if defined(_WIN32)
    if (api == GpuApi::D3d12) {
        return enumerateD3d12Adapters();
    }
#else
    (void)api;
#endif
    return {};
}

} // namespace

std::vector<GpuAdapterInfo> enumerateGpuAdapters(GpuApi api) {
    static std::mutex mutex;
    static std::map<GpuApi, std::vector<GpuAdapterInfo>> cache;
    std::lock_guard lock(mutex);
    const auto found = cache.find(api);
    if (found != cache.end()) {
        return found->second;
    }
    return cache[api] = enumerateUncached(api);
}

GpuVendor vendorFromPciId(uint32_t vendorId) {
    switch (vendorId) {
    case 0x10de:
        return GpuVendor::Nvidia;
    case 0x1002:
    case 0x1022:
        return GpuVendor::Amd;
    case 0x8086:
    case 0x8087:
        return GpuVendor::Intel;
    case 0x106b:
        return GpuVendor::Apple;
    case 0x5143:
        return GpuVendor::Qualcomm;
    case 0x13b5:
        return GpuVendor::Arm;
    case 0:
        return GpuVendor::Unknown;
    default:
        return GpuVendor::Other;
    }
}

std::string_view gpuVendorName(GpuVendor vendor) {
    switch (vendor) {
    case GpuVendor::Nvidia:
        return "NVIDIA";
    case GpuVendor::Amd:
        return "AMD";
    case GpuVendor::Intel:
        return "Intel";
    case GpuVendor::Apple:
        return "Apple";
    case GpuVendor::Qualcomm:
        return "Qualcomm";
    case GpuVendor::Arm:
        return "Arm";
    case GpuVendor::Other:
        return "Other";
    default:
        return "Unknown";
    }
}

std::string_view gpuKindName(GpuKind kind) {
    switch (kind) {
    case GpuKind::Discrete:
        return "discrete";
    case GpuKind::Integrated:
        return "integrated";
    case GpuKind::Virtual:
        return "virtual";
    case GpuKind::Software:
        return "software";
    default:
        return "unknown";
    }
}

std::string GpuAdapterInfo::summary() const {
    const std::string wave = subgroupMax == 0 ? "?" : std::to_string(subgroupMax);
    return std::format("{} ({}, wave {}, {} MB)", name, gpuKindName(kind), wave,
                       deviceLocalBytes / (1024 * 1024));
}

int selectGpuAdapter(std::span<const GpuAdapterInfo> adapters, std::string_view preference, GpuRole role) {
    try {
        const std::string normalizedPreference = lowercase(trim(preference));
        std::vector<size_t> candidates;
        candidates.reserve(adapters.size());
        for (size_t index = 0; index < adapters.size(); ++index) {
            if (usable(adapters[index], role)) {
                candidates.push_back(index);
            }
        }
        if (candidates.empty()) {
            return -1;
        }

        const auto largestDiscrete = [&]() -> int {
            int best = -1;
            for (size_t index : candidates) {
                if (adapters[index].kind == GpuKind::Discrete &&
                    (best < 0 || adapters[index].deviceLocalBytes > adapters[static_cast<size_t>(best)].deviceLocalBytes)) {
                    best = static_cast<int>(index);
                }
            }
            return best;
        };

        if (normalizedPreference.empty() || normalizedPreference == "auto") {
            if (const int discrete = largestDiscrete(); discrete >= 0) {
                return discrete;
            }
            const auto integrated = std::ranges::find_if(candidates, [&](size_t index) {
                return adapters[index].kind == GpuKind::Integrated;
            });
            if (integrated != candidates.end()) {
                return static_cast<int>(*integrated);
            }
            const auto hardware = std::ranges::find_if(candidates, [&](size_t index) {
                return adapters[index].kind != GpuKind::Software;
            });
            return static_cast<int>(hardware != candidates.end() ? *hardware : candidates.front());
        }

        if (normalizedPreference == "integrated") {
            const auto match = std::ranges::find_if(candidates, [&](size_t index) {
                return adapters[index].kind == GpuKind::Integrated;
            });
            return match == candidates.end() ? -1 : static_cast<int>(*match);
        }
        if (normalizedPreference == "discrete") {
            return largestDiscrete();
        }

        bool decimal = !normalizedPreference.empty();
        for (unsigned char character : normalizedPreference) {
            decimal = decimal && character >= '0' && character <= '9';
        }
        if (decimal) {
            size_t index = 0;
            const auto [end, error] = std::from_chars(normalizedPreference.data(),
                                                       normalizedPreference.data() + normalizedPreference.size(), index);
            if (error != std::errc{} || end != normalizedPreference.data() + normalizedPreference.size() ||
                index >= adapters.size() || !usable(adapters[index], role)) {
                return -1;
            }
            return static_cast<int>(index);
        }

        const auto match = std::ranges::find_if(candidates, [&](size_t index) {
            return lowercase(adapters[index].name).find(normalizedPreference) != std::string::npos;
        });
        return match == candidates.end() ? -1 : static_cast<int>(*match);
    } catch (...) {
        return -1;
    }
}

std::span<const BallStepTuning> ballStepProfiles() {
    static constexpr BallStepTuning profiles[] = {
#define BALLS_BALLSTEP_PROFILE(name, threads, balls, countThreads, countItems, sortThreads, sortItems) \
    {#name, threads, balls, countThreads, countItems, sortThreads, sortItems},
#include "gpu/BallStepProfiles.def"
#undef BALLS_BALLSTEP_PROFILE
    };
    return profiles;
}

BallStepTuning ballStepTuningFor(const GpuAdapterInfo& adapter) {
    const std::string_view wanted = adapter.subgroupMax > 32 ? "wave64" : "wave32";
    for (const BallStepTuning& profile : ballStepProfiles()) {
        if (profile.variant == wanted) {
            return profile;
        }
    }
    return ballStepProfiles().front();
}
