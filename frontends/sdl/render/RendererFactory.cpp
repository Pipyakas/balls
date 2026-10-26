#include "render/IRenderer.h"

#if defined(BALLS_RENDERER_NVRHI)
#include "render/nvrhi/GraphicsDevice.h"

std::unique_ptr<IRenderer> createNvrhiRenderer(SDL_Window* window, GraphicsApi api, std::string& error);

namespace {

GraphicsApi graphicsApiFor(std::string_view requested) {
#if defined(_WIN32)
    return requested == "vulkan" ? GraphicsApi::Vulkan : GraphicsApi::D3d12;
#else
    return GraphicsApi::Vulkan;
#endif
}

}

SDL_WindowFlags rendererWindowFlags(std::string_view api) { return windowFlagsFor(graphicsApiFor(api)); }

std::unique_ptr<IRenderer> createRenderer(SDL_Window* window, std::string_view api, std::string& error) {
    return createNvrhiRenderer(window, graphicsApiFor(api), error);
}
#else
SDL_WindowFlags rendererWindowFlags(std::string_view) { return 0; }

std::unique_ptr<IRenderer> createRenderer(SDL_Window* window, std::string_view, std::string& error) {
    return createSdlGpuRenderer(window, error);
}
#endif
