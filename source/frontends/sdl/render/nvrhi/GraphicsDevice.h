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
    // Creating a device takes up to a few hundred milliseconds, so it happens off the UI thread with no hold on the
    // window. attach then creates the swap chain on the UI thread, once the previous device has detached.
    virtual bool attach(std::string& error) = 0;
    // Waits for the GPU and releases the swap chain, so the device can be destroyed on another thread.
    virtual void detach() = 0;
    virtual bool beginFrame() = 0;
    virtual nvrhi::ITexture* backBuffer() const = 0;
    virtual void present() = 0;
    // False when the compositor still holds every queued frame; the caller then skips this present instead of
    // blocking, so a throttled display (Remote Desktop composes at about 30 Hz) does not throttle the game loop.
    // A true answer reserves the slot, so it must be followed by present().
    virtual bool readyToPresent() { return true; }
    virtual bool setVsync(bool enabled) = 0;
    virtual bool isVsyncEnabled() const = 0;
    virtual nvrhi::Format backBufferFormat() const = 0;
    virtual std::string description() const = 0;
};

SDL_WindowFlags windowFlagsFor(GraphicsApi api);
// gpuPreference picks the rendering adapter: "auto" (default), "integrated", "discrete", an index, or a
// case-insensitive substring of the adapter name. The device comes back detached; call attach before rendering.
std::unique_ptr<GraphicsDevice> createGraphicsDevice(GraphicsApi api, SDL_Window* window, std::string_view gpuPreference,
                                                     std::string& error);
