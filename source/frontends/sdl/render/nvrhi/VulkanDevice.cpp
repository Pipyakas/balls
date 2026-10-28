#include "render/nvrhi/GraphicsDevice.h"
#include "render/nvrhi/MessageLog.h"

#define NOMINMAX
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <nvrhi/vulkan.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <format>
#include <string>
#include <system_error>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

constexpr uint32_t preferredImageCount = 3;
constexpr uint64_t acquireTimeoutNanoseconds = 1'000'000'000;
constexpr const char* portabilityEnumerationExtension = "VK_KHR_portability_enumeration";
constexpr const char* portabilitySubsetExtension = "VK_KHR_portability_subset";

auto& vk() { return VULKAN_HPP_DEFAULT_DISPATCHER; }

bool hasExtension(const std::vector<VkExtensionProperties>& available, const char* name) {
    return std::ranges::any_of(available, [&](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

const char* deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "software";
    default:
        return "other";
    }
}

std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

class VulkanDevice final : public GraphicsDevice {
public:
    VulkanDevice(SDL_Window* window, std::string gpuPreference)
        : window(window), gpuPreference(std::move(gpuPreference)) {}
    ~VulkanDevice() override;

    bool initialize(std::string& error);
    nvrhi::IDevice* device() const override { return nvrhiDevice; }
    bool attach(std::string& error) override;
    void detach() override { destroySwapchain(); }
    bool beginFrame() override;
    nvrhi::ITexture* backBuffer() const override { return backBuffers[imageIndex]; }
    void present() override;
    bool setVsync(bool enabled) override;
    bool isVsyncEnabled() const override { return vsync; }
    nvrhi::Format backBufferFormat() const override { return nvrhiFormat; }
    std::string description() const override { return std::format("Vulkan / {}", adapterName); }

private:
    bool createInstance(std::string& error);
    bool pickPhysicalDevice(std::string& error);
    bool createLogicalDevice(std::string& error);
    bool createSwapchain();
    void destroySwapchain();
    VkPresentModeKHR choosePresentMode() const;

    SDL_Window* window;
    std::string gpuPreference;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice logicalDevice = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    nvrhi::Format nvrhiFormat = nvrhi::Format::BGRA8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<nvrhi::TextureHandle> backBuffers;
    std::vector<VkSemaphore> acquireSemaphores;
    std::vector<VkSemaphore> presentSemaphores;
    size_t acquireCursor = 0;
    uint32_t imageIndex = 0;
    std::vector<const char*> instanceExtensions;
    std::vector<const char*> deviceExtensions;
    nvrhi::RefCountPtr<nvrhi::vulkan::IDevice> nvrhiDevice;
    nvrhi::CommandListHandle signalList;
    std::string adapterName;
    bool vsync = true;
    bool swapchainDirty = false;
};

VulkanDevice::~VulkanDevice() {
    if (logicalDevice) {
        vk().vkDeviceWaitIdle(logicalDevice);
    }
    destroySwapchain();
    signalList = nullptr;
    nvrhiDevice = nullptr;
    if (logicalDevice) {
        vk().vkDestroyDevice(logicalDevice, nullptr);
    }
    if (surface) {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    }
    if (instance) {
        vk().vkDestroyInstance(instance, nullptr);
    }
}

bool VulkanDevice::createInstance(std::string& error) {
    const auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!getInstanceProcAddr) {
        error = std::format("Vulkan loader unavailable: {}", SDL_GetError());
        return false;
    }
    vk().init(getInstanceProcAddr);
    Uint32 sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    instanceExtensions.assign(sdlExtensions, sdlExtensions + sdlExtensionCount);
    uint32_t availableCount = 0;
    vk().vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vk().vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    VkInstanceCreateFlags flags = 0;
    if (hasExtension(available, portabilityEnumerationExtension)) {
        instanceExtensions.push_back(portabilityEnumerationExtension);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Balls";
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.flags = flags;
    createInfo.pApplicationInfo = &application;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();
    if (vk().vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        error = "vkCreateInstance failed (Vulkan 1.3 required)";
        return false;
    }
    vk().init(vk::Instance(instance));
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        error = std::format("SDL_Vulkan_CreateSurface: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool VulkanDevice::pickPhysicalDevice(std::string& error) {
    struct UsableDevice {
        VkPhysicalDevice device = VK_NULL_HANDLE;
        uint32_t family = 0;
        std::string name;
        VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    };
    std::vector<UsableDevice> usable;
    std::string listing;

    uint32_t count = 0;
    vk().vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> candidates(count);
    vk().vkEnumeratePhysicalDevices(instance, &count, candidates.data());
    for (VkPhysicalDevice candidate : candidates) {
        VkPhysicalDeviceProperties properties{};
        vk().vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }
        uint32_t familyCount = 0;
        vk().vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vk().vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            VkBool32 presentable = VK_FALSE;
            vk().vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &presentable);
            const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if (!presentable || (families[family].queueFlags & needed) != needed) {
                continue;
            }
            usable.push_back({candidate, family, properties.deviceName, properties.deviceType});
            if (!listing.empty()) {
                listing += ", ";
            }
            listing += std::format("{} {} ({})", usable.size() - 1, properties.deviceName,
                                   deviceTypeName(properties.deviceType));
            break;
        }
    }
    if (usable.empty()) {
        error = "no Vulkan 1.3 GPU with a graphics+compute queue that can present to the window";
        return false;
    }
    SDL_Log("Vulkan devices: %s", listing.c_str());

    // An index or a name substring pins one adapter; otherwise the requested class wins, and "auto"
    // keeps the historical preference for a discrete GPU.
    const std::string preference = gpuPreference.empty() ? std::string("auto") : gpuPreference;
    size_t selected = usable.size();
    int wantedIndex = -1;
    const auto [scanEnd, scanError] =
        std::from_chars(preference.data(), preference.data() + preference.size(), wantedIndex);
    const bool isIndex = scanError == std::errc{} && scanEnd == preference.data() + preference.size() && wantedIndex >= 0;
    if (isIndex && static_cast<size_t>(wantedIndex) < usable.size()) {
        selected = static_cast<size_t>(wantedIndex);
    }
    if (selected == usable.size() && !isIndex && preference != "auto" && preference != "integrated" &&
        preference != "discrete") {
        for (size_t index = 0; index < usable.size(); ++index) {
            if (lowercase(usable[index].name).find(preference) != std::string::npos) {
                selected = index;
                break;
            }
        }
    }
    if (selected == usable.size()) {
        const VkPhysicalDeviceType wanted = preference == "integrated" ? VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                                                                      : VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        for (size_t index = 0; index < usable.size(); ++index) {
            if (usable[index].type == wanted) {
                selected = index;
                break;
            }
        }
    }
    if (selected == usable.size()) {
        if (preference != "auto") {
            SDL_Log("No Vulkan adapter matches '%s'; using device 0", preference.c_str());
        }
        selected = 0;
    }

    const UsableDevice& chosen = usable[selected];
    physicalDevice = chosen.device;
    queueFamily = chosen.family;
    adapterName = chosen.name;
    SDL_Log("Vulkan rendering on device %zu: %s (%s)", selected, adapterName.c_str(), deviceTypeName(chosen.type));
    return true;
}

