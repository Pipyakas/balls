// Native WebGPU renderer (wgpu-native / Google Dawn, webgpu.h).
//
// Step 1: instance, adapter, device, surface, present mode, clear pass, resize.
// Step 2: rects pipeline, storage buffer upload, instanced quad draw.
// Step 3: imgui pipeline, texture management, vertex/index upload, scissor clipping.
// Step 4: balls pipeline, visibility compute passes (clear, bid, resolve, compact), indirect draw.

#include "render/webgpu/WebGpuRenderer.h"
#include "shaders/generated/web/WgslShaders.h"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

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

constexpr uint32_t computeThreadsPerGroup = 256;
constexpr uint32_t renderCoordinateRange = 16384;
constexpr size_t biddersPerSlot = 32;

struct BallUniforms {
    float boardOrigin[2];
    float unpackScale[2];
    float viewportSize[2];
    float cellPixels;
    float radiusPixels;
    float palette[rendererPaletteSize][4];
};

struct VisibilityUniforms {
    uint32_t itemCount;
    uint32_t gridWidth;
    uint32_t gridHeight;
    uint32_t frameSeed;
    float coordinateToSlotX;
    float coordinateToSlotY;
    uint32_t bidderMask;
    uint32_t unusedPadding;
};

uint32_t groupsFor(size_t items) {
    return static_cast<uint32_t>(std::max<size_t>((items + computeThreadsPerGroup - 1) / computeThreadsPerGroup, 1));
}

uint32_t bidderMaskFor(size_t balls, size_t cells) {
    uint32_t mask = 0;
    while (mask < 0x3FFFFFFFu && (static_cast<size_t>(mask) + 1) * 2 * cells * biddersPerSlot <= balls) {
        mask = mask * 2 + 1;
    }
    return mask;
}

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

