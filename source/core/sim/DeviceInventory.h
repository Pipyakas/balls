#pragma once

#include "sim/BackendFactory.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct DeviceBackendOption {
    BackendKind kind = BackendKind::Scalar;
    int pluginDevice = 0;
};

struct PhysicalDevice {

    std::string id;
    std::string name;
    PhysicsDeviceType type = PhysicsDeviceType::Cpu;
    std::array<uint8_t, 16> uuid{};
    bool hasUuid = false;
    uint64_t memoryBytes = 0;

    std::vector<DeviceBackendOption> backends;

    bool isGpu() const { return type != PhysicsDeviceType::Cpu && type != PhysicsDeviceType::Npu; }
    const DeviceBackendOption* option(BackendKind kind) const;
};

std::string physicalDeviceLabel(const PhysicalDevice& device);

const std::vector<PhysicalDevice>& physicalDevices();

std::vector<PhysicalDevice> mergePhysicalDevices(
    const std::vector<std::pair<BackendKind, std::vector<PhysicsDeviceInfo>>>& kindDevices, std::string_view cpuName);

const PhysicalDevice* findPhysicalDevice(std::string_view token, const std::vector<PhysicalDevice>& devices);
const PhysicalDevice* findPhysicalDevice(std::string_view token);

const PhysicalDevice* defaultDeviceFor(BackendKind kind);
std::string uuidText(const std::array<uint8_t, 16>& uuid);

std::string cpuBrandName();