bool VulkanDevice::createLogicalDevice(std::string& error) {
    uint32_t availableCount = 0;
    vk().vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vk().vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableCount, available.data());
    deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (hasExtension(available, portabilitySubsetExtension)) {
        deviceExtensions.push_back(portabilitySubsetExtension);
    }
    VkPhysicalDeviceVulkan13Features vulkan13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    vulkan13.dynamicRendering = VK_TRUE;
    vulkan13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    vulkan12.timelineSemaphore = VK_TRUE;
    vulkan12.pNext = &vulkan13;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &vulkan12;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    if (vk().vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice) != VK_SUCCESS) {
        error = "vkCreateDevice failed (needs dynamic rendering, synchronization2 and timeline semaphores)";
        return false;
    }
    vk().init(vk::Device(logicalDevice));
    vk().vkGetDeviceQueue(logicalDevice, queueFamily, 0, &queue);

    nvrhi::vulkan::DeviceDesc description;
    description.errorCB = &MessageLog::instance();
    description.instance = instance;
    description.physicalDevice = physicalDevice;
    description.device = logicalDevice;
    description.graphicsQueue = queue;
    description.graphicsQueueIndex = static_cast<int>(queueFamily);
    description.instanceExtensions = instanceExtensions.data();
    description.numInstanceExtensions = instanceExtensions.size();
    description.deviceExtensions = deviceExtensions.data();
    description.numDeviceExtensions = deviceExtensions.size();
    nvrhiDevice = static_cast<nvrhi::vulkan::IDevice*>(nvrhi::vulkan::createDevice(description).Get());
    if (!nvrhiDevice) {
        error = "nvrhi::vulkan::createDevice failed";
        return false;
    }
    signalList = nvrhiDevice->createCommandList();
    return true;
}

