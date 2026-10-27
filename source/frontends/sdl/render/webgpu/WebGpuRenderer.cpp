// Native WebGPU renderer (wgpu-native / Google Dawn, webgpu.h).
//
// Step 1: instance, adapter, device, surface, present mode, clear pass, resize.

#include "render/webgpu/WebGpuRenderer.h"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::string lowercase(std::string_view text) {
    std::string s(text);
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string_view fromStringView(WGPUStringView s) {
    if (!s.data) {
        return {};
    }
    if (s.length == SIZE_MAX) {
        return std::string_view(s.data);
    }
    return std::string_view(s.data, s.length);
}

std::string normalizeBackend(std::string_view backend) {
    std::string name = lowercase(backend);
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

WGPUSurface createSurfaceFromWindow(WGPUInstance instance, SDL_Window* window) {
#if defined(__EMSCRIPTEN__)
    WGPUSurfaceSourceCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector = "#canvas";
    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &canvasDesc.chain;
    return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(_WIN32)
    HWND hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) {
        return nullptr;
    }
    WGPUSurfaceSourceWindowsHWND hwndDesc{};
    hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    hwndDesc.hinstance = GetModuleHandle(nullptr);
    hwndDesc.hwnd = hwnd;
    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &hwndDesc.chain;
    return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(__linux__)
    if (auto* display = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                               SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr)) {
        auto* surface = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                               SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        WGPUSurfaceSourceWaylandSurface waylandDesc{};
        waylandDesc.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        waylandDesc.display = display;
        waylandDesc.surface = surface;
        WGPUSurfaceDescriptor desc{};
        desc.nextInChain = &waylandDesc.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
    }
    auto* display = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                           SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    const uint64_t xwindow = SDL_GetNumberProperty(SDL_GetWindowProperties(window),
                                                   SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    WGPUSurfaceSourceXlibWindow x11Desc{};
    x11Desc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
    x11Desc.display = display;
    x11Desc.window = xwindow;
    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &x11Desc.chain;
    return wgpuInstanceCreateSurface(instance, &desc);
#else
    return nullptr;
#endif
}

class WebGpuRenderer final : public IRenderer {
public:
    WebGpuRenderer(SDL_Window* window, WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device,
                   WGPUQueue queue, WGPUSurface surface, std::string backendName, std::string adapterName,
                   WGPUTextureFormat surfaceFormat)
        : window(window),
          instance(instance),
          adapter(adapter),
          device(device),
          queue(queue),
          surface(surface),
          backendName(std::move(backendName)),
          adapterName(std::move(adapterName)),
          surfaceFormat(surfaceFormat) {
        updateWindowDimensions();
        reconfigureSurface();
    }

    ~WebGpuRenderer() override {
        if (currentSurfaceView) {
            wgpuTextureViewRelease(currentSurfaceView);
        }
        if (currentSurfaceTexture) {
            wgpuTextureRelease(currentSurfaceTexture);
        }
        if (surface) {
            wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceRelease(surface);
        }
        if (queue) {
            wgpuQueueRelease(queue);
        }
        if (device) {
            wgpuDeviceDestroy(device);
            wgpuDeviceRelease(device);
        }
        if (adapter) {
            wgpuAdapterRelease(adapter);
        }
        if (instance) {
            wgpuInstanceRelease(instance);
        }
    }

    bool beginFrame() override {
        const int previousPixelWidth = pixelWidth;
        const int previousPixelHeight = pixelHeight;
        updateWindowDimensions();

        if (pixelWidth != previousPixelWidth || pixelHeight != previousPixelHeight) {
            reconfigureSurface();
        }

        if (pixelWidth <= 0 || pixelHeight <= 0) {
            return false;
        }

        WGPUSurfaceTexture surfaceTexture{};
        wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
        if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
            surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Timeout) {
            reconfigureSurface();
            wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
        }

