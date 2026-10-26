#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <array>
#include <string>
#include <string_view>

enum class BackendKind { Scalar, Avx2, Cuda, Hybrid };

inline constexpr std::array<BackendKind, 4> allBackendKinds{BackendKind::Scalar, BackendKind::Avx2, BackendKind::Cuda,
                                                             BackendKind::Hybrid};

std::string_view backendKindName(BackendKind kind);
std::string_view backendPluginFileName(BackendKind kind);
bool isBackendAvailable(BackendKind kind);
std::string backendUnavailableReason(BackendKind kind);
std::string backendDetail(BackendKind kind);
BackendHandle createBackend(BackendKind kind);
BackendKind fastestAvailableCpuBackend();
GpuRoofline measureGpuRoofline(size_t bufferBytes);
size_t gpuStreamedBytesPerBall(bool gridActive);
