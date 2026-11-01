#pragma once

#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class BackendKind { Scalar, Avx2, Cuda, CudaAvx2, Vulkan, Hybrid, Zen4, Rocm, VulkanMulti };

inline constexpr std::array<BackendKind, 9> allBackendKinds{
    BackendKind::Scalar, BackendKind::Avx2,   BackendKind::Zen4,        BackendKind::Cuda,  BackendKind::CudaAvx2,
    BackendKind::Rocm,   BackendKind::Vulkan, BackendKind::VulkanMulti, BackendKind::Hybrid};

std::string_view backendKindName(BackendKind kind);

std::optional<BackendKind> backendKindFromName(std::string_view name);
std::string_view backendPluginFileName(BackendKind kind);
bool isBackendAvailable(BackendKind kind);
std::string backendUnavailableReason(BackendKind kind);
std::string backendDetail(BackendKind kind);
BackendHandle createBackend(BackendKind kind);
BackendKind fastestAvailableCpuBackend();

std::vector<PhysicsDeviceInfo> backendDevices(BackendKind kind);
std::string backendUnavailableReasonOnDevice(BackendKind kind, int device);
std::string backendDetailOnDevice(BackendKind kind, int device);
BackendHandle createBackendOnDevice(BackendKind kind, int device);
BackendHandle createBackendOnDevices(BackendKind kind, const int* devices, int count);

void setPluginSearchDirectory(std::filesystem::path directory);
std::filesystem::path pluginSearchDirectory();

void setComputeGpuPreference(std::string_view preference);
std::string computeGpuPreference();
GpuRoofline measureGpuRoofline(size_t bufferBytes);
size_t gpuStreamedBytesPerBall(bool gridActive);
