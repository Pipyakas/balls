#pragma once

#include "render/IRenderer.h"

#include <SDL3/SDL_video.h>

#include <memory>
#include <string>
#include <string_view>

// backend selects the WebGPU backend: "auto" (D3D12 on Windows, Vulkan elsewhere), "d3d12", or "vulkan".
// gpuPreference uses the same vocabulary as the NVRHI renderer: "auto" (default), "integrated", "discrete",
// an adapter index, or a case-insensitive substring of the adapter name.
std::unique_ptr<IRenderer> createWebGpuRenderer(SDL_Window* window, std::string_view backend,
                                                std::string_view gpuPreference, std::string& error);