        if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success || !surfaceTexture.texture) {
            return false;
        }

        currentSurfaceTexture = surfaceTexture.texture;
        currentSurfaceView = wgpuTextureCreateView(currentSurfaceTexture, nullptr);
        frameStartTime = Clock::now();
        return true;
    }

    void drawRects(std::span<const RendererRect> rects) override {
        // Will be populated in Step 2
        (void)rects;
    }

    void drawBalls(const BallDrawRequest& request) override {
        // Will be populated in Step 4
        (void)request;
    }

    void endFrame(ClearColor clearColor) override {
        if (!currentSurfaceView) {
            return;
        }

        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = currentSurfaceView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{
            static_cast<double>(clearColor.red),
            static_cast<double>(clearColor.green),
            static_cast<double>(clearColor.blue),
            1.0
        };
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);

        const auto submitStart = Clock::now();
        wgpuQueueSubmit(queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);

        wgpuSurfacePresent(surface);
        const auto frameEnd = Clock::now();
        lastBusyMs = std::chrono::duration<double, std::milli>(frameEnd - submitStart).count();

        wgpuTextureViewRelease(currentSurfaceView);
        currentSurfaceView = nullptr;
        wgpuTextureRelease(currentSurfaceTexture);
        currentSurfaceTexture = nullptr;
    }

    float viewWidth() const override { return logicalWidth; }
    float viewHeight() const override { return logicalHeight; }

    bool setVsync(bool enabled) override {
        if (vsync == enabled) {
            return true;
        }
        vsync = enabled;
        reconfigureSurface();
        return true;
    }

    bool isVsyncEnabled() const override { return vsync; }

    void requestScreenshot(const std::string& path) override {
        pendingScreenshotPath = path;
    }

    bool lastScreenshotSucceeded() const override {
        return screenshotSuccess;
    }

    double lastGpuWaitMs() const override { return lastWaitMs; }
    double lastGpuBusyMs() const override { return lastBusyMs; }

    std::string deviceDescription() const override {
        const std::string api = backendName == "d3d12" ? "Direct3D 12" : "Vulkan";
        return std::format("WebGPU / {} / {}", api, adapterName);
    }

private:
    void updateWindowDimensions() {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window, &w, &h);
        logicalWidth = static_cast<float>(std::max(w, 1));
        logicalHeight = static_cast<float>(std::max(h, 1));

        int pw = 0;
        int ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        pixelWidth = std::max(pw, 1);
        pixelHeight = std::max(ph, 1);
    }

    void reconfigureSurface() {
        if (!surface || pixelWidth <= 0 || pixelHeight <= 0) {
            return;
        }

        WGPUSurfaceCapabilities caps{};
        if (wgpuSurfaceGetCapabilities(surface, adapter, &caps) == WGPUStatus_Success) {
            WGPUPresentMode wantedMode = vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
            bool supported = false;
            for (size_t i = 0; i < caps.presentModeCount; ++i) {
                if (caps.presentModes[i] == wantedMode) {
                    supported = true;
                    break;
                }
            }
            if (!supported && !vsync) {
                for (size_t i = 0; i < caps.presentModeCount; ++i) {
                    if (caps.presentModes[i] == WGPUPresentMode_Mailbox) {
                        wantedMode = WGPUPresentMode_Mailbox;
                        supported = true;
                        break;
                    }
                }
                if (!supported) {
                    wantedMode = WGPUPresentMode_Fifo;
                }
            }

            WGPUSurfaceConfiguration config{};
            config.device = device;
            config.format = surfaceFormat;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.width = static_cast<uint32_t>(pixelWidth);
            config.height = static_cast<uint32_t>(pixelHeight);
            config.presentMode = wantedMode;
            wgpuSurfaceConfigure(surface, &config);
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
    }

    SDL_Window* window = nullptr;
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;

    std::string backendName;
    std::string adapterName;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

    float logicalWidth = 1440.0f;
    float logicalHeight = 900.0f;
    int pixelWidth = 1440;
    int pixelHeight = 900;
    bool vsync = false;

    WGPUTexture currentSurfaceTexture = nullptr;
    WGPUTextureView currentSurfaceView = nullptr;

    Clock::time_point frameStartTime = Clock::now();
    double lastWaitMs = 0.0;
    double lastBusyMs = 0.0;

    std::string pendingScreenshotPath;
    bool screenshotSuccess = false;
};

struct DeviceRequestPayload {
    WGPUDevice device = nullptr;
    std::string error;
    bool complete = false;
};

} // namespace