VkPresentModeKHR VulkanDevice::choosePresentMode() const {
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    uint32_t count = 0;
    vk().vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vk().vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());
    for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
        if (std::ranges::find(modes, preferred) != modes.end()) {
            return preferred;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

bool VulkanDevice::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    vk().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    extent = capabilities.currentExtent.width != UINT32_MAX
                 ? capabilities.currentExtent
                 : VkExtent2D{std::clamp(static_cast<uint32_t>(pixelWidth), capabilities.minImageExtent.width,
                                         capabilities.maxImageExtent.width),
                              std::clamp(static_cast<uint32_t>(pixelHeight), capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height)};
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }
    uint32_t formatCount = 0;
    vk().vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vk().vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            swapchainFormat = format.format;
            colorSpace = format.colorSpace;
            break;
        }
    }
    nvrhiFormat = swapchainFormat == VK_FORMAT_R8G8B8A8_UNORM ? nvrhi::Format::RGBA8_UNORM : nvrhi::Format::BGRA8_UNORM;
    uint32_t imageCount = std::max(preferredImageCount, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = swapchainFormat;
    createInfo.imageColorSpace = colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = choosePresentMode();
    createInfo.clipped = VK_TRUE;
    if (vk().vkCreateSwapchainKHR(logicalDevice, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        return false;
    }
    uint32_t actualCount = 0;
    vk().vkGetSwapchainImagesKHR(logicalDevice, swapchain, &actualCount, nullptr);
    images.resize(actualCount);
    vk().vkGetSwapchainImagesKHR(logicalDevice, swapchain, &actualCount, images.data());
    backBuffers.clear();
    for (VkImage image : images) {
        nvrhi::TextureDesc description;
        description.width = extent.width;
        description.height = extent.height;
        description.format = nvrhiFormat;
        description.debugName = "back buffer";
        description.initialState = nvrhi::ResourceStates::Present;
        description.keepInitialState = true;
        description.isRenderTarget = true;
        backBuffers.push_back(nvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image,
                                                                        nvrhi::Object(image), description));
    }
    const VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    acquireSemaphores.resize(actualCount + 1);
    presentSemaphores.resize(actualCount);
    for (VkSemaphore& semaphore : acquireSemaphores) {
        vk().vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &semaphore);
    }
    for (VkSemaphore& semaphore : presentSemaphores) {
        vk().vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &semaphore);
    }
    swapchainDirty = false;
    return true;
}

void VulkanDevice::destroySwapchain() {
    if (!swapchain) {
        return;
    }
    vk().vkDeviceWaitIdle(logicalDevice);
    backBuffers.clear();
    if (nvrhiDevice) {
        nvrhiDevice->runGarbageCollection();
    }
    for (VkSemaphore semaphore : acquireSemaphores) {
        vk().vkDestroySemaphore(logicalDevice, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : presentSemaphores) {
        vk().vkDestroySemaphore(logicalDevice, semaphore, nullptr);
    }
    acquireSemaphores.clear();
    presentSemaphores.clear();
    vk().vkDestroySwapchainKHR(logicalDevice, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool VulkanDevice::initialize(std::string& error) {
    return createInstance(error) && pickPhysicalDevice(error) && createLogicalDevice(error);
}

bool VulkanDevice::attach(std::string& error) {
    if (!createSwapchain()) {
        error = "vkCreateSwapchainKHR failed";
        return false;
    }
    return true;
}

bool VulkanDevice::beginFrame() {
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return false;
    }
    if (swapchainDirty || static_cast<uint32_t>(pixelWidth) != extent.width ||
        static_cast<uint32_t>(pixelHeight) != extent.height) {
        destroySwapchain();
        if (!createSwapchain()) {
            return false;
        }
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        VkSemaphore semaphore = acquireSemaphores[acquireCursor];
        const VkResult result = vk().vkAcquireNextImageKHR(logicalDevice, swapchain, acquireTimeoutNanoseconds,
                                                           semaphore, VK_NULL_HANDLE, &imageIndex);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            acquireCursor = (acquireCursor + 1) % acquireSemaphores.size();
            nvrhiDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
            return true;
        }
        if (result != VK_ERROR_OUT_OF_DATE_KHR) {
            return false;
        }
        destroySwapchain();
        if (!createSwapchain()) {
            return false;
        }
    }
    return false;
}

void VulkanDevice::present() {
    VkSemaphore semaphore = presentSemaphores[imageIndex];
    nvrhiDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
    signalList->open();
    signalList->close();
    nvrhiDevice->executeCommandList(signalList);
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult result = vk().vkQueuePresentKHR(queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchainDirty = true;
    }
}

bool VulkanDevice::setVsync(bool enabled) {
    if (enabled != vsync) {
        vsync = enabled;
        swapchainDirty = true;
    }
    return true;
}

}

std::unique_ptr<GraphicsDevice> createVulkanGraphicsDevice(SDL_Window* window, std::string_view gpuPreference,
                                                           std::string& error) {
    auto device = std::make_unique<VulkanDevice>(window, std::string(gpuPreference));
    if (!device->initialize(error)) {
        return nullptr;
    }
    return device;
}
