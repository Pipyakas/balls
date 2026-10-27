#include "render/IRenderer.h"

#if defined(BALLS_RENDERER_WEBGPU) || defined(BALLS_WITH_WEBGPU)
#include "render/webgpu/WebGpuRenderer.h"
#endif

#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
#include "render/nvrhi/GraphicsDevice.h"

std::unique_ptr<IRenderer> createNvrhiRenderer(SDL_Window* window, GraphicsApi api, std::string_view gpuPreference,
                                               std::string& error);
#endif

#include <algorithm>
#include <cctype>
#include <format>
#include <string>

namespace {

#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
GraphicsApi graphicsApiFor(std::string_view requested) {
#if defined(_WIN32)
    return requested == "vulkan" ? GraphicsApi::Vulkan : GraphicsApi::D3d12;
#else
    return GraphicsApi::Vulkan;
#endif
}
#endif

std::string lowercase(std::string_view text) {
    std::string s(text);
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace

bool isWebGpuCompiledIn() {
#if defined(BALLS_RENDERER_WEBGPU) || defined(BALLS_WITH_WEBGPU)
    return true;
#else
    return false;
#endif
}

bool isNvrhiCompiledIn() {
#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
    return true;
#else
    return false;
#endif
}

SDL_WindowFlags rendererWindowFlags(std::string_view renderer, std::string_view api) {
    std::string r = lowercase(renderer);
    if (r.empty()) {
        if (api == "webgpu" || api.starts_with("webgpu-")) {
            r = "webgpu";
        } else {
#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
            r = "nvrhi";
#elif defined(BALLS_RENDERER_WEBGPU) || defined(BALLS_WITH_WEBGPU)
            r = "webgpu";
#endif
        }
    }
    if (r == "webgpu") {
        return 0;
    }
#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
    return windowFlagsFor(graphicsApiFor(api));
#else
    return 0;
#endif
}

SDL_WindowFlags rendererWindowFlags(std::string_view api) {
    return rendererWindowFlags({}, api);
}

std::string_view fallbackGraphicsApi(std::string_view api) {
#if defined(_WIN32)
    return api == "vulkan" ? "d3d12" : "vulkan";
#else
    return {};
#endif
}

std::unique_ptr<IRenderer> createRenderer(SDL_Window* window, std::string_view rendererName,
                                          std::string_view api, std::string_view gpuPreference,
                                          std::string& error) {
    std::string r = lowercase(rendererName);
    if (r.empty()) {
        if (api == "webgpu" || api.starts_with("webgpu-")) {
#if defined(BALLS_RENDERER_WEBGPU) || defined(BALLS_WITH_WEBGPU)
            r = "webgpu";
#endif
        } else {
#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
            r = "nvrhi";
#elif defined(BALLS_RENDERER_WEBGPU) || defined(BALLS_WITH_WEBGPU)
            r = "webgpu";
#endif
        }
    }

#if defined(BALLS_RENDERER_WEBGPU) || defined(BALLS_WITH_WEBGPU)
    if (r == "webgpu") {
        std::string_view backend = api;
        if (backend == "webgpu") {
            backend = {};
        } else if (backend.starts_with("webgpu-")) {
            backend = backend.substr(7);
        }
        return createWebGpuRenderer(window, backend, gpuPreference, error);
    }
#endif

#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
    if (r == "nvrhi") {
        return createNvrhiRenderer(window, graphicsApiFor(api), gpuPreference, error);
    }
#endif

    error = std::format("unsupported or uncompiled renderer: '{}'", rendererName);
    return nullptr;
}