WGPUShaderModule createShaderModule(WGPUDevice device, const char* source, const char* label) {
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{source, std::strlen(source)};
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgslDesc.chain;
    if (label) {
        desc.label = WGPUStringView{label, std::strlen(label)};
    }
    return wgpuDeviceCreateShaderModule(device, &desc);
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

struct ImguiTextureBinding {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

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
        initRectsPipeline();
        initBallsPipelines();

        ImGui_ImplSDL3_InitForOther(window);
        ImGui::GetIO().BackendRendererName = "balls_webgpu";
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        imguiReady = initImguiPipeline();
    }

    ~WebGpuRenderer() override {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
            destroyImguiTexture(texture);
        }
        if (imguiReady) {
            ImGui_ImplSDL3_Shutdown();
        }

        if (imguiPipeline) {
            wgpuRenderPipelineRelease(imguiPipeline);
        }
        if (imguiPipelineLayout) {
            wgpuPipelineLayoutRelease(imguiPipelineLayout);
        }
        if (imguiBindGroupLayout) {
            wgpuBindGroupLayoutRelease(imguiBindGroupLayout);
        }
        if (imguiUniformBuffer) {
            wgpuBufferDestroy(imguiUniformBuffer);
            wgpuBufferRelease(imguiUniformBuffer);
        }
        if (imguiVertexBuffer) {
            wgpuBufferDestroy(imguiVertexBuffer);
            wgpuBufferRelease(imguiVertexBuffer);
        }
        if (imguiIndexBuffer) {
            wgpuBufferDestroy(imguiIndexBuffer);
            wgpuBufferRelease(imguiIndexBuffer);
        }
        if (imguiSampler) {
            wgpuSamplerRelease(imguiSampler);
        }
        if (imguiVertexModule) {
            wgpuShaderModuleRelease(imguiVertexModule);
        }
        if (imguiFragmentModule) {
            wgpuShaderModuleRelease(imguiFragmentModule);
        }

        if (rectsStorageBindGroup) {
            wgpuBindGroupRelease(rectsStorageBindGroup);
        }
        if (rectsUniformBindGroup) {
            wgpuBindGroupRelease(rectsUniformBindGroup);
        }
        if (rectsBuffer) {
            wgpuBufferDestroy(rectsBuffer);
            wgpuBufferRelease(rectsBuffer);
        }
        if (rectsUniformBuffer) {
            wgpuBufferDestroy(rectsUniformBuffer);
            wgpuBufferRelease(rectsUniformBuffer);
        }
        if (rectsPipeline) {
            wgpuRenderPipelineRelease(rectsPipeline);
        }
        if (rectsPipelineLayout) {
            wgpuPipelineLayoutRelease(rectsPipelineLayout);
        }
        if (rectsStorageLayout) {
            wgpuBindGroupLayoutRelease(rectsStorageLayout);
        }
        if (rectsUniformLayout) {
            wgpuBindGroupLayoutRelease(rectsUniformLayout);
        }
        if (rectsVertexModule) {
            wgpuShaderModuleRelease(rectsVertexModule);
        }
        if (rectsFragmentModule) {
            wgpuShaderModuleRelease(rectsFragmentModule);
        }

        releaseBallBindGroups();
        if (ballUniformBindGroup) {
            wgpuBindGroupRelease(ballUniformBindGroup);
        }
        for (int i = 0; i < 4; ++i) {
            if (visibilityConstantsBindGroups[i]) {
                wgpuBindGroupRelease(visibilityConstantsBindGroups[i]);
            }
            if (visibilityUniformBuffers[i]) {
                wgpuBufferDestroy(visibilityUniformBuffers[i]);
                wgpuBufferRelease(visibilityUniformBuffers[i]);
            }
        }
        if (ballUniformBuffer) {
            wgpuBufferDestroy(ballUniformBuffer);
            wgpuBufferRelease(ballUniformBuffer);
        }
        if (argumentBuffer) {
            wgpuBufferDestroy(argumentBuffer);
            wgpuBufferRelease(argumentBuffer);
        }
        if (ballBuffer) {
            wgpuBufferDestroy(ballBuffer);
            wgpuBufferRelease(ballBuffer);
        }
        if (priorityBuffer) {
            wgpuBufferDestroy(priorityBuffer);
            wgpuBufferRelease(priorityBuffer);
        }
        if (winnerBuffer) {
            wgpuBufferDestroy(winnerBuffer);
            wgpuBufferRelease(winnerBuffer);
        }
        if (visibleBuffer) {
            wgpuBufferDestroy(visibleBuffer);
            wgpuBufferRelease(visibleBuffer);
        }
        if (ballPipeline) {
            wgpuRenderPipelineRelease(ballPipeline);
        }
        if (clearPipeline) {
            wgpuComputePipelineRelease(clearPipeline);
        }
        if (bidPipeline) {
            wgpuComputePipelineRelease(bidPipeline);
        }
        if (resolvePipeline) {
            wgpuComputePipelineRelease(resolvePipeline);
        }
        if (compactPipeline) {
            wgpuComputePipelineRelease(compactPipeline);
        }
        if (ballPipelineLayout) {
            wgpuPipelineLayoutRelease(ballPipelineLayout);
        }
        if (computePipelineLayout) {
            wgpuPipelineLayoutRelease(computePipelineLayout);
        }
        if (ballDrawStorageLayout) {
            wgpuBindGroupLayoutRelease(ballDrawStorageLayout);
        }
        if (ballDrawUniformLayout) {
            wgpuBindGroupLayoutRelease(ballDrawUniformLayout);
        }
        if (computeInputLayout) {
            wgpuBindGroupLayoutRelease(computeInputLayout);
        }
        if (computeOutputLayout) {
            wgpuBindGroupLayoutRelease(computeOutputLayout);
        }
        if (computeConstantsLayout) {
            wgpuBindGroupLayoutRelease(computeConstantsLayout);
        }
        if (ballsVertexModule) {
            wgpuShaderModuleRelease(ballsVertexModule);
        }
        if (ballsFragmentModule) {
            wgpuShaderModuleRelease(ballsFragmentModule);
        }
        if (visibilityClearModule) {
            wgpuShaderModuleRelease(visibilityClearModule);
        }
        if (visibilityBidModule) {
            wgpuShaderModuleRelease(visibilityBidModule);
        }
        if (visibilityResolveModule) {
            wgpuShaderModuleRelease(visibilityResolveModule);
        }
        if (visibilityCompactModule) {
            wgpuShaderModuleRelease(visibilityCompactModule);
        }

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
        const auto waitStart = Clock::now();
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

        const auto waitEnd = Clock::now();
        lastWaitMs = std::chrono::duration<double, std::milli>(waitEnd - waitStart).count();

        const bool statusOk = (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                               surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
        if (!statusOk || !surfaceTexture.texture) {
            return false;
        }

        currentSurfaceTexture = surfaceTexture.texture;
        currentSurfaceView = wgpuTextureCreateView(currentSurfaceTexture, nullptr);
        frameStartTime = Clock::now();
        pendingRects.clear();
        hasBalls = false;

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        return true;
    }

    void drawRects(std::span<const RendererRect> rects) override {
        pendingRects.assign(rects.begin(), rects.end());
    }

    void drawBalls(const BallDrawRequest& request) override {
        pendingBalls = request;
        hasBalls = request.count > 0 && request.packed != nullptr;
    }

    void endFrame(ClearColor clearColor) override {
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData) {
            updateImguiTextures(drawData);
        }

        if (!currentSurfaceView) {
            return;
        }

        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

        if (hasBalls) {
            runBallVisibility(encoder);
        }

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

        // 1. Rects
        if (!pendingRects.empty() && rectsPipeline) {
            ensureRectBuffers(pendingRects.size());
            wgpuQueueWriteBuffer(queue, rectsBuffer, 0, pendingRects.data(),
                                 pendingRects.size() * sizeof(RendererRect));
            const float uniforms[4] = { logicalWidth, logicalHeight, 0.0f, 0.0f };
            wgpuQueueWriteBuffer(queue, rectsUniformBuffer, 0, uniforms, sizeof(uniforms));

            wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, static_cast<uint32_t>(pixelWidth),
                                                static_cast<uint32_t>(pixelHeight));
            wgpuRenderPassEncoderSetPipeline(pass, rectsPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, rectsStorageBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, rectsUniformBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(pendingRects.size()), 0, 0);
        }
        pendingRects.clear();

        // 2. Balls
        if (hasBalls && ballPipeline) {
            const BallDrawRequest& request = pendingBalls;
            BallUniforms uniforms{};
            uniforms.boardOrigin[0] = request.originX;
            uniforms.boardOrigin[1] = request.originY;
            uniforms.unpackScale[0] = request.fieldWidth / static_cast<float>(renderCoordinateRange - 1);
            uniforms.unpackScale[1] = request.fieldHeight / static_cast<float>(renderCoordinateRange - 1);
            uniforms.viewportSize[0] = logicalWidth;
            uniforms.viewportSize[1] = logicalHeight;
            uniforms.cellPixels = request.cellPixels;
            uniforms.radiusPixels = std::max(request.radiusCells * request.cellPixels,
                                             0.5f * logicalWidth / static_cast<float>(pixelWidth));
            for (int color = 0; color < rendererPaletteSize; ++color) {
                std::copy(request.palette[color].begin(), request.palette[color].end(), uniforms.palette[color]);
            }
            wgpuQueueWriteBuffer(queue, ballUniformBuffer, 0, &uniforms, sizeof(uniforms));

            const float scale = static_cast<float>(pixelWidth) / logicalWidth;
            const bool clipped = request.clipWidth > 0.0f && request.clipHeight > 0.0f;
            if (clipped) {
                const float left = std::max(request.clipX * scale, 0.0f);
                const float top = std::max(request.clipY * scale, 0.0f);
                const float right = std::min((request.clipX + request.clipWidth) * scale, static_cast<float>(pixelWidth));
                const float bottom = std::min((request.clipY + request.clipHeight) * scale, static_cast<float>(pixelHeight));
                if (right > left && bottom > top) {
                    wgpuRenderPassEncoderSetScissorRect(pass, static_cast<uint32_t>(left), static_cast<uint32_t>(top),
                                                        static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top));
                }
            } else {
                wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, static_cast<uint32_t>(pixelWidth),
                                                    static_cast<uint32_t>(pixelHeight));
            }

            wgpuRenderPassEncoderSetPipeline(pass, ballPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, ballStorageBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, ballUniformBindGroup, 0, nullptr);
            if (argumentBuffer && compactPipeline) {
                wgpuRenderPassEncoderDrawIndirect(pass, argumentBuffer, 0);
            } else {
                wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(request.count), 0, 0);
            }
        }

        // 3. ImGui
        if (drawData && drawData->TotalVtxCount > 0 && imguiPipeline) {
            renderImgui(pass, drawData);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        const bool capturing = !pendingScreenshotPath.empty();
        WGPUBuffer stagingBuffer = nullptr;
        uint32_t alignedBytesPerRow = 0;
        size_t totalBytes = 0;

        if (capturing) {
            alignedBytesPerRow = (static_cast<uint32_t>(pixelWidth) * 4 + 255u) & ~255u;
            totalBytes = static_cast<size_t>(alignedBytesPerRow) * pixelHeight;

            WGPUBufferDescriptor bufDesc{};
            bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            bufDesc.size = totalBytes;
            stagingBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);

            WGPUTexelCopyTextureInfo src{};
            src.texture = currentSurfaceTexture;
            src.mipLevel = 0;
            src.origin = {0, 0, 0};
            src.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferInfo dst{};
            dst.buffer = stagingBuffer;
            dst.layout.offset = 0;
            dst.layout.bytesPerRow = alignedBytesPerRow;
            dst.layout.rowsPerImage = static_cast<uint32_t>(pixelHeight);

            WGPUExtent3D extent{};
            extent.width = static_cast<uint32_t>(pixelWidth);
            extent.height = static_cast<uint32_t>(pixelHeight);
            extent.depthOrArrayLayers = 1;

            wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
        }

        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);

        const auto submitStart = Clock::now();
        wgpuQueueSubmit(queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);

        const WGPUStatus pres = wgpuSurfacePresent(surface);
        const auto frameEnd = Clock::now();
        lastBusyMs = std::chrono::duration<double, std::milli>(frameEnd - submitStart).count();

        if (capturing && stagingBuffer) {
            screenshotSuccess = false;
            struct MapCtx {
                bool done = false;
                WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Unknown;
            } mapCtx;

            WGPUBufferMapCallbackInfo cbInfo{};
            cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
            cbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ud1, void*) {
                auto* c = static_cast<MapCtx*>(ud1);
                c->done = true;
                c->status = status;
            };
            cbInfo.userdata1 = &mapCtx;

            wgpuBufferMapAsync(stagingBuffer, WGPUMapMode_Read, 0, totalBytes, cbInfo);
            while (!mapCtx.done) {
                wgpuDevicePoll(device, true, nullptr);
                wgpuInstanceProcessEvents(instance);
            }

            if (mapCtx.status == WGPUMapAsyncStatus_Success) {
                const void* mapped = wgpuBufferGetMappedRange(stagingBuffer, 0, totalBytes);
                if (mapped) {
                    const SDL_PixelFormat format =
                        (surfaceFormat == WGPUTextureFormat_BGRA8Unorm || surfaceFormat == WGPUTextureFormat_BGRA8UnormSrgb)
                            ? SDL_PIXELFORMAT_ARGB8888
                            : SDL_PIXELFORMAT_ABGR8888;
                    SDL_Surface* shotSurface = SDL_CreateSurfaceFrom(pixelWidth, pixelHeight, format,
                                                                    const_cast<void*>(mapped), alignedBytesPerRow);
                    if (shotSurface) {
                        screenshotSuccess = SDL_SaveBMP(shotSurface, pendingScreenshotPath.c_str());
                        SDL_DestroySurface(shotSurface);
                    }
                }
                wgpuBufferUnmap(stagingBuffer);
            }
            wgpuBufferDestroy(stagingBuffer);
            wgpuBufferRelease(stagingBuffer);
            pendingScreenshotPath.clear();
        }

        wgpuTextureViewRelease(currentSurfaceView);
        currentSurfaceView = nullptr;
        if (pres == WGPUStatus_Success) {
            wgpuTextureRelease(currentSurfaceTexture);
        }
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
                    SDL_Log("WebGPU: Immediate/Mailbox not offered; presenting with Fifo (vsync-locked)");
                }
            }

            WGPUSurfaceConfiguration config{};
            config.device = device;
            config.format = surfaceFormat;
            config.usage = WGPUTextureUsage_RenderAttachment;
            if ((caps.usages & WGPUTextureUsage_CopySrc) != 0 || caps.usages == 0) {
                config.usage |= WGPUTextureUsage_CopySrc;
            }
            config.width = static_cast<uint32_t>(pixelWidth);
            config.height = static_cast<uint32_t>(pixelHeight);
            config.alphaMode = caps.alphaModeCount > 0 ? caps.alphaModes[0] : WGPUCompositeAlphaMode_Opaque;
            config.presentMode = wantedMode;
            SDL_Log("WebGPU: surface %ux%u present mode %d", config.width, config.height,
                    static_cast<int>(wantedMode));
            wgpuSurfaceConfigure(surface, &config);
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
    }

    bool initRectsPipeline() {
        rectsVertexModule = createShaderModule(device, rectsVertexWgsl, "rectsVertex");
        rectsFragmentModule = createShaderModule(device, rectsFragmentWgsl, "rectsFragment");
        if (!rectsVertexModule || !rectsFragmentModule) {
            SDL_Log("WebGPU: failed to create rects shader modules");
            return false;
        }

        WGPUBindGroupLayoutEntry storageEntry{};
        storageEntry.binding = 0;
        storageEntry.visibility = WGPUShaderStage_Vertex;
        storageEntry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

        WGPUBindGroupLayoutDescriptor storageLayoutDesc{};
        storageLayoutDesc.entryCount = 1;
        storageLayoutDesc.entries = &storageEntry;
        rectsStorageLayout = wgpuDeviceCreateBindGroupLayout(device, &storageLayoutDesc);

        WGPUBindGroupLayoutEntry uniformEntry{};
        uniformEntry.binding = 0;
        uniformEntry.visibility = WGPUShaderStage_Vertex;
        uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor uniformLayoutDesc{};
        uniformLayoutDesc.entryCount = 1;
        uniformLayoutDesc.entries = &uniformEntry;
        rectsUniformLayout = wgpuDeviceCreateBindGroupLayout(device, &uniformLayoutDesc);

        WGPUBindGroupLayout bgLayouts[2] = { rectsStorageLayout, rectsUniformLayout };
        WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
        pipelineLayoutDesc.bindGroupLayoutCount = 2;
        pipelineLayoutDesc.bindGroupLayouts = bgLayouts;
        rectsPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUBlendState blend{};
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = surfaceFormat;
        colorTarget.blend = &blend;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState{};
        fragmentState.module = rectsFragmentModule;
        fragmentState.entryPoint = WGPUStringView{"fragmentMain", 12};
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPUVertexState vertexState{};
        vertexState.module = rectsVertexModule;
        vertexState.entryPoint = WGPUStringView{"vertexMain", 10};

        WGPURenderPipelineDescriptor desc{};
        desc.layout = rectsPipelineLayout;
        desc.vertex = vertexState;
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.primitive.frontFace = WGPUFrontFace_CCW;
        desc.multisample.count = 1;
        desc.multisample.mask = ~0u;
        desc.fragment = &fragmentState;

        rectsPipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
        return rectsPipeline != nullptr;
    }

    void ensureRectBuffers(size_t rectCount) {
        const size_t neededBytes = rectCount * sizeof(RendererRect);
        if (!rectsBuffer || rectsBufferCapacity < neededBytes) {
            if (rectsStorageBindGroup) {
                wgpuBindGroupRelease(rectsStorageBindGroup);
                rectsStorageBindGroup = nullptr;
            }
            if (rectsBuffer) {
                wgpuBufferRelease(rectsBuffer);
            }
            rectsBufferCapacity = std::max(neededBytes, static_cast<size_t>(65536));
            WGPUBufferDescriptor bufferDesc{};
            bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            bufferDesc.size = rectsBufferCapacity;
            rectsBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

            WGPUBindGroupEntry entry{};
            entry.binding = 0;
            entry.buffer = rectsBuffer;
            entry.size = rectsBufferCapacity;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.layout = rectsStorageLayout;
            bgDesc.entryCount = 1;
            bgDesc.entries = &entry;
            rectsStorageBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
        }

        if (!rectsUniformBuffer) {
            WGPUBufferDescriptor uDesc{};
            uDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uDesc.size = 16;
            rectsUniformBuffer = wgpuDeviceCreateBuffer(device, &uDesc);

            WGPUBindGroupEntry entry{};
            entry.binding = 0;
            entry.buffer = rectsUniformBuffer;
            entry.size = 16;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.layout = rectsUniformLayout;
            bgDesc.entryCount = 1;
            bgDesc.entries = &entry;
            rectsUniformBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
        }
    }

    bool initBallsPipelines() {
        ballsVertexModule = createShaderModule(device, ballsVertexWgsl, "ballsVertex");
        ballsFragmentModule = createShaderModule(device, ballsFragmentWgsl, "ballsFragment");
        visibilityClearModule = createShaderModule(device, visibilityClearWgsl, "visibilityClear");
        visibilityBidModule = createShaderModule(device, visibilityBidWgsl, "visibilityBid");
        visibilityResolveModule = createShaderModule(device, visibilityResolveWgsl, "visibilityResolve");
        visibilityCompactModule = createShaderModule(device, visibilityCompactWgsl, "visibilityCompact");

        if (!ballsVertexModule || !ballsFragmentModule || !visibilityClearModule ||
            !visibilityBidModule || !visibilityResolveModule || !visibilityCompactModule) {
            SDL_Log("WebGPU: failed to create balls or visibility shader modules");
            return false;
        }

        // Draw pipeline layouts
        WGPUBindGroupLayoutEntry drawStorageEntry{};
        drawStorageEntry.binding = 0;
        drawStorageEntry.visibility = WGPUShaderStage_Vertex;
        drawStorageEntry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

        WGPUBindGroupLayoutDescriptor drawStorageDesc{};
        drawStorageDesc.entryCount = 1;
        drawStorageDesc.entries = &drawStorageEntry;
        ballDrawStorageLayout = wgpuDeviceCreateBindGroupLayout(device, &drawStorageDesc);

        WGPUBindGroupLayoutEntry drawUniformEntry{};
        drawUniformEntry.binding = 0;
        drawUniformEntry.visibility = WGPUShaderStage_Vertex;
        drawUniformEntry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor drawUniformDesc{};
        drawUniformDesc.entryCount = 1;
        drawUniformDesc.entries = &drawUniformEntry;
        ballDrawUniformLayout = wgpuDeviceCreateBindGroupLayout(device, &drawUniformDesc);

        WGPUBindGroupLayout drawBgLayouts[2] = { ballDrawStorageLayout, ballDrawUniformLayout };
        WGPUPipelineLayoutDescriptor drawPipelineLayoutDesc{};
        drawPipelineLayoutDesc.bindGroupLayoutCount = 2;
        drawPipelineLayoutDesc.bindGroupLayouts = drawBgLayouts;
        ballPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &drawPipelineLayoutDesc);

        WGPUColorTargetState colorTarget{};
        colorTarget.format = surfaceFormat;
        colorTarget.blend = nullptr; // Opaque / discard in fragment shader
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState{};
        fragmentState.module = ballsFragmentModule;
        fragmentState.entryPoint = WGPUStringView{"fragmentMain", 12};
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPUVertexState vertexState{};
        vertexState.module = ballsVertexModule;
        vertexState.entryPoint = WGPUStringView{"vertexMain", 10};

        WGPURenderPipelineDescriptor renderPipeDesc{};
        renderPipeDesc.layout = ballPipelineLayout;
        renderPipeDesc.vertex = vertexState;
        renderPipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        renderPipeDesc.primitive.cullMode = WGPUCullMode_None;
        renderPipeDesc.primitive.frontFace = WGPUFrontFace_CCW;
        renderPipeDesc.multisample.count = 1;
        renderPipeDesc.multisample.mask = ~0u;
        renderPipeDesc.fragment = &fragmentState;

        ballPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipeDesc);

        // Compute layouts
        WGPUBindGroupLayoutEntry compInEntry{};
        compInEntry.binding = 0;
        compInEntry.visibility = WGPUShaderStage_Compute;
        compInEntry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

        WGPUBindGroupLayoutDescriptor compInDesc{};
        compInDesc.entryCount = 1;
        compInDesc.entries = &compInEntry;
        computeInputLayout = wgpuDeviceCreateBindGroupLayout(device, &compInDesc);

        WGPUBindGroupLayoutEntry compOutEntries[3]{};
        for (uint32_t i = 0; i < 3; ++i) {
            compOutEntries[i].binding = i;
            compOutEntries[i].visibility = WGPUShaderStage_Compute;
            compOutEntries[i].buffer.type = WGPUBufferBindingType_Storage;
        }
        WGPUBindGroupLayoutDescriptor compOutDesc{};
        compOutDesc.entryCount = 3;
        compOutDesc.entries = compOutEntries;
        computeOutputLayout = wgpuDeviceCreateBindGroupLayout(device, &compOutDesc);

        WGPUBindGroupLayoutEntry compConstEntry{};
        compConstEntry.binding = 0;
        compConstEntry.visibility = WGPUShaderStage_Compute;
        compConstEntry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor compConstDesc{};
        compConstDesc.entryCount = 1;
        compConstDesc.entries = &compConstEntry;
        computeConstantsLayout = wgpuDeviceCreateBindGroupLayout(device, &compConstDesc);

        WGPUBindGroupLayout compBgLayouts[3] = { computeInputLayout, computeOutputLayout, computeConstantsLayout };
        WGPUPipelineLayoutDescriptor compPipelineLayoutDesc{};
        compPipelineLayoutDesc.bindGroupLayoutCount = 3;
        compPipelineLayoutDesc.bindGroupLayouts = compBgLayouts;
        computePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &compPipelineLayoutDesc);

        auto createCompPipeline = [&](WGPUShaderModule mod) {
            WGPUComputePipelineDescriptor desc{};
            desc.layout = computePipelineLayout;
            desc.compute.module = mod;
            desc.compute.entryPoint = WGPUStringView{"computeMain", 11};
            return wgpuDeviceCreateComputePipeline(device, &desc);
        };

        clearPipeline = createCompPipeline(visibilityClearModule);
        bidPipeline = createCompPipeline(visibilityBidModule);
        resolvePipeline = createCompPipeline(visibilityResolveModule);
        compactPipeline = createCompPipeline(visibilityCompactModule);

        // Uniform buffers
        WGPUBufferDescriptor ballUDesc{};
        ballUDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        ballUDesc.size = (sizeof(BallUniforms) + 15) & ~15ull;
        ballUniformBuffer = wgpuDeviceCreateBuffer(device, &ballUDesc);

        WGPUBindGroupEntry ballUEntry{};
        ballUEntry.binding = 0;
        ballUEntry.buffer = ballUniformBuffer;
        ballUEntry.size = ballUDesc.size;

        WGPUBindGroupDescriptor ballUBgDesc{};
        ballUBgDesc.layout = ballDrawUniformLayout;
        ballUBgDesc.entryCount = 1;
        ballUBgDesc.entries = &ballUEntry;
        ballUniformBindGroup = wgpuDeviceCreateBindGroup(device, &ballUBgDesc);

        for (int i = 0; i < 4; ++i) {
            WGPUBufferDescriptor visUDesc{};
            visUDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            visUDesc.size = (sizeof(VisibilityUniforms) + 15) & ~15ull;
            visibilityUniformBuffers[i] = wgpuDeviceCreateBuffer(device, &visUDesc);

            WGPUBindGroupEntry visUEntry{};
            visUEntry.binding = 0;
            visUEntry.buffer = visibilityUniformBuffers[i];
            visUEntry.size = visUDesc.size;

            WGPUBindGroupDescriptor visUBgDesc{};
            visUBgDesc.layout = computeConstantsLayout;
            visUBgDesc.entryCount = 1;
            visUBgDesc.entries = &visUEntry;
            visibilityConstantsBindGroups[i] = wgpuDeviceCreateBindGroup(device, &visUBgDesc);
        }

        WGPUBufferDescriptor argDesc{};
        argDesc.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        argDesc.size = 64;
        argumentBuffer = wgpuDeviceCreateBuffer(device, &argDesc);

        return ballPipeline && clearPipeline && bidPipeline && resolvePipeline && compactPipeline;
    }

    void releaseBallBindGroups() {
        if (ballsInputSet) {
            wgpuBindGroupRelease(ballsInputSet);
            ballsInputSet = nullptr;
        }
        if (winnersInputSet) {
            wgpuBindGroupRelease(winnersInputSet);
            winnersInputSet = nullptr;
        }
        if (clearOutputSet) {
            wgpuBindGroupRelease(clearOutputSet);
            clearOutputSet = nullptr;
        }
        if (bidOutputSet) {
            wgpuBindGroupRelease(bidOutputSet);
            bidOutputSet = nullptr;
        }
        if (resolveOutputSet) {
            wgpuBindGroupRelease(resolveOutputSet);
            resolveOutputSet = nullptr;
        }
        if (compactOutputSet) {
            wgpuBindGroupRelease(compactOutputSet);
            compactOutputSet = nullptr;
        }
        if (ballStorageBindGroup) {
            wgpuBindGroupRelease(ballStorageBindGroup);
            ballStorageBindGroup = nullptr;
        }
    }

    void ensureBallBuffers(size_t ballCount, size_t gridCellCount, size_t visibleCount) {
        bool needRebind = false;

        const size_t neededBallBytes = std::max(ballCount * sizeof(uint32_t), static_cast<size_t>(65536));
        if (!ballBuffer || ballCapacity < neededBallBytes) {
            if (ballBuffer) {
                wgpuBufferRelease(ballBuffer);
            }
            ballCapacity = neededBallBytes;
            WGPUBufferDescriptor desc{};
            desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            desc.size = (ballCapacity + 3) & ~3ull;
            ballBuffer = wgpuDeviceCreateBuffer(device, &desc);
            needRebind = true;
        }

        const size_t neededGridBytes = std::max(gridCellCount * sizeof(uint32_t), static_cast<size_t>(65536));
        if (!priorityBuffer || priorityCapacity < neededGridBytes) {
            if (priorityBuffer) {
                wgpuBufferRelease(priorityBuffer);
            }
            if (winnerBuffer) {
                wgpuBufferRelease(winnerBuffer);
            }
            priorityCapacity = neededGridBytes;
            winnerCapacity = neededGridBytes;

            WGPUBufferDescriptor desc{};
            desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            desc.size = (priorityCapacity + 3) & ~3ull;
            priorityBuffer = wgpuDeviceCreateBuffer(device, &desc);
            winnerBuffer = wgpuDeviceCreateBuffer(device, &desc);
            needRebind = true;
        }

        const size_t neededVisibleBytes = std::max(visibleCount * sizeof(uint32_t), static_cast<size_t>(65536));
        if (!visibleBuffer || visibleCapacity < neededVisibleBytes) {
            if (visibleBuffer) {
                wgpuBufferRelease(visibleBuffer);
            }
            visibleCapacity = neededVisibleBytes;
            WGPUBufferDescriptor desc{};
            desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            desc.size = (visibleCapacity + 3) & ~3ull;
            visibleBuffer = wgpuDeviceCreateBuffer(device, &desc);
            needRebind = true;
        }

        if (needRebind) {
            releaseBallBindGroups();

            auto makeStorageIn = [&](WGPUBuffer buf, size_t sz) {
                WGPUBindGroupEntry entry{};
                entry.binding = 0;
                entry.buffer = buf;
                entry.size = sz;
                WGPUBindGroupDescriptor bgDesc{};
                bgDesc.layout = computeInputLayout;
                bgDesc.entryCount = 1;
                bgDesc.entries = &entry;
                return wgpuDeviceCreateBindGroup(device, &bgDesc);
            };

            ballsInputSet = makeStorageIn(ballBuffer, ballCapacity);
            winnersInputSet = makeStorageIn(winnerBuffer, winnerCapacity);

            auto makeOutputs = [&](WGPUBuffer b0, size_t s0, WGPUBuffer b1, size_t s1, WGPUBuffer b2, size_t s2) {
                WGPUBindGroupEntry entries[3]{};
                entries[0].binding = 0;
                entries[0].buffer = b0;
                entries[0].size = s0;

                entries[1].binding = 1;
                entries[1].buffer = b1;
                entries[1].size = s1;

                entries[2].binding = 2;
                entries[2].buffer = b2;
                entries[2].size = s2;

                WGPUBindGroupDescriptor bgDesc{};
                bgDesc.layout = computeOutputLayout;
                bgDesc.entryCount = 3;
                bgDesc.entries = entries;
                return wgpuDeviceCreateBindGroup(device, &bgDesc);
            };

            clearOutputSet = makeOutputs(priorityBuffer, priorityCapacity, winnerBuffer, winnerCapacity, argumentBuffer, 64);
            bidOutputSet = makeOutputs(priorityBuffer, priorityCapacity, winnerBuffer, winnerCapacity, argumentBuffer, 64);
            resolveOutputSet = makeOutputs(priorityBuffer, priorityCapacity, winnerBuffer, winnerCapacity, argumentBuffer, 64);
            compactOutputSet = makeOutputs(visibleBuffer, visibleCapacity, argumentBuffer, 64, priorityBuffer, priorityCapacity);

            WGPUBindGroupEntry drawEntry{};
            drawEntry.binding = 0;
            drawEntry.buffer = (compactPipeline && argumentBuffer) ? visibleBuffer : ballBuffer;
            drawEntry.size = (compactPipeline && argumentBuffer) ? visibleCapacity : ballCapacity;

            WGPUBindGroupDescriptor drawBgDesc{};
            drawBgDesc.layout = ballDrawStorageLayout;
            drawBgDesc.entryCount = 1;
            drawBgDesc.entries = &drawEntry;
            ballStorageBindGroup = wgpuDeviceCreateBindGroup(device, &drawBgDesc);
        }
    }

    void runBallVisibility(WGPUCommandEncoder encoder) {
        const BallDrawRequest& request = pendingBalls;
        const float slotSize =
            std::max(request.radiusCells * request.slotsPerRadius, 1.0f / std::max(request.cellPixels, 1.0f));
        const uint32_t gridWidth =
            std::max<uint32_t>(static_cast<uint32_t>(std::ceil(request.fieldWidth / slotSize)), 1);
        const uint32_t gridHeight =
            std::max<uint32_t>(static_cast<uint32_t>(std::ceil(request.fieldHeight / slotSize)), 1);
        const size_t gridCells = static_cast<size_t>(gridWidth) * gridHeight;
        const bool bidForSlots = !request.isVisibilityGrid && request.count > gridCells;
        const size_t visibleCount = bidForSlots ? gridCells : request.count;

        ensureBallBuffers(request.count, gridCells, visibleCount);
        wgpuQueueWriteBuffer(queue, ballBuffer, 0, request.packed, request.count * sizeof(uint32_t));

        VisibilityUniforms uniforms{};
        uniforms.gridWidth = gridWidth;
        uniforms.gridHeight = gridHeight;
        uniforms.frameSeed = ++frameSeed;
        uniforms.coordinateToSlotX = static_cast<float>(gridWidth) / static_cast<float>(renderCoordinateRange);
        uniforms.coordinateToSlotY = static_cast<float>(gridHeight) / static_cast<float>(renderCoordinateRange);
        uniforms.bidderMask = bidderMaskFor(request.count, gridCells);

        // 0: clear
        uniforms.itemCount = static_cast<uint32_t>(bidForSlots ? gridCells : 0);
        wgpuQueueWriteBuffer(queue, visibilityUniformBuffers[0], 0, &uniforms, sizeof(uniforms));

        // 1: bid
        uniforms.itemCount = static_cast<uint32_t>(request.count);
        wgpuQueueWriteBuffer(queue, visibilityUniformBuffers[1], 0, &uniforms, sizeof(uniforms));

        // 2: resolve
        wgpuQueueWriteBuffer(queue, visibilityUniformBuffers[2], 0, &uniforms, sizeof(uniforms));

        // 3: compact
        uniforms.itemCount = static_cast<uint32_t>(bidForSlots ? gridCells : request.count);
        wgpuQueueWriteBuffer(queue, visibilityUniformBuffers[3], 0, &uniforms, sizeof(uniforms));

        WGPUComputePassDescriptor computeDesc{};
        WGPUComputePassEncoder cpass = wgpuCommandEncoderBeginComputePass(encoder, &computeDesc);

        auto dispatch = [&](WGPUComputePipeline pipe, WGPUBindGroup inSet, WGPUBindGroup outSet,
                            WGPUBindGroup constSet, size_t threads) {
            wgpuComputePassEncoderSetPipeline(cpass, pipe);
            wgpuComputePassEncoderSetBindGroup(cpass, 0, inSet, 0, nullptr);
            wgpuComputePassEncoderSetBindGroup(cpass, 1, outSet, 0, nullptr);
            wgpuComputePassEncoderSetBindGroup(cpass, 2, constSet, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(cpass, groupsFor(threads), 1, 1);
        };

        const size_t clearThreads = std::max<size_t>(bidForSlots ? gridCells : 0, 1);
        dispatch(clearPipeline, ballsInputSet, clearOutputSet, visibilityConstantsBindGroups[0], clearThreads);

        if (bidForSlots) {
            dispatch(bidPipeline, ballsInputSet, bidOutputSet, visibilityConstantsBindGroups[1], request.count);
            dispatch(resolvePipeline, ballsInputSet, resolveOutputSet, visibilityConstantsBindGroups[2], request.count);
        }

        const size_t compactThreads = bidForSlots ? gridCells : request.count;
        dispatch(compactPipeline, bidForSlots ? winnersInputSet : ballsInputSet, compactOutputSet,
                 visibilityConstantsBindGroups[3], compactThreads);

        wgpuComputePassEncoderEnd(cpass);
        wgpuComputePassEncoderRelease(cpass);
    }

    bool initImguiPipeline() {
        imguiVertexModule = createShaderModule(device, imguiVertexWgsl, "imguiVertex");
        imguiFragmentModule = createShaderModule(device, imguiFragmentWgsl, "imguiFragment");
        if (!imguiVertexModule || !imguiFragmentModule) {
            SDL_Log("WebGPU: failed to create imgui shader modules");
            return false;
        }

        WGPUBindGroupLayoutEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutDescriptor layoutDesc{};
        layoutDesc.entryCount = 3;
        layoutDesc.entries = entries;
        imguiBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &imguiBindGroupLayout;
        imguiPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUBufferDescriptor uDesc{};
        uDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uDesc.size = 16;
        imguiUniformBuffer = wgpuDeviceCreateBuffer(device, &uDesc);

        WGPUSamplerDescriptor samplerDesc{};
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDesc.lodMinClamp = 0.0f;
        samplerDesc.lodMaxClamp = 32.0f;
        samplerDesc.maxAnisotropy = 1;
        imguiSampler = wgpuDeviceCreateSampler(device, &samplerDesc);

        WGPUVertexAttribute vertAttrs[3]{};
        vertAttrs[0].format = WGPUVertexFormat_Float32x2;
        vertAttrs[0].offset = offsetof(ImDrawVert, pos);
        vertAttrs[0].shaderLocation = 0;

        vertAttrs[1].format = WGPUVertexFormat_Float32x2;
        vertAttrs[1].offset = offsetof(ImDrawVert, uv);
        vertAttrs[1].shaderLocation = 1;

        vertAttrs[2].format = WGPUVertexFormat_Unorm8x4;
        vertAttrs[2].offset = offsetof(ImDrawVert, col);
        vertAttrs[2].shaderLocation = 2;

        WGPUVertexBufferLayout vbLayout{};
        vbLayout.arrayStride = sizeof(ImDrawVert);
        vbLayout.stepMode = WGPUVertexStepMode_Vertex;
        vbLayout.attributeCount = 3;
        vbLayout.attributes = vertAttrs;

        WGPUVertexState vertexState{};
        vertexState.module = imguiVertexModule;
        vertexState.entryPoint = WGPUStringView{"vertexMain", 10};
        vertexState.bufferCount = 1;
        vertexState.buffers = &vbLayout;

        WGPUBlendState blend{};
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = surfaceFormat;
        colorTarget.blend = &blend;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState{};
        fragmentState.module = imguiFragmentModule;
        fragmentState.entryPoint = WGPUStringView{"fragmentMain", 12};
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPURenderPipelineDescriptor desc{};
        desc.layout = imguiPipelineLayout;
        desc.vertex = vertexState;
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.primitive.frontFace = WGPUFrontFace_CCW;
        desc.multisample.count = 1;
        desc.multisample.mask = ~0u;
        desc.fragment = &fragmentState;

        imguiPipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
        return imguiPipeline != nullptr;
    }

    void createImguiTexture(ImTextureData* texture) {
        if (!texture->GetPixels()) {
            return;
        }

        auto* binding = static_cast<ImguiTextureBinding*>(texture->BackendUserData);
        if (binding && (binding->width != static_cast<uint32_t>(texture->Width) ||
                        binding->height != static_cast<uint32_t>(texture->Height))) {
            destroyImguiTexture(texture);
            binding = nullptr;
        }

        if (!binding) {
            binding = new ImguiTextureBinding();
            binding->width = static_cast<uint32_t>(texture->Width);
            binding->height = static_cast<uint32_t>(texture->Height);

            WGPUTextureDescriptor desc{};
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = { binding->width, binding->height, 1 };
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;
            binding->texture = wgpuDeviceCreateTexture(device, &desc);
            binding->view = wgpuTextureCreateView(binding->texture, nullptr);

            WGPUBindGroupEntry bgEntries[3]{};
            bgEntries[0].binding = 0;
            bgEntries[0].buffer = imguiUniformBuffer;
            bgEntries[0].offset = 0;
            bgEntries[0].size = 16;

            bgEntries[1].binding = 1;
            bgEntries[1].textureView = binding->view;

            bgEntries[2].binding = 2;
            bgEntries[2].sampler = imguiSampler;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.layout = imguiBindGroupLayout;
            bgDesc.entryCount = 3;
            bgDesc.entries = bgEntries;
            binding->bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

            texture->BackendUserData = binding;
            texture->SetTexID(reinterpret_cast<ImTextureID>(binding));
        }

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = binding->texture;
        dst.mipLevel = 0;
        dst.origin = { 0, 0, 0 };
        dst.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = static_cast<uint32_t>(texture->Width) * texture->BytesPerPixel;
        layout.rowsPerImage = static_cast<uint32_t>(texture->Height);

        WGPUExtent3D writeSize = { static_cast<uint32_t>(texture->Width), static_cast<uint32_t>(texture->Height), 1 };
        wgpuQueueWriteTexture(queue, &dst, texture->GetPixels(),
                              static_cast<size_t>(texture->Width) * texture->Height * texture->BytesPerPixel,
                              &layout, &writeSize);
        texture->SetStatus(ImTextureStatus_OK);
    }

    void destroyImguiTexture(ImTextureData* texture) {
        auto* binding = static_cast<ImguiTextureBinding*>(texture->BackendUserData);
        if (binding) {
            if (binding->bindGroup) {
                wgpuBindGroupRelease(binding->bindGroup);
            }
            if (binding->view) {
                wgpuTextureViewRelease(binding->view);
            }
            if (binding->texture) {
                wgpuTextureDestroy(binding->texture);
                wgpuTextureRelease(binding->texture);
            }
            delete binding;
            texture->BackendUserData = nullptr;
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    void updateImguiTextures(ImDrawData* drawData) {
        if (!drawData->Textures) {
            return;
        }
        for (ImTextureData* texture : *drawData->Textures) {
            if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
                createImguiTexture(texture);
            } else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0) {
                destroyImguiTexture(texture);
            }
        }
    }

    void ensureImguiBuffers(size_t vertexCount, size_t indexCount) {
        const size_t neededVertexBytes = vertexCount * sizeof(ImDrawVert);
        if (!imguiVertexBuffer || imguiVertexCapacity < neededVertexBytes) {
            if (imguiVertexBuffer) {
                wgpuBufferRelease(imguiVertexBuffer);
            }
            imguiVertexCapacity = std::max(neededVertexBytes, static_cast<size_t>(65536));
            WGPUBufferDescriptor desc{};
            desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            desc.size = (imguiVertexCapacity + 3) & ~3ull;
            imguiVertexBuffer = wgpuDeviceCreateBuffer(device, &desc);
        }

        const size_t neededIndexBytes = indexCount * sizeof(ImDrawIdx);
        if (!imguiIndexBuffer || imguiIndexCapacity < neededIndexBytes) {
            if (imguiIndexBuffer) {
                wgpuBufferRelease(imguiIndexBuffer);
            }
            imguiIndexCapacity = std::max(neededIndexBytes, static_cast<size_t>(65536));
            WGPUBufferDescriptor desc{};
            desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            desc.size = (imguiIndexCapacity + 3) & ~3ull;
            imguiIndexBuffer = wgpuDeviceCreateBuffer(device, &desc);
        }
    }

    void renderImgui(WGPURenderPassEncoder pass, ImDrawData* drawData) {
        const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
        const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
        if (framebufferWidth <= 0.0f || framebufferHeight <= 0.0f || drawData->TotalVtxCount <= 0) {
            return;
        }

        std::vector<ImDrawVert> vertices;
        std::vector<ImDrawIdx> indices;
        vertices.reserve(static_cast<size_t>(drawData->TotalVtxCount));
        indices.reserve(static_cast<size_t>(drawData->TotalIdxCount));
        for (const ImDrawList* list : drawData->CmdLists) {
            vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
            indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
        }

        if (indices.size() % 2 != 0) { indices.push_back(0); }
        ensureImguiBuffers(vertices.size(), indices.size());
        wgpuQueueWriteBuffer(queue, imguiVertexBuffer, 0, vertices.data(),
                             vertices.size() * sizeof(ImDrawVert));
        wgpuQueueWriteBuffer(queue, imguiIndexBuffer, 0, indices.data(),
                             indices.size() * sizeof(ImDrawIdx));

        const float uniforms[4] = {
            2.0f / drawData->DisplaySize.x,
            -2.0f / drawData->DisplaySize.y,
            -1.0f - drawData->DisplayPos.x * 2.0f / drawData->DisplaySize.x,
            1.0f + drawData->DisplayPos.y * 2.0f / drawData->DisplaySize.y
        };
        wgpuQueueWriteBuffer(queue, imguiUniformBuffer, 0, uniforms, sizeof(uniforms));

        wgpuRenderPassEncoderSetPipeline(pass, imguiPipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, imguiVertexBuffer, 0, WGPU_WHOLE_SIZE);
        const WGPUIndexFormat indexFormat =
            sizeof(ImDrawIdx) == 2 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        wgpuRenderPassEncoderSetIndexBuffer(pass, imguiIndexBuffer, indexFormat, 0, WGPU_WHOLE_SIZE);

        const ImVec2 clipOffset = drawData->DisplayPos;
        const ImVec2 clipScale = drawData->FramebufferScale;
        uint32_t vertexBase = 0;
        uint32_t indexBase = 0;

        for (const ImDrawList* list : drawData->CmdLists) {
            for (const ImDrawCmd& cmd : list->CmdBuffer) {
                if (cmd.UserCallback) {
                    if (cmd.UserCallback != ImDrawCallback_ResetRenderState) {
                        cmd.UserCallback(list, &cmd);
                    }
                    continue;
                }

                const float left = std::max((cmd.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f);
                const float top = std::max((cmd.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f);
                const float right = std::min((cmd.ClipRect.z - clipOffset.x) * clipScale.x, framebufferWidth);
                const float bottom = std::min((cmd.ClipRect.w - clipOffset.y) * clipScale.y, framebufferHeight);
                if (right <= left || bottom <= top) {
                    continue;
                }

                const auto* binding =
                    reinterpret_cast<const ImguiTextureBinding*>(static_cast<intptr_t>(cmd.GetTexID()));
                if (!binding || !binding->bindGroup) {
                    continue;
                }

                wgpuRenderPassEncoderSetScissorRect(pass, static_cast<uint32_t>(left),
                                                    static_cast<uint32_t>(top),
                                                    static_cast<uint32_t>(right - left),
                                                    static_cast<uint32_t>(bottom - top));
                wgpuRenderPassEncoderSetBindGroup(pass, 0, binding->bindGroup, 0, nullptr);
                wgpuRenderPassEncoderDrawIndexed(pass, cmd.ElemCount, 1,
                                                 indexBase + cmd.IdxOffset,
                                                 static_cast<int32_t>(vertexBase + cmd.VtxOffset), 0);
            }
            vertexBase += static_cast<uint32_t>(list->VtxBuffer.Size);
            indexBase += static_cast<uint32_t>(list->IdxBuffer.Size);
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

    std::vector<RendererRect> pendingRects;
    WGPUShaderModule rectsVertexModule = nullptr;
    WGPUShaderModule rectsFragmentModule = nullptr;
    WGPUBindGroupLayout rectsStorageLayout = nullptr;
    WGPUBindGroupLayout rectsUniformLayout = nullptr;
    WGPUPipelineLayout rectsPipelineLayout = nullptr;
    WGPURenderPipeline rectsPipeline = nullptr;
    WGPUBuffer rectsBuffer = nullptr;
    size_t rectsBufferCapacity = 0;
    WGPUBuffer rectsUniformBuffer = nullptr;
    WGPUBindGroup rectsStorageBindGroup = nullptr;
    WGPUBindGroup rectsUniformBindGroup = nullptr;

    bool hasBalls = false;
    BallDrawRequest pendingBalls;
    uint32_t frameSeed = 0;

    WGPUShaderModule ballsVertexModule = nullptr;
    WGPUShaderModule ballsFragmentModule = nullptr;
    WGPUShaderModule visibilityClearModule = nullptr;
    WGPUShaderModule visibilityBidModule = nullptr;
    WGPUShaderModule visibilityResolveModule = nullptr;
    WGPUShaderModule visibilityCompactModule = nullptr;

    WGPUBindGroupLayout ballDrawStorageLayout = nullptr;
    WGPUBindGroupLayout ballDrawUniformLayout = nullptr;
    WGPUPipelineLayout ballPipelineLayout = nullptr;
    WGPURenderPipeline ballPipeline = nullptr;

    WGPUBindGroupLayout computeInputLayout = nullptr;
    WGPUBindGroupLayout computeOutputLayout = nullptr;
    WGPUBindGroupLayout computeConstantsLayout = nullptr;
    WGPUPipelineLayout computePipelineLayout = nullptr;

    WGPUComputePipeline clearPipeline = nullptr;
    WGPUComputePipeline bidPipeline = nullptr;
    WGPUComputePipeline resolvePipeline = nullptr;
    WGPUComputePipeline compactPipeline = nullptr;

    WGPUBuffer ballBuffer = nullptr;
    size_t ballCapacity = 0;
    WGPUBuffer priorityBuffer = nullptr;
    size_t priorityCapacity = 0;
    WGPUBuffer winnerBuffer = nullptr;
    size_t winnerCapacity = 0;
    WGPUBuffer visibleBuffer = nullptr;
    size_t visibleCapacity = 0;
    WGPUBuffer argumentBuffer = nullptr;

    WGPUBuffer ballUniformBuffer = nullptr;
    WGPUBindGroup ballUniformBindGroup = nullptr;
    WGPUBindGroup ballStorageBindGroup = nullptr;

    WGPUBuffer visibilityUniformBuffers[4]{};
    WGPUBindGroup visibilityConstantsBindGroups[4]{};

    WGPUBindGroup ballsInputSet = nullptr;
    WGPUBindGroup winnersInputSet = nullptr;
    WGPUBindGroup clearOutputSet = nullptr;
    WGPUBindGroup bidOutputSet = nullptr;
    WGPUBindGroup resolveOutputSet = nullptr;
    WGPUBindGroup compactOutputSet = nullptr;

    bool imguiReady = false;
    WGPUShaderModule imguiVertexModule = nullptr;
    WGPUShaderModule imguiFragmentModule = nullptr;
    WGPUBindGroupLayout imguiBindGroupLayout = nullptr;
    WGPUPipelineLayout imguiPipelineLayout = nullptr;
    WGPURenderPipeline imguiPipeline = nullptr;
    WGPUBuffer imguiUniformBuffer = nullptr;
    WGPUSampler imguiSampler = nullptr;
    WGPUBuffer imguiVertexBuffer = nullptr;
    size_t imguiVertexCapacity = 0;
    WGPUBuffer imguiIndexBuffer = nullptr;
    size_t imguiIndexCapacity = 0;
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

    // Create device with validation error and device lost callbacks
    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type,
                                                        WGPUStringView message, void*, void*) {
        const std::string_view msg = fromStringView(message);
        SDL_Log("WebGPU validation error [%d]: %.*s", static_cast<int>(type),
                static_cast<int>(msg.size()), msg.data());
    };
    deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    deviceDesc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason,
                                                    WGPUStringView message, void*, void*) {
        const std::string_view msg = fromStringView(message);
        SDL_Log("WebGPU DEVICE LOST [%d]: %.*s", static_cast<int>(reason),
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

    SDL_Log("WebGPU: requesting device on adapter '%s'...", chosenName.c_str());
    WGPUFuture future = wgpuAdapterRequestDevice(chosenAdapter, &deviceDesc, callbackInfo);
    WGPUFutureWaitInfo waitInfo{future, false};
    while (!payload.complete) {
        wgpuInstanceWaitAny(instance, 1, &waitInfo, 0);
        wgpuInstanceProcessEvents(instance);
    }
    SDL_Log("WebGPU: device request returned, device=%p, error='%s'", payload.device, payload.error.c_str());

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
        bool foundUnorm = false;
        for (size_t i = 0; i < caps.formatCount; ++i) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm ||
                caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
                surfaceFormat = caps.formats[i];
                foundUnorm = true;
                break;
            }
        }
        if (!foundUnorm && caps.formatCount > 0) {
            surfaceFormat = caps.formats[0];
        }
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    SDL_Log("WebGPU: initialized successfully with backend %s, adapter '%s'", normalized.c_str(), chosenName.c_str());

    return std::make_unique<WebGpuRenderer>(window, instance, chosenAdapter, device, queue, surface,
                                            normalized, chosenName, surfaceFormat);
}
