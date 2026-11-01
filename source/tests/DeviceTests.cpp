#include "TestFramework.h"

#include "sim/DeviceInventory.h"

#include <algorithm>
#include <cstring>

namespace {

PhysicsDeviceInfo cpuInfo() { return PhysicsDeviceInfo{}; }

PhysicsDeviceInfo gpuInfo(const char* name, uint8_t uuidByte, int pciBus) {
    PhysicsDeviceInfo info;
    std::memcpy(info.name, name, std::min(std::strlen(name), sizeof(info.name) - 1));
    info.uuid[0] = uuidByte;
    info.hasUuid = 1;
    info.type = PhysicsDeviceType::DiscreteGpu;
    info.pciDomain = 0;
    info.pciBus = pciBus;
    info.pciDevice = 0;
    return info;
}

std::vector<PhysicalDevice> twoGpuMachine() {

    return mergePhysicalDevices({{BackendKind::Avx2, {cpuInfo()}},
                                 {BackendKind::Scalar, {cpuInfo()}},
                                 {BackendKind::Cuda, {gpuInfo("NVIDIA GeForce RTX 2060", 0x11, 1)}},
                                 {BackendKind::Vulkan, {gpuInfo("AMD Radeon RX 6700 XT", 0x22, 3),
                                                        gpuInfo("NVIDIA GeForce RTX 2060", 0x11, 1)}}},
                                "Test CPU");
}

}

TEST_CASE("one GPU seen by CUDA and Vulkan is one physical device") {
    const std::vector<PhysicalDevice> devices = twoGpuMachine();
    EXPECT(devices.size() == 3);
    EXPECT(devices[0].id == "cpu" && devices[0].name == "Test CPU");
    EXPECT(devices[0].backends.size() == 2 && devices[0].backends[0].kind == BackendKind::Avx2);

    EXPECT(devices[1].id == "gpu0" && devices[1].name == "NVIDIA GeForce RTX 2060");
    EXPECT(devices[1].backends.size() == 2);
    EXPECT(devices[1].backends[0].kind == BackendKind::Cuda && devices[1].backends[0].pluginDevice == 0);
    EXPECT(devices[1].backends[1].kind == BackendKind::Vulkan && devices[1].backends[1].pluginDevice == 1);
    EXPECT(devices[2].id == "gpu1" && devices[2].option(BackendKind::Cuda) == nullptr);
    EXPECT(devices[2].option(BackendKind::Vulkan)->pluginDevice == 0);
}

TEST_CASE("devices are found by id, UUID prefix or name") {
    const std::vector<PhysicalDevice> devices = twoGpuMachine();
    EXPECT(findPhysicalDevice("GPU1", devices) == &devices[2]);
    EXPECT(findPhysicalDevice("rx 6700", devices) == &devices[2]);
    EXPECT(findPhysicalDevice("11000000", devices) == &devices[1]);
    EXPECT(findPhysicalDevice("cpu", devices) == &devices[0]);
    EXPECT(findPhysicalDevice("gpu7", devices) == nullptr);
    EXPECT(findPhysicalDevice("", devices) == nullptr);
}