std::unique_ptr<IRenderer> createWebGpuRenderer(SDL_Window* window, std::string_view backend,
                                                std::string_view gpuPreference, std::string& error) {
    if (!window) {
        error = "WebGPU renderer needs an SDL window";
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

    WGPUInstanceDescriptor instanceDesc{};
    WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
        error = "wgpuCreateInstance failed";
        return nullptr;
    }

    WGPUSurface surface = createSurfaceFromWindow(instance, window);
    if (!surface) {
        error = "failed to create WebGPU surface for SDL window";
        wgpuInstanceRelease(instance);
        return nullptr;
    }

    WGPUInstanceBackend backendMask =
        normalized == "d3d12" ? WGPUInstanceBackend_DX12 : WGPUInstanceBackend_Vulkan;
    WGPUInstanceEnumerateAdapterOptions enumOptions{};
    enumOptions.backends = backendMask;

    const size_t adapterCount = wgpuInstanceEnumerateAdapters(instance, &enumOptions, nullptr);
    if (adapterCount == 0) {
        error = std::format("no WebGPU adapters found for backend '{}'", normalized);
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        return nullptr;
    }

    std::vector<WGPUAdapter> adapters(adapterCount);
    wgpuInstanceEnumerateAdapters(instance, &enumOptions, adapters.data());

    struct Candidate {
        WGPUAdapter adapter;
        std::string name;
        WGPUAdapterType type;
        size_t index;
    };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < adapters.size(); ++i) {
        WGPUAdapterInfo info{};
        if (wgpuAdapterGetInfo(adapters[i], &info) == WGPUStatus_Success) {
            std::string name(fromStringView(info.device));
            if (name.empty()) {
                name = std::string(fromStringView(info.description));
            }
            candidates.push_back({adapters[i], name, info.adapterType, i});
            wgpuAdapterInfoFreeMembers(info);
        } else {
            candidates.push_back({adapters[i], std::format("GPU{}", i), WGPUAdapterType_Unknown, i});
        }
    }

    // Match gpuPreference: auto (discrete > integrated), discrete, integrated, index, name substring
    const std::string pref = lowercase(gpuPreference);
    size_t chosenIndex = 0;
    bool found = false;

    // Check if integer index
    char* endPtr = nullptr;
    const long requestedIdx = std::strtol(pref.c_str(), &endPtr, 10);
    if (endPtr != pref.c_str() && *endPtr == '\0' && requestedIdx >= 0 &&
        static_cast<size_t>(requestedIdx) < candidates.size()) {
        chosenIndex = static_cast<size_t>(requestedIdx);
        found = true;
    }

    if (!found && (pref.empty() || pref == "auto")) {
        // Discrete first, then integrated
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].type == WGPUAdapterType_DiscreteGPU) {
                chosenIndex = i;
                found = true;
                break;
            }
        }
        if (!found) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (candidates[i].type == WGPUAdapterType_IntegratedGPU) {
                    chosenIndex = i;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            chosenIndex = 0;
            found = true;
        }
    } else if (!found && pref == "discrete") {
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].type == WGPUAdapterType_DiscreteGPU) {
                chosenIndex = i;
                found = true;
                break;
            }
        }
    } else if (!found && pref == "integrated") {
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].type == WGPUAdapterType_IntegratedGPU) {
                chosenIndex = i;
                found = true;
                break;
            }
        }
    } else if (!found) {
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (lowercase(candidates[i].name).find(pref) != std::string::npos) {
                chosenIndex = i;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        error = std::format("WebGPU adapter preference '{}' matched no adapter on backend '{}'",
                            gpuPreference, normalized);
        for (auto& c : candidates) {
            wgpuAdapterRelease(c.adapter);
        }
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        return nullptr;
    }

    WGPUAdapter chosenAdapter = candidates[chosenIndex].adapter;
    const std::string chosenName = candidates[chosenIndex].name;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i != chosenIndex) {
            wgpuAdapterRelease(candidates[i].adapter);
        }
    }

    // Create device with validation error callback
    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type,
                                                        WGPUStringView message, void*, void*) {
        const std::string_view msg = fromStringView(message);
        SDL_Log("WebGPU validation error [%d]: %.*s", static_cast<int>(type),
                static_cast<int>(msg.size()), msg.data());
    };

    DeviceRequestPayload payload;
    WGPURequestDeviceCallbackInfo callbackInfo{};
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice dev, WGPUStringView message,
                               void* u1, void*) {
        auto* pl = static_cast<DeviceRequestPayload*>(u1);
        pl->complete = true;
        if (status == WGPURequestDeviceStatus_Success) {
            pl->device = dev;
        } else {
            pl->error = std::string(fromStringView(message));
        }
    };
    callbackInfo.userdata1 = &payload;

    wgpuAdapterRequestDevice(chosenAdapter, &deviceDesc, callbackInfo);
    while (!payload.complete) {
        wgpuInstanceProcessEvents(instance);
    }

    if (!payload.device) {
        error = std::format("wgpuAdapterRequestDevice failed: {}", payload.error);
        wgpuAdapterRelease(chosenAdapter);
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        return nullptr;
    }

    WGPUDevice device = payload.device;
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    WGPUSurfaceCapabilities caps{};
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    if (wgpuSurfaceGetCapabilities(surface, chosenAdapter, &caps) == WGPUStatus_Success) {
        if (caps.formatCount > 0) {
            surfaceFormat = caps.formats[0];
        }
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    return std::make_unique<WebGpuRenderer>(window, instance, chosenAdapter, device, queue, surface,
                                            normalized, chosenName, surfaceFormat);
}
