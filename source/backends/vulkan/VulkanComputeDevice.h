#pragma once

#include "common/DynamicLibrary.h"
#include "gpu/GpuAdapters.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <string>
#include <string_view>

// The plugin's one VkInstance. NVRHI's static Vulkan build calls through Vulkan-Hpp's global default dispatcher,
// which is initialised here at instance level only and never re-initialised for a device: device functions then
// resolve to the loader's trampolines, which dispatch on the VkDevice handle, so devices from different drivers
// (an NVIDIA and an AMD GPU) can be used side by side and from several threads.
class VulkanInstance final {
public:
    static std::shared_ptr<VulkanInstance> create(std::string& error);
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    VkInstance handle() const { return instance; }
    uint32_t apiVersion() const { return version; }
    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const { return getProcAddr; }
    const vk::detail::DispatchLoaderDynamic& dispatcher() const { return instanceDispatcher; }

private:
    VulkanInstance() = default;
    bool initialize(std::string& error);

    std::unique_ptr<DynamicLibrary> loader;
    PFN_vkGetInstanceProcAddr getProcAddr = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    uint32_t version = VK_API_VERSION_1_0;
    vk::detail::DispatchLoaderDynamic instanceDispatcher;
};

// One logical device with a compute queue on one physical GPU, wrapped in an NVRHI device.
class VulkanComputeDevice final {
public:
    static std::shared_ptr<VulkanComputeDevice> create(std::shared_ptr<VulkanInstance> instance,
                                                       const GpuAdapterInfo& adapter, std::string& error);
    ~VulkanComputeDevice();

    VulkanComputeDevice(const VulkanComputeDevice&) = delete;
    VulkanComputeDevice& operator=(const VulkanComputeDevice&) = delete;

    nvrhi::IDevice* device() const { return nvrhiDevice; }
    const GpuAdapterInfo& adapter() const { return adapterInfo; }
    BallStepTuning tuning() const { return adapterTuning; }
    std::string_view lastMessage() const;

private:
    class MessageCallback;

    VulkanComputeDevice() = default;
    bool initialize(const GpuAdapterInfo& adapter, std::string& error);
    bool fail(std::string& error, std::string message);

    std::shared_ptr<VulkanInstance> vulkan;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice logicalDevice = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    vk::detail::DispatchLoaderDynamic deviceDispatcher;
    uint32_t queueFamily = 0;
    nvrhi::vulkan::DeviceHandle nvrhiDevice;
    std::unique_ptr<MessageCallback> messageCallback;
    GpuAdapterInfo adapterInfo;
    BallStepTuning adapterTuning;
};
