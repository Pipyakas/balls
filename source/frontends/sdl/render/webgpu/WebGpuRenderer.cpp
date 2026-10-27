// Native WebGPU renderer (wgpu-native / Google Dawn, webgpu.h).
//
// Step 1: instance, adapter, device, surface, present mode, clear pass, resize.
// Step 2: rects pipeline, storage buffer upload, instanced quad draw.
// Step 3: imgui pipeline, texture management, vertex/index upload, scissor clipping.

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

        const bool statusOk = (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                               surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
        if (!statusOk || !surfaceTexture.texture) {
            return false;
        }

        currentSurfaceTexture = surfaceTexture.texture;
        currentSurfaceView = wgpuTextureCreateView(currentSurfaceTexture, nullptr);
        frameStartTime = Clock::now();
        pendingRects.clear();

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        return true;
    }

    void drawRects(std::span<const RendererRect> rects) override {
        pendingRects.assign(rects.begin(), rects.end());
    }

    void drawBalls(const BallDrawRequest& request) override {
        // Will be populated in Step 4
        (void)request;
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

        if (!pendingRects.empty() && rectsPipeline) {
            ensureRectBuffers(pendingRects.size());
            wgpuQueueWriteBuffer(queue, rectsBuffer, 0, pendingRects.data(),
                                 pendingRects.size() * sizeof(RendererRect));
            const float uniforms[4] = { logicalWidth, logicalHeight, 0.0f, 0.0f };
            wgpuQueueWriteBuffer(queue, rectsUniformBuffer, 0, uniforms, sizeof(uniforms));

            wgpuRenderPassEncoderSetPipeline(pass, rectsPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, rectsStorageBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, rectsUniformBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(pendingRects.size()), 0, 0);
        }
        pendingRects.clear();

        if (drawData && drawData->TotalVtxCount > 0 && imguiPipeline) {
            renderImgui(pass, drawData);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);

        const auto submitStart = Clock::now();
        wgpuQueueSubmit(queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);

        const WGPUStatus pres = wgpuSurfacePresent(surface);
        const auto frameEnd = Clock::now();
        lastBusyMs = std::chrono::duration<double, std::milli>(frameEnd - submitStart).count();

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
                }
            }

            WGPUSurfaceConfiguration config{};
            config.device = device;
            config.format = surfaceFormat;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.width = static_cast<uint32_t>(pixelWidth);
            config.height = static_cast<uint32_t>(pixelHeight);
            config.alphaMode = caps.alphaModeCount > 0 ? caps.alphaModes[0] : WGPUCompositeAlphaMode_Opaque;
            config.presentMode = wantedMode;
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
        if (caps.formatCount > 0) {
            surfaceFormat = caps.formats[0];
        }
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    SDL_Log("WebGPU: initialized successfully with backend %s, adapter '%s'", normalized.c_str(), chosenName.c_str());

    return std::make_unique<WebGpuRenderer>(window, instance, chosenAdapter, device, queue, surface,
                                            normalized, chosenName, surfaceFormat);
}
