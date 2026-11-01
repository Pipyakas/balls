#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class GpuApi { Vulkan, D3d12 };

enum class GpuKind { Unknown, Integrated, Discrete, Virtual, Software };

enum class GpuVendor { Unknown, Nvidia, Amd, Intel, Apple, Qualcomm, Arm, Other };

enum class GpuRole { Render, Compute };

struct GpuAdapterInfo {
    GpuApi api = GpuApi::Vulkan;
    int index = 0;
    std::string name;
    std::string driver;
    GpuVendor vendor = GpuVendor::Unknown;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    GpuKind kind = GpuKind::Unknown;
    uint32_t apiVersion = 0;
    uint32_t subgroupMin = 0;
    uint32_t subgroupMax = 0;
    uint32_t maxSharedMemoryBytes = 0;
    uint32_t maxWorkgroupInvocations = 0;
    uint64_t deviceLocalBytes = 0;
    bool shaderInt64 = false;
    bool shaderInt16 = false;
    bool shaderFloat16 = false;
    bool atomicFloat = false;
    bool hasGraphicsQueue = false;
    bool hasComputeQueue = false;

    std::array<uint8_t, 16> uuid{};
    bool hasUuid = false;

    int pciDomain = -1;
    int pciBus = -1;
    int pciDevice = -1;

    bool usableForCompute() const { return hasComputeQueue && kind != GpuKind::Software; }

    bool usableForRender() const { return hasGraphicsQueue; }

    std::string summary() const;
};

std::vector<GpuAdapterInfo> enumerateGpuAdapters(GpuApi api);

GpuVendor vendorFromPciId(uint32_t vendorId);
std::string_view gpuVendorName(GpuVendor vendor);
std::string_view gpuKindName(GpuKind kind);

int selectGpuAdapter(std::span<const GpuAdapterInfo> adapters, std::string_view preference, GpuRole role);

struct BallStepTuning {
    std::string_view variant;
    uint32_t threadsPerGroup = 256;
    uint32_t ballsPerThread = 4;
    uint32_t countThreads = 256;
    uint32_t countItems = 16;
    uint32_t sortThreads = 512;
    uint32_t sortItems = 8;
};

std::span<const BallStepTuning> ballStepProfiles();
BallStepTuning ballStepTuningFor(const GpuAdapterInfo& adapter);
