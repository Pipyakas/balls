#include "TestFramework.h"

#include "gpu/GpuAdapters.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uint64_t mebibyte = 1024 * 1024;

std::vector<GpuAdapterInfo> sampleAdapters() {
    GpuAdapterInfo rtx;
    rtx.name = "NVIDIA GeForce RTX 3070 Laptop GPU";
    rtx.vendor = GpuVendor::Nvidia;
    rtx.vendorId = 0x10de;
    rtx.deviceId = 0x249d;
    rtx.kind = GpuKind::Discrete;
    rtx.subgroupMin = 32;
    rtx.subgroupMax = 32;
    rtx.deviceLocalBytes = 8192 * mebibyte;
    rtx.hasGraphicsQueue = true;
    rtx.hasComputeQueue = true;

    GpuAdapterInfo vega;
    vega.name = "AMD Vega iGPU";
    vega.vendor = GpuVendor::Amd;
    vega.vendorId = 0x1002;
    vega.deviceId = 0x1638;
    vega.kind = GpuKind::Integrated;
    vega.subgroupMin = 64;
    vega.subgroupMax = 64;
    vega.deviceLocalBytes = 512 * mebibyte;
    vega.hasGraphicsQueue = true;
    vega.hasComputeQueue = true;

    GpuAdapterInfo intel;
    intel.name = "Intel HD Graphics 620";
    intel.vendor = GpuVendor::Intel;
    intel.vendorId = 0x8086;
    intel.deviceId = 0x5916;
    intel.kind = GpuKind::Integrated;
    intel.subgroupMin = 8;
    intel.subgroupMax = 32;
    intel.hasGraphicsQueue = true;
    intel.hasComputeQueue = true;

    GpuAdapterInfo radeon;
    radeon.name = "AMD Radeon R5 M445";
    radeon.vendor = GpuVendor::Amd;
    radeon.vendorId = 0x1002;
    radeon.deviceId = 0x6900;
    radeon.kind = GpuKind::Discrete;
    radeon.subgroupMin = 64;
    radeon.subgroupMax = 64;
    radeon.deviceLocalBytes = 2048 * mebibyte;
    radeon.hasGraphicsQueue = true;
    radeon.hasComputeQueue = true;

    return {rtx, vega, intel, radeon};
}

}

TEST_CASE("GPU selection preferences honor usability and enumeration order") {
    const std::vector<GpuAdapterInfo> adapters = sampleAdapters();
    EXPECT(selectGpuAdapter(adapters, "auto", GpuRole::Render) == 0);
    EXPECT(selectGpuAdapter(adapters, "  AUTO  ", GpuRole::Compute) == 0);
    EXPECT(selectGpuAdapter(adapters, "integrated", GpuRole::Render) == 1);
    EXPECT(selectGpuAdapter(adapters, "discrete", GpuRole::Render) == 0);
    EXPECT(selectGpuAdapter(adapters, "3", GpuRole::Render) == 3);
    EXPECT(selectGpuAdapter(adapters, "4", GpuRole::Render) == -1);
    EXPECT(selectGpuAdapter(adapters, "radeon", GpuRole::Render) == 3);
    EXPECT(selectGpuAdapter(adapters, "RTX", GpuRole::Render) == 0);
}

TEST_CASE("GPU selection skips software adapters and respects compute-only devices") {
    std::vector<GpuAdapterInfo> adapters = sampleAdapters();
    GpuAdapterInfo software;
    software.name = "Software Rasterizer";
    software.kind = GpuKind::Software;
    software.hasGraphicsQueue = true;
    software.hasComputeQueue = true;
    adapters.push_back(software);
    GpuAdapterInfo computeOnly;
    computeOnly.name = "Compute-only accelerator";
    computeOnly.kind = GpuKind::Discrete;
    computeOnly.deviceLocalBytes = 16384 * mebibyte;
    computeOnly.hasComputeQueue = true;
    adapters.push_back(computeOnly);

    EXPECT(selectGpuAdapter(adapters, "auto", GpuRole::Render) == 0);
    EXPECT(selectGpuAdapter(adapters, "5", GpuRole::Render) == -1);
    EXPECT(selectGpuAdapter(adapters, "compute-only", GpuRole::Render) == -1);
    EXPECT(selectGpuAdapter(adapters, "auto", GpuRole::Compute) == 5);
    EXPECT(selectGpuAdapter(adapters, "5", GpuRole::Compute) == 5);
    EXPECT(selectGpuAdapter(adapters, "Software Rasterizer", GpuRole::Compute) == -1);
    EXPECT(selectGpuAdapter(adapters, "software rasterizer", GpuRole::Render) == 4);
    EXPECT(selectGpuAdapter(adapters, "4", GpuRole::Render) == 4);
}

