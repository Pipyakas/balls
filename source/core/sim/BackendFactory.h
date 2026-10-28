#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// CudaAvx2 is the fixed CUDA + AVX2 split (HybridBackend over those two); it was called "Hybrid" before the
// general device composer took that name. Hybrid is the composer (DeviceComposer): one backend on each device the
// player picked (BackendSelection.h).
// New kinds go at the end: the values are compared and stored by name, but keep the old numbering stable anyway.
// Zen4 is the AVX-512 CPU backend; ROCm drives AMD GPUs through the HSA runtime; VulkanMulti is one Vulkan backend
// that spans several GPUs itself (unlike Hybrid, which composes one backend per device).
enum class BackendKind { Scalar, Avx2, Cuda, CudaAvx2, Vulkan, Hybrid, Zen4, Rocm, VulkanMulti };

inline constexpr std::array<BackendKind, 9> allBackendKinds{
    BackendKind::Scalar, BackendKind::Avx2,   BackendKind::Zen4,        BackendKind::Cuda,  BackendKind::CudaAvx2,
    BackendKind::Rocm,   BackendKind::Vulkan, BackendKind::VulkanMulti, BackendKind::Hybrid};

std::string_view backendKindName(BackendKind kind);
// Case-insensitive, ignoring '+', '-', '_' and spaces, so "cuda-avx2" works in URLs and on the command line.
// Saved files use parseStoredBackend (BackendSelection.h), where the old "Hybrid" still means CudaAvx2.
std::optional<BackendKind> backendKindFromName(std::string_view name);
std::string_view backendPluginFileName(BackendKind kind);
bool isBackendAvailable(BackendKind kind);
std::string backendUnavailableReason(BackendKind kind);
std::string backendDetail(BackendKind kind);
BackendHandle createBackend(BackendKind kind);
BackendKind fastestAvailableCpuBackend();
// The physical devices a backend kind's plugin can run on, in the plugin's order (the index the functions below
// take). CudaAvx2 spans two devices and lists none. DeviceInventory.h merges these lists into physical devices.
std::vector<PhysicsDeviceInfo> backendDevices(BackendKind kind);
std::string backendUnavailableReasonOnDevice(BackendKind kind, int device);
std::string backendDetailOnDevice(BackendKind kind, int device);
BackendHandle createBackendOnDevice(BackendKind kind, int device);
BackendHandle createBackendOnDevices(BackendKind kind, const int* devices, int count);
// Where physics plugins are looked for, ahead of the executable's own folder. Hosts whose executable lives
// elsewhere (Unity's editor and player both do) set this once at start-up; an empty path restores the default.
void setPluginSearchDirectory(std::filesystem::path directory);
std::filesystem::path pluginSearchDirectory();
// Which adapter the GPU physics plugins run on: "auto" (default), "integrated", "discrete", an index, or a name
// substring, resolved by selectGpuAdapter. Independent of the renderer's adapter; applies to backends created or
// queried afterwards, and is ignored by plugins that have no adapter choice (CPU, CUDA).
void setComputeGpuPreference(std::string_view preference);
std::string computeGpuPreference();
GpuRoofline measureGpuRoofline(size_t bufferBytes);
size_t gpuStreamedBytesPerBall(bool gridActive);
