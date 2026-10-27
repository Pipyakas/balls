// Native WebGPU renderer spike (Google Dawn, webgpu.h).
//
// Status: scaffolding. createWebGpuRenderer currently reports "not yet
// implemented" until the Dawn/wgpu-native dependency lands (see README.md).
// The IRenderer plumbing, factory wiring, and --api parsing below are final.

#include "render/webgpu/WebGpuRenderer.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <format>

namespace {

// backend: "auto", "d3d12", or "vulkan" (also accepts "webgpu",
// "webgpu-d3d12", "webgpu-vulkan").
std::string normalizeBackend(std::string_view backend) {
    std::string name(backend);
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (name.empty() || name == "auto" || name == "webgpu") {
#if defined(_WIN32)
        return "d3d12";
#else
        return "vulkan";
#endif
    }
    if (name == "webgpu-d3d12" || name == "d3d12") {
        return "d3d12";
    }
    if (name == "webgpu-vulkan" || name == "vulkan") {
        return "vulkan";
    }
    return name;
}

}

std::unique_ptr<IRenderer> createWebGpuRenderer(SDL_Window* window, std::string_view backend,
                                                std::string_view /*gpuPreference*/, std::string& error) {
    if (!window) {
        error = "WebGPU renderer needs a window";
        return nullptr;
    }
    const std::string normalized = normalizeBackend(backend);
    if (normalized != "d3d12" && normalized != "vulkan") {
        error = std::format("unknown WebGPU backend '{}'", backend);
        return nullptr;
    }
#if !defined(_WIN32)
    if (normalized != "vulkan") {
        error = "WebGPU backend 'd3d12' is Windows-only";
        return nullptr;
    }
#endif
    // TODO(webgpu): create Dawn instance/adapter/device/surface here once the
    // dependency lands. Until then fail loudly so nobody mistakes the stub for
    // a working renderer.
    error = std::format("WebGPU renderer (backend '{}') is not implemented yet", normalized);
    return nullptr;
}
