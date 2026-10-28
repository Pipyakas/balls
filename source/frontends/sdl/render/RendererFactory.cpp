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
    // Vulkan is the default everywhere: the physics compute plugin is Vulkan-only, so the renderer sharing its API
    // keeps one driver stack per GPU. Direct3D 12 stays available on Windows when asked for.
#if defined(_WIN32)
    return requested == "d3d12" ? GraphicsApi::D3d12 : GraphicsApi::Vulkan;
#else
    (void)requested;
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

bool isRendererCompiledIn(std::string_view renderer) {
    const std::string name = lowercase(renderer);
    if (name.empty()) {
        return true;
    }
    if (name == "nvrhi") {
        return isNvrhiCompiledIn();
    }
    if (name == "webgpu") {
        return isWebGpuCompiledIn();
    }
#if defined(BALLS_RENDERER_SDLGPU)
    return name == "sdlgpu";
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
    // SDL only creates Vulkan surfaces on windows that asked for Vulkan, so an NVRHI window always does: the player
    // can switch from Direct3D 12 to Vulkan without the window being recreated.
    (void)api;
    return SDL_WINDOW_VULKAN;
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
#elif defined(BALLS_RENDERER_SDLGPU)
            r = "sdlgpu";
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
        auto renderer = createNvrhiRenderer(window, graphicsApiFor(api), gpuPreference, error);
        if (renderer && !renderer->attachToWindow(error)) {
            return nullptr;
        }
        return renderer;
    }
#endif

#if defined(BALLS_RENDERER_SDLGPU)
    if (r == "sdlgpu") {
        return createSdlGpuRenderer(window, error);
    }
#endif

    error = std::format("unknown or unsupported renderer '{}'", rendererName);
    return nullptr;
}

std::unique_ptr<IRenderer> prepareRenderer(SDL_Window* window, std::string_view rendererName, std::string_view api,
                                           std::string_view gpuPreference, std::string& error) {
#if defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
    const std::string r = lowercase(rendererName);
    const bool webGpuApi = api == "webgpu" || api.starts_with("webgpu-");
    if (r == "nvrhi" || (r.empty() && !webGpuApi)) {
        return createNvrhiRenderer(window, graphicsApiFor(api), gpuPreference, error);
    }
#else
    (void)window;
    (void)rendererName;
    (void)api;
    (void)gpuPreference;
    (void)error;
#endif
    return nullptr;
}
