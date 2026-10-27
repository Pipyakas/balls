#include "vulkan/VulkanComputeDevice.h"

#include <cstdio>
#include <format>
#include <ranges>
#include <vector>

namespace {

auto& vulkanDispatcher() { return VULKAN_HPP_DEFAULT_DISPATCHER; }

#if defined(_WIN32)
constexpr const char* vulkanLibraryName = "vulkan-1.dll";
#elif defined(__APPLE__)
constexpr const char* vulkanLibraryName = "libvulkan.1.dylib";
#else
constexpr const char* vulkanLibraryName = "libvulkan.so.1";
#endif

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return std::string_view(extension.extensionName) == name;
    });
}

bool hasDeviceExtension(const vk::detail::DispatchLoaderDynamic& dispatcher, VkPhysicalDevice device, const char* name) {
    uint32_t count = 0;
    if (dispatcher.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (dispatcher.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return hasExtension(extensions, name);
}

uint32_t minVersion(uint32_t left, uint32_t right) { return left < right ? left : right; }

}

class VulkanComputeDevice::MessageCallback final : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        if (severity >= nvrhi::MessageSeverity::Error) {
            lastError = text ? text : "NVRHI Vulkan error";
        }
        if (severity >= nvrhi::MessageSeverity::Warning) {
            std::fprintf(stderr, "nvrhi Vulkan: %s\n", text ? text : "");
        }
    }

    std::string lastError;
};

std::shared_ptr<VulkanComputeDevice> VulkanComputeDevice::create(std::string_view preference, std::string& error) {
    auto result = std::shared_ptr<VulkanComputeDevice>(new VulkanComputeDevice());
    if (!result->initialize(preference, error)) {
        return {};
    }
    return result;
}

