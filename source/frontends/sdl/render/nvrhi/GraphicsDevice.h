#pragma once

#include <nvrhi/nvrhi.h>

#include <SDL3/SDL_video.h>

#include <memory>
#include <string>

enum class GraphicsApi { D3d12, Vulkan };

class GraphicsDevice {
public:
    virtual ~GraphicsDevice() = default;

    virtual nvrhi::IDevice* device() const = 0;
    virtual bool beginFrame() = 0;
    virtual nvrhi::ITexture* backBuffer() const = 0;
    virtual void present() = 0;
    virtual bool setVsync(bool enabled) = 0;
    virtual bool isVsyncEnabled() const = 0;
    virtual nvrhi::Format backBufferFormat() const = 0;
    virtual std::string description() const = 0;
};

SDL_WindowFlags windowFlagsFor(GraphicsApi api);
// gpuPreference picks the rendering adapter: "auto" (default), "integrated", "discrete", an index, or a
// case-insensitive substring of the adapter name.
std::unique_ptr<GraphicsDevice> createGraphicsDevice(GraphicsApi api, SDL_Window* window, std::string_view gpuPreference,
                                                     std::string& error);
