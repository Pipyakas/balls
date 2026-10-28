#pragma once

#include "sim/BackendFactory.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// One backend kind a physical device can run, and the device's index inside that kind's plugin.
struct DeviceBackendOption {
    BackendKind kind = BackendKind::Scalar;
    int pluginDevice = 0;
};

// One piece of hardware the physics can run on: the CPU, or a GPU seen through CUDA, Vulkan or both. The same GPU
// listed by two plugins is one device here, matched by UUID, so a selection can give it exactly one backend.
struct PhysicalDevice {
    // "cpu", or "gpu0", "gpu1", ... with GPUs ordered by PCI location (enumeration order where it is unknown).
    std::string id;
    std::string name;
    PhysicsDeviceType type = PhysicsDeviceType::Cpu;
    std::array<uint8_t, 16> uuid{};
    bool hasUuid = false;
    uint64_t memoryBytes = 0;
    // Fastest first: AVX2 before Scalar, CUDA before Vulkan.
    std::vector<DeviceBackendOption> backends;

    bool isGpu() const { return type != PhysicsDeviceType::Cpu && type != PhysicsDeviceType::Npu; }
    const DeviceBackendOption* option(BackendKind kind) const;
};

// How menus name a device: "CPU", the GPU's name, and " (iGPU)" after an integrated one.
std::string physicalDeviceLabel(const PhysicalDevice& device);

// Every physical device any plugin reports, the CPU first. Listed once per run: plugins open drivers to answer.
const std::vector<PhysicalDevice>& physicalDevices();
// Builds the list from per-kind plugin answers; physicalDevices() uses it, tests call it directly.
std::vector<PhysicalDevice> mergePhysicalDevices(
    const std::vector<std::pair<BackendKind, std::vector<PhysicsDeviceInfo>>>& kindDevices, std::string_view cpuName);
// "cpu", "gpu1", a UUID hex prefix of 8+ digits, or a case-insensitive substring of the device name.
const PhysicalDevice* findPhysicalDevice(std::string_view token, const std::vector<PhysicalDevice>& devices);
const PhysicalDevice* findPhysicalDevice(std::string_view token);
// The device a kind runs on when no device is named: the CPU for CPU kinds, the Vulkan plugin's compute GPU
// preference for Vulkan, CUDA's first device for CUDA. Null when the kind has no device here.
const PhysicalDevice* defaultDeviceFor(BackendKind kind);
std::string uuidText(const std::array<uint8_t, 16>& uuid);
// The processor's brand string ("AMD Ryzen 9 5900HS ..."), or "unknown CPU".
std::string cpuBrandName();
