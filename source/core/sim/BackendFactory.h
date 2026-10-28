#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Hybrid is the device composer (DeviceComposer): one backend on each device the player picked
// (BackendSelection.h). It replaced the fixed CUDA+AVX2 pair, whose names now select Hybrid.
enum class BackendKind { Scalar, Avx2, Cuda, Vulkan, Hybrid };

inline constexpr std::array<BackendKind, 5> allBackendKinds{BackendKind::Scalar, BackendKind::Avx2, BackendKind::Cuda,
                                                             BackendKind::Vulkan, BackendKind::Hybrid};

std::string_view backendKindName(BackendKind kind);
// Case-insensitive, ignoring '+', '-', '_' and spaces, so "vulkan" or "Hybrid" work in URLs and on the command
// line. The retired pair's "CUDA+AVX2" (or "cuda-avx2") gives Hybrid. Saved files use parseStoredBackend
// (BackendSelection.h).
std::optional<BackendKind> backendKindFromName(std::string_view name);
std::string_view backendPluginFileName(BackendKind kind);
bool isBackendAvailable(BackendKind kind);
std::string backendUnavailableReason(BackendKind kind);
std::string backendDetail(BackendKind kind);
BackendHandle createBackend(BackendKind kind);
BackendKind fastestAvailableCpuBackend();
// The physical devices a backend kind's plugin can run on, in the plugin's order (the index the functions below
// take). Hybrid spans devices and lists none. DeviceInventory.h merges these lists into physical devices.
std::vector<PhysicsDeviceInfo> backendDevices(BackendKind kind);
std::string backendUnavailableReasonOnDevice(BackendKind kind, int device);
std::string backendDetailOnDevice(BackendKind kind, int device);
BackendHandle createBackendOnDevice(BackendKind kind, int device);
// Which adapter the GPU physics plugins run on: "auto" (default), "integrated", "discrete", an index, or a name
// substring, resolved by selectGpuAdapter. Independent of the renderer's adapter; applies to backends created or
// queried afterwards, and is ignored by plugins that have no adapter choice (CPU, CUDA).
void setComputeGpuPreference(std::string_view preference);
std::string computeGpuPreference();
GpuRoofline measureGpuRoofline(size_t bufferBytes);
size_t gpuStreamedBytesPerBall(bool gridActive);
