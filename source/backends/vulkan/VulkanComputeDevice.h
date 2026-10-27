#pragma once

#include "common/DynamicLibrary.h"
#include "gpu/GpuAdapters.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <string>
#include <string_view>

class VulkanComputeDevice final {
public:
    static std::shared_ptr<VulkanComputeDevice> create(std::string_view preference, std::string& error);
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
    bool initialize(std::string_view preference, std::string& error);
    bool fail(std::string& error, std::string message);

    std::unique_ptr<DynamicLibrary> loader;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice logicalDevice = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    nvrhi::vulkan::DeviceHandle nvrhiDevice;
    std::unique_ptr<MessageCallback> messageCallback;
    GpuAdapterInfo adapterInfo;
    BallStepTuning adapterTuning;
};
