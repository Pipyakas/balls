#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class GpuApi { Vulkan, D3d12 };

enum class GpuKind { Unknown, Integrated, Discrete, Virtual, Software };

enum class GpuVendor { Unknown, Nvidia, Amd, Intel, Apple, Qualcomm, Arm, Other };

enum class GpuRole { Render, Compute };

// What one graphics adapter can do, read through the API's own queries (never from the name).
// Fields the API cannot answer stay zero/false, so tuning code must treat 0 as "unknown".
struct GpuAdapterInfo {
    GpuApi api = GpuApi::Vulkan;
    int index = 0;  // position in this API's enumeration order (what --gpu N and --compute-gpu N mean)
    std::string name;
    std::string driver;
    GpuVendor vendor = GpuVendor::Unknown;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    GpuKind kind = GpuKind::Unknown;
    uint32_t apiVersion = 0;  // Vulkan: packed VK_API_VERSION; D3D12: feature level (0xc000 = 12_0)
    uint32_t subgroupMin = 0;  // lanes per subgroup/wave: Vulkan minSubgroupSize, D3D12 WaveLaneCountMin
    uint32_t subgroupMax = 0;
    uint32_t maxSharedMemoryBytes = 0;
    uint32_t maxWorkgroupInvocations = 0;
    uint64_t deviceLocalBytes = 0;  // largest device-local heap (D3D12: DedicatedVideoMemory)
    bool shaderInt64 = false;
    bool shaderInt16 = false;
    bool shaderFloat16 = false;
    bool atomicFloat = false;
    bool hasGraphicsQueue = false;
    bool hasComputeQueue = false;

    bool usableForCompute() const { return hasComputeQueue && kind != GpuKind::Software; }
    // Software rasterizers (WARP, lavapipe) can render: on a machine whose GPU is busy with physics that is a real
    // option, so they are usable when picked explicitly. "auto" only falls back to them when nothing else renders.
    bool usableForRender() const { return hasGraphicsQueue; }
    // "NVIDIA GeForce RTX 3070 Laptop GPU (discrete, wave 32, 8192 MB)" for logs and menus.
    std::string summary() const;
};

// Every adapter the API exposes, in enumeration order. Loads the Vulkan loader / DXGI dynamically and returns an
// empty list (never throws, never aborts) when the API is missing. Enumerating opens a device per adapter, about
// 200 ms, so the first list per API is kept for the rest of the run; safe to call from any thread.
std::vector<GpuAdapterInfo> enumerateGpuAdapters(GpuApi api);

GpuVendor vendorFromPciId(uint32_t vendorId);
std::string_view gpuVendorName(GpuVendor vendor);
std::string_view gpuKindName(GpuKind kind);

// preference: "" / "auto", "integrated", "discrete", a decimal index into `adapters`, or a case-insensitive
// substring of the adapter name. Returns an index into `adapters`, or -1 when nothing usable for `role` matches.
// "auto" prefers a discrete adapter with the most device-local memory, then integrated, for both roles.
int selectGpuAdapter(std::span<const GpuAdapterInfo> adapters, std::string_view preference, GpuRole role);

// Pure function of the capability record, so it runs in unit tests without a GPU. The profile table lives in
// source/core/gpu/BallStepProfiles.def; scripts/CompileBallStep.py compiles one shader blob set per row.
struct BallStepTuning {
    std::string_view variant;  // blob set name, e.g. "wave32"
    uint32_t threadsPerGroup = 256;
    uint32_t ballsPerThread = 4;
    uint32_t countThreads = 256;
    uint32_t countItems = 16;
    uint32_t sortThreads = 512;
    uint32_t sortItems = 8;
};

std::span<const BallStepTuning> ballStepProfiles();
BallStepTuning ballStepTuningFor(const GpuAdapterInfo& adapter);