VulkanComputeDevice::~VulkanComputeDevice() {
    nvrhiDevice = nullptr;
    if (logicalDevice != VK_NULL_HANDLE) {
        deviceDispatcher.vkDeviceWaitIdle(logicalDevice);
        deviceDispatcher.vkDestroyDevice(logicalDevice, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        instanceDispatcher.vkDestroyInstance(instance, nullptr);
    }
}

bool VulkanComputeDevice::fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

bool VulkanComputeDevice::initialize(std::string_view preference, std::string& error) {
    loader = std::make_unique<DynamicLibrary>(vulkanLibraryName);
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    if (!loader->isLoaded() || !loader->resolve("vkGetInstanceProcAddr", getInstanceProcAddr) || !getInstanceProcAddr) {
        return fail(error, std::format("Vulkan loader unavailable ({})", vulkanLibraryName));
    }
    vulkanDispatcher().init(getInstanceProcAddr);

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerateInstanceVersion = nullptr;
    loader->resolve("vkEnumerateInstanceVersion", enumerateInstanceVersion);
    if (enumerateInstanceVersion) {
        enumerateInstanceVersion(&loaderVersion);
    }
    if (loaderVersion < VK_API_VERSION_1_1) {
        return fail(error, "Vulkan 1.1 or later is required by the compute shaders and NVRHI");
    }

    const std::vector<GpuAdapterInfo> adapters = enumerateGpuAdapters(GpuApi::Vulkan);
    if (adapters.empty()) {
        return fail(error, "no usable Vulkan compute adapter was found");
    }
    const int selectedIndex = selectGpuAdapter(adapters, preference, GpuRole::Compute);
    if (selectedIndex < 0) {
        return fail(error, std::format("compute adapter preference '{}' did not match a usable Vulkan adapter",
                                       preference.empty() ? "auto" : preference));
    }
    adapterInfo = adapters[static_cast<size_t>(selectedIndex)];
    adapterTuning = ballStepTuningFor(adapterInfo);

    uint32_t instanceExtensionCount = 0;
    if (vulkanDispatcher().vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr) != VK_SUCCESS) {
        return fail(error, "vkEnumerateInstanceExtensionProperties failed");
    }
    std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
    if (vulkanDispatcher().vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data()) !=
        VK_SUCCESS) {
        return fail(error, "vkEnumerateInstanceExtensionProperties failed");
    }
    std::vector<const char*> enabledInstanceExtensions;
    VkInstanceCreateFlags instanceFlags = 0;
    if (hasExtension(instanceExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        enabledInstanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "Balls Vulkan physics";
    applicationInfo.apiVersion = minVersion(loaderVersion, VK_API_VERSION_1_3);
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.flags = instanceFlags;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledInstanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = enabledInstanceExtensions.data();
    const VkResult createInstanceResult = vulkanDispatcher().vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (createInstanceResult != VK_SUCCESS) {
        return fail(error, std::format("vkCreateInstance failed with VkResult {}", static_cast<int>(createInstanceResult)));
    }
    instanceDispatcher.init(instance, getInstanceProcAddr);
    vulkanDispatcher().init(vk::Instance(instance));

    uint32_t physicalDeviceCount = 0;
    VkResult result = instanceDispatcher.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
    if (result != VK_SUCCESS || physicalDeviceCount == 0) {
        return fail(error, "Vulkan instance has no physical adapters");
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    result = instanceDispatcher.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());
    if (result != VK_SUCCESS) {
        return fail(error, std::format("vkEnumeratePhysicalDevices failed with VkResult {}", static_cast<int>(result)));
    }
    physicalDevices.resize(physicalDeviceCount);

    for (size_t index = 0; index < physicalDevices.size(); ++index) {
        VkPhysicalDeviceProperties properties{};
        instanceDispatcher.vkGetPhysicalDeviceProperties(physicalDevices[index], &properties);
        if (properties.vendorID == adapterInfo.vendorId && properties.deviceID == adapterInfo.deviceId &&
            adapterInfo.index == static_cast<int>(index)) {
            physicalDevice = physicalDevices[index];
            break;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        const auto match = std::ranges::find_if(physicalDevices, [&](VkPhysicalDevice candidate) {
            VkPhysicalDeviceProperties properties{};
            instanceDispatcher.vkGetPhysicalDeviceProperties(candidate, &properties);
            return properties.vendorID == adapterInfo.vendorId && properties.deviceID == adapterInfo.deviceId &&
                   adapterInfo.name == properties.deviceName;
        });
        if (match != physicalDevices.end()) {
            physicalDevice = *match;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        return fail(error, std::format("selected Vulkan adapter '{}' disappeared during device enumeration",
                                       adapterInfo.name));
    }

    uint32_t familyCount = 0;
    instanceDispatcher.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(familyCount);
    instanceDispatcher.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, queueFamilies.data());
    const auto computeFamily = std::ranges::find_if(queueFamilies, [](const VkQueueFamilyProperties& family) {
        return family.queueCount > 0 && (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    });
    if (computeFamily == queueFamilies.end()) {
        return fail(error, std::format("selected Vulkan adapter '{}' has no compute queue family", adapterInfo.name));
    }
    queueFamily = static_cast<uint32_t>(computeFamily - queueFamilies.begin());

    VkPhysicalDeviceProperties properties{};
    instanceDispatcher.vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    const uint32_t usableVersion = minVersion(applicationInfo.apiVersion, properties.apiVersion);
    const bool timelineCore = usableVersion >= VK_API_VERSION_1_2;
    const bool timelineExtension =
        usableVersion < VK_API_VERSION_1_2 &&
        hasDeviceExtension(instanceDispatcher, physicalDevice, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    if (!timelineCore && !timelineExtension) {
        return fail(error, std::format("selected Vulkan adapter '{}' lacks timeline semaphores required by NVRHI",
                                       adapterInfo.name));
    }
    const bool synchronization2Core = usableVersion >= VK_API_VERSION_1_3;
    const bool synchronization2Extension =
        !synchronization2Core &&
        hasDeviceExtension(instanceDispatcher, physicalDevice, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (!synchronization2Core && !synchronization2Extension) {
        return fail(error, std::format("selected Vulkan adapter '{}' lacks synchronization2 required by NVRHI",
                                       adapterInfo.name));
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures availableTimeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceSynchronization2Features availableSynchronization2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    availableTimeline.pNext = &availableSynchronization2;
    VkPhysicalDeviceFeatures2 availableFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    availableFeatures.pNext = &availableTimeline;
    instanceDispatcher.vkGetPhysicalDeviceFeatures2(physicalDevice, &availableFeatures);
    if (availableTimeline.timelineSemaphore != VK_TRUE) {
        return fail(error, std::format("selected Vulkan adapter '{}' does not support timeline semaphores required by NVRHI",
                                       adapterInfo.name));
    }
    if (availableSynchronization2.synchronization2 != VK_TRUE) {
        return fail(error, std::format("selected Vulkan adapter '{}' does not support synchronization2 required by NVRHI",
                                       adapterInfo.name));
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkPhysicalDeviceTimelineSemaphoreFeatures enabledTimeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    enabledTimeline.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceSynchronization2Features enabledSynchronization2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    enabledSynchronization2.synchronization2 = VK_TRUE;
    enabledTimeline.pNext = &enabledSynchronization2;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &enabledTimeline;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    std::vector<const char*> enabledDeviceExtensions;
    if (timelineExtension) {
        enabledDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    }
    if (synchronization2Extension) {
        enabledDeviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();
    result = instanceDispatcher.vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &logicalDevice);
    if (result != VK_SUCCESS) {
        return fail(error, std::format("vkCreateDevice for '{}' failed with VkResult {}", adapterInfo.name,
                                       static_cast<int>(result)));
    }
    deviceDispatcher.init(instance, getInstanceProcAddr, logicalDevice, instanceDispatcher.vkGetDeviceProcAddr);
    deviceDispatcher.vkGetDeviceQueue(logicalDevice, queueFamily, 0, &computeQueue);
    // NVRHI's static Vulkan build uses Vulkan-Hpp's default dispatcher.
    vulkanDispatcher().init(vk::Device(logicalDevice));

    messageCallback = std::make_unique<MessageCallback>();
    nvrhi::vulkan::DeviceDesc description;
    description.errorCB = messageCallback.get();
    description.instance = instance;
    description.physicalDevice = physicalDevice;
    description.device = logicalDevice;
    description.computeQueue = computeQueue;
    description.computeQueueIndex = static_cast<int>(queueFamily);
    nvrhiDevice = nvrhi::vulkan::createDevice(description);
    if (!nvrhiDevice) {
        const std::string nvrhiError = messageCallback->lastError.empty() ? "NVRHI createDevice returned null"
                                                                          : messageCallback->lastError;
        return fail(error, std::format("NVRHI initialization failed for '{}': {}", adapterInfo.name, nvrhiError));
    }
    return true;
}

std::string_view VulkanComputeDevice::lastMessage() const {
    return messageCallback ? std::string_view(messageCallback->lastError) : std::string_view{};
}

