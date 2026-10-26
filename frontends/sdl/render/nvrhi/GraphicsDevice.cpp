#include "render/nvrhi/GraphicsDevice.h"

#if defined(_WIN32)
std::unique_ptr<GraphicsDevice> createD3d12GraphicsDevice(SDL_Window* window, std::string& error);
#endif
std::unique_ptr<GraphicsDevice> createVulkanGraphicsDevice(SDL_Window* window, std::string_view gpuPreference,
                                                           std::string& error);

SDL_WindowFlags windowFlagsFor(GraphicsApi api) { return api == GraphicsApi::Vulkan ? SDL_WINDOW_VULKAN : 0; }

std::unique_ptr<GraphicsDevice> createGraphicsDevice(GraphicsApi api, SDL_Window* window, std::string_view gpuPreference,
                                                    std::string& error) {
#if defined(_WIN32)
    if (api == GraphicsApi::D3d12) {
        return createD3d12GraphicsDevice(window, error);
    }
#endif
    return createVulkanGraphicsDevice(window, gpuPreference, error);
}
