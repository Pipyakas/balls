#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <array>
#include <string>
#include <string_view>

enum class BackendKind { Scalar, Avx2, Cuda, Hybrid, Vulkan };

inline constexpr std::array<BackendKind, 5> allBackendKinds{BackendKind::Scalar, BackendKind::Avx2, BackendKind::Cuda,
                                                             BackendKind::Hybrid, BackendKind::Vulkan};

std::string_view backendKindName(BackendKind kind);
std::string_view backendPluginFileName(BackendKind kind);
bool isBackendAvailable(BackendKind kind);
std::string backendUnavailableReason(BackendKind kind);
std::string backendDetail(BackendKind kind);
BackendHandle createBackend(BackendKind kind);
BackendKind fastestAvailableCpuBackend();
// Which adapter the GPU physics plugins run on: "auto" (default), "integrated", "discrete", an index, or a name
// substring, resolved by selectGpuAdapter. Independent of the renderer's adapter; applies to backends created or
// queried afterwards, and is ignored by plugins that have no adapter choice (CPU, CUDA).
void setComputeGpuPreference(std::string_view preference);
std::string computeGpuPreference();
GpuRoofline measureGpuRoofline(size_t bufferBytes);
size_t gpuStreamedBytesPerBall(bool gridActive);