TEST_CASE("auto rendering falls back to a software rasterizer only when nothing else can render") {
    GpuAdapterInfo software;
    software.name = "Microsoft Basic Render Driver";
    software.kind = GpuKind::Software;
    software.hasGraphicsQueue = true;
    std::vector<GpuAdapterInfo> adapters{software};
    EXPECT(selectGpuAdapter(adapters, "auto", GpuRole::Render) == 0);
    GpuAdapterInfo virtualAdapter;
    virtualAdapter.name = "Virtual GPU";
    virtualAdapter.kind = GpuKind::Virtual;
    virtualAdapter.hasGraphicsQueue = true;
    adapters.push_back(virtualAdapter);
    EXPECT(selectGpuAdapter(adapters, "auto", GpuRole::Render) == 1);
}

TEST_CASE("GPU PCI vendor mapping covers known vendors and unknown IDs") {
    EXPECT(vendorFromPciId(0x10de) == GpuVendor::Nvidia);
    EXPECT(vendorFromPciId(0x1002) == GpuVendor::Amd);
    EXPECT(vendorFromPciId(0x1022) == GpuVendor::Amd);
    EXPECT(vendorFromPciId(0x8086) == GpuVendor::Intel);
    EXPECT(vendorFromPciId(0x8087) == GpuVendor::Intel);
    EXPECT(vendorFromPciId(0x106b) == GpuVendor::Apple);
    EXPECT(vendorFromPciId(0x5143) == GpuVendor::Qualcomm);
    EXPECT(vendorFromPciId(0x13b5) == GpuVendor::Arm);
    EXPECT(vendorFromPciId(0x1234) == GpuVendor::Other);
    EXPECT(vendorFromPciId(0) == GpuVendor::Unknown);
    EXPECT(gpuVendorName(GpuVendor::Nvidia) == "NVIDIA");
    EXPECT(gpuVendorName(GpuVendor::Amd) == "AMD");
    EXPECT(gpuKindName(GpuKind::Discrete) == "discrete");
    EXPECT(gpuKindName(GpuKind::Unknown) == "unknown");
}

TEST_CASE("ball step tuning keeps default numbers and selects a wave variant") {
    GpuAdapterInfo adapter;
    adapter.subgroupMax = 32;
    BallStepTuning tuning = ballStepTuningFor(adapter);
    EXPECT(tuning.variant == "wave32");
    EXPECT(tuning.threadsPerGroup == 256);
    EXPECT(tuning.ballsPerThread == 4);
    EXPECT(tuning.countThreads == 256);
    EXPECT(tuning.countItems == 16);
    EXPECT(tuning.sortThreads == 512);
    EXPECT(tuning.sortItems == 8);

    adapter.subgroupMax = 64;
    EXPECT(ballStepTuningFor(adapter).variant == "wave64");
    adapter.subgroupMax = 0;
    EXPECT(ballStepTuningFor(adapter).variant == "wave32");
}

TEST_CASE("GPU adapter summary includes kind wave size and local memory") {
    const GpuAdapterInfo adapter = sampleAdapters().front();
    EXPECT(adapter.summary() == "NVIDIA GeForce RTX 3070 Laptop GPU (discrete, wave 32, 8192 MB)");

    GpuAdapterInfo unknownWave;
    unknownWave.name = "Unknown adapter";
    unknownWave.kind = GpuKind::Integrated;
    unknownWave.deviceLocalBytes = 512 * mebibyte;
    EXPECT(unknownWave.summary() == "Unknown adapter (integrated, wave ?, 512 MB)");
}

TEST_CASE("Vulkan enumeration returns adapter indices in physical-device order") {
    const std::vector<GpuAdapterInfo> adapters = enumerateGpuAdapters(GpuApi::Vulkan);
    for (size_t index = 0; index < adapters.size(); ++index) {
        EXPECT(adapters[index].index == static_cast<int>(index));
    }
    std::printf("Vulkan adapters:\n");
    for (const GpuAdapterInfo& adapter : adapters) {
        const std::string summary = adapter.summary();
        std::printf("  GPU%d: %s; subgroup %u-%u, shared %u, shaderInt64 %s, driver %s\n", adapter.index,
                    summary.c_str(), adapter.subgroupMin, adapter.subgroupMax, adapter.maxSharedMemoryBytes,
                    adapter.shaderInt64 ? "true" : "false", adapter.driver.c_str());
    }

    const std::vector<GpuAdapterInfo> d3d12Adapters = enumerateGpuAdapters(GpuApi::D3d12);
    std::printf("D3D12 adapters:\n");
    for (const GpuAdapterInfo& adapter : d3d12Adapters) {
        const std::string summary = adapter.summary();
        std::printf("  GPU%d: %s; subgroup %u-%u, local %llu bytes\n", adapter.index, summary.c_str(),
                    adapter.subgroupMin, adapter.subgroupMax,
                    static_cast<unsigned long long>(adapter.deviceLocalBytes));
    }
}
