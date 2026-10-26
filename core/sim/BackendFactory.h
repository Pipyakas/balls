#pragma once

#include "sim/PhysicsBackend.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

enum class BackendKind { Scalar, Avx2, Cuda };

inline constexpr std::array<BackendKind, 3> allBackendKinds{BackendKind::Scalar, BackendKind::Avx2, BackendKind::Cuda};

std::string_view backendKindName(BackendKind kind);
bool isBackendAvailable(BackendKind kind);
std::string backendUnavailableReason(BackendKind kind);
std::string backendDetail(BackendKind kind);
std::unique_ptr<PhysicsBackend> createBackend(BackendKind kind);
BackendKind fastestAvailableCpuBackend();
GpuRoofline measureGpuRoofline(size_t bufferBytes);
size_t gpuStreamedBytesPerBall(bool gridActive);
