#include "render/IRenderer.h"

#include "sim/PhysicsBackend.h"

#include "shaders/generated/ShaderBlobs.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <vector>

namespace {

constexpr uint32_t computeThreadsPerGroup = 256;
constexpr uint32_t verticesPerQuad = 6;
constexpr uint32_t emptySlot = 0xFFFFFFFFu;
constexpr uint32_t renderCoordinateRange = 16384;
constexpr size_t biddersPerSlot = 32;
constexpr size_t initialBallCapacity = 1 << 16;
constexpr size_t initialRectCapacity = 4096;
constexpr size_t drawArgumentBytes = 4 * sizeof(uint32_t);
constexpr size_t framesInFlight = 2;
constexpr size_t bytesPerPixel = 4;

using Clock = std::chrono::steady_clock;

struct ShaderCode {
    const unsigned char* data = nullptr;
    size_t size = 0;
};

struct BallUniforms {
    float boardOrigin[2];
    float unpackScale[2];
    float viewportSize[2];
    float cellPixels;
    float radiusPixels;
    float palette[rendererPaletteSize][4];
};

struct RectUniforms {
    float viewportSize[2];
    float unusedPadding[2];
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

struct GrowableBuffer {
    SDL_GPUBuffer* buffer = nullptr;
    size_t capacityBytes = 0;
    SDL_GPUBufferUsageFlags usage = 0;

    bool ensure(SDL_GPUDevice* device, size_t bytes) {
        if (bytes <= capacityBytes && buffer) {
            return false;
        }
        if (buffer) {
            SDL_ReleaseGPUBuffer(device, buffer);
        }
        capacityBytes = std::max(bytes, capacityBytes * 2);
        const SDL_GPUBufferCreateInfo info{usage, static_cast<Uint32>(capacityBytes), 0};
        buffer = SDL_CreateGPUBuffer(device, &info);
        return true;
    }

    void release(SDL_GPUDevice* device) {
        if (buffer) {
            SDL_ReleaseGPUBuffer(device, buffer);
            buffer = nullptr;
        }
        capacityBytes = 0;
    }
};

struct GrowableTransfer {
    SDL_GPUTransferBuffer* buffer = nullptr;
    size_t capacityBytes = 0;
    SDL_GPUTransferBufferUsage usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    void ensure(SDL_GPUDevice* device, size_t bytes) {
        if (bytes <= capacityBytes && buffer) {
            return;
        }
        if (buffer) {
            SDL_ReleaseGPUTransferBuffer(device, buffer);
        }
        capacityBytes = std::max(bytes, capacityBytes * 2);
        const SDL_GPUTransferBufferCreateInfo info{usage, static_cast<Uint32>(capacityBytes), 0};
        buffer = SDL_CreateGPUTransferBuffer(device, &info);
    }

    void release(SDL_GPUDevice* device) {
        if (buffer) {
            SDL_ReleaseGPUTransferBuffer(device, buffer);
            buffer = nullptr;
        }
        capacityBytes = 0;
    }
};

class SdlGpuRenderer final : public IRenderer {
public:
    SdlGpuRenderer(SDL_Window* window, SDL_GPUDevice* device) : window(window), device(device) {}
    ~SdlGpuRenderer() override;

    bool initialize(std::string& error);
    bool beginFrame() override;
    void drawRects(std::span<const RendererRect> rects) override;
    void drawBalls(const BallDrawRequest& request) override;
    void endFrame(ClearColor clearColor) override;

    float viewWidth() const override { return logicalWidth; }
    float viewHeight() const override { return logicalHeight; }
    bool setVsync(bool enabled) override;
    bool isVsyncEnabled() const override { return vsyncEnabled; }
    void requestScreenshot(const std::string& path) override { screenshotPath = path; }
    bool lastScreenshotSucceeded() const override { return screenshotSucceeded; }
    double lastGpuWaitMs() const override { return gpuWaitMs; }
    double lastGpuBusyMs() const override { return gpuBusyMs; }
    std::string deviceDescription() const override;

private:
    ShaderCode pick(const unsigned char* spirv, size_t spirvSize, const unsigned char* dxil, size_t dxilSize) const;
    SDL_GPUShader* createShader(ShaderCode code, SDL_GPUShaderStage stage, Uint32 storageBuffers,
                                Uint32 uniformBuffers) const;
    SDL_GPUGraphicsPipeline* createDrawPipeline(ShaderCode vertex, ShaderCode fragment, bool blended);
    SDL_GPUComputePipeline* createComputePipeline(ShaderCode code, Uint32 readOnlyBuffers, Uint32 readWriteBuffers);
    void ensureRenderTarget();
    void waitForOldestFrame();
    void uploadFrameData(SDL_GPUCommandBuffer* commandBuffer);
    void runBallVisibility(SDL_GPUCommandBuffer* commandBuffer);
    void dispatch(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUComputePipeline* pipeline,
                  std::span<SDL_GPUBuffer* const> readOnly, std::span<SDL_GPUBuffer* const> readWrite,
                  const VisibilityUniforms& uniforms, size_t threads);
    void renderScene(SDL_GPUCommandBuffer* commandBuffer, ClearColor clearColor, ImDrawData* drawData);
    void captureScreenshot(SDL_GPUCommandBuffer* commandBuffer);
    void finishScreenshot(SDL_GPUFence* fence);

    SDL_Window* window;
    SDL_GPUDevice* device;
    SDL_GPUTextureFormat targetFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTexture* renderTarget = nullptr;
    int targetWidth = 0;
    int targetHeight = 0;
    float logicalWidth = 1.0f;
    float logicalHeight = 1.0f;

    SDL_GPUGraphicsPipeline* rectPipeline = nullptr;
    SDL_GPUGraphicsPipeline* ballPipeline = nullptr;
    SDL_GPUComputePipeline* clearPipeline = nullptr;
    SDL_GPUComputePipeline* bidPipeline = nullptr;
    SDL_GPUComputePipeline* resolvePipeline = nullptr;
    SDL_GPUComputePipeline* compactPipeline = nullptr;

    GrowableBuffer rectBuffer{nullptr, 0, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ};
    GrowableBuffer ballBuffer{nullptr, 0, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ};
    GrowableBuffer priorityBuffer{nullptr, 0, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ};
    GrowableBuffer winnerBuffer{nullptr, 0, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ};
    GrowableBuffer visibleBuffer{nullptr, 0, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ};
    GrowableBuffer argumentBuffer{nullptr, 0, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT};
    GrowableTransfer uploadTransfer{nullptr, 0, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD};
    GrowableTransfer screenshotTransfer{nullptr, 0, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD};

    std::vector<RendererRect> pendingRects;
    BallDrawRequest pendingBalls;
    std::vector<uint32_t> ballWords;
    bool hasBalls = false;
    size_t gridCells = 0;
    uint32_t gridWidth = 0;
    uint32_t gridHeight = 0;
    uint32_t frameSeed = 0;

    std::array<SDL_GPUFence*, framesInFlight> fences{};
    std::array<Clock::time_point, framesInFlight> submitTimes{};
    Clock::time_point lastCompletion{};
    size_t fenceCursor = 0;
    double gpuWaitMs = 0.0;
    double gpuBusyMs = 0.0;
    bool vsyncEnabled = true;
    std::string screenshotPath;
    std::string screenshotInFlight;
    bool screenshotSucceeded = false;
    bool imguiReady = false;
};

SdlGpuRenderer::~SdlGpuRenderer() {
    SDL_WaitForGPUIdle(device);
    for (SDL_GPUFence*& fence : fences) {
        if (fence) {
            SDL_ReleaseGPUFence(device, fence);
            fence = nullptr;
        }
    }
    if (imguiReady) {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    for (GrowableBuffer* buffer : {&rectBuffer, &ballBuffer, &priorityBuffer, &winnerBuffer, &visibleBuffer, &argumentBuffer}) {
        buffer->release(device);
    }
    uploadTransfer.release(device);
    screenshotTransfer.release(device);
    for (SDL_GPUGraphicsPipeline* pipeline : {rectPipeline, ballPipeline}) {
        if (pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        }
    }
    for (SDL_GPUComputePipeline* pipeline : {clearPipeline, bidPipeline, resolvePipeline, compactPipeline}) {
        if (pipeline) {
            SDL_ReleaseGPUComputePipeline(device, pipeline);
        }
    }
    if (renderTarget) {
        SDL_ReleaseGPUTexture(device, renderTarget);
    }
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
}

ShaderCode SdlGpuRenderer::pick(const unsigned char* spirv, size_t spirvSize, const unsigned char* dxil,
                                size_t dxilSize) const {
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        return {dxil, dxilSize};
    }
    return {spirv, spirvSize};
}

SDL_GPUShader* SdlGpuRenderer::createShader(ShaderCode code, SDL_GPUShaderStage stage, Uint32 storageBuffers,
                                            Uint32 uniformBuffers) const {
    const bool dxil = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    SDL_GPUShaderCreateInfo info{};
    info.code_size = code.size;
    info.code = code.data;
    info.entrypoint = stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vertexMain" : "fragmentMain";
    info.format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_storage_buffers = storageBuffers;
    info.num_uniform_buffers = uniformBuffers;
    return SDL_CreateGPUShader(device, &info);
}

SDL_GPUGraphicsPipeline* SdlGpuRenderer::createDrawPipeline(ShaderCode vertex, ShaderCode fragment, bool blended) {
    SDL_GPUShader* vertexShader = createShader(vertex, SDL_GPU_SHADERSTAGE_VERTEX, 1, 1);
    SDL_GPUShader* fragmentShader = createShader(fragment, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vertexShader || !fragmentShader) {
        return nullptr;
    }
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = targetFormat;
    if (blended) {
        colorTarget.blend_state.enable_blend = true;
        colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertexShader;
    info.fragment_shader = fragmentShader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1;
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);
    return pipeline;
}

SDL_GPUComputePipeline* SdlGpuRenderer::createComputePipeline(ShaderCode code, Uint32 readOnlyBuffers,
                                                              Uint32 readWriteBuffers) {
    const bool dxil = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    SDL_GPUComputePipelineCreateInfo info{};
    info.code_size = code.size;
    info.code = code.data;
    info.entrypoint = "computeMain";
    info.format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
    info.num_readonly_storage_buffers = readOnlyBuffers;
    info.num_readwrite_storage_buffers = readWriteBuffers;
    info.num_uniform_buffers = 1;
    info.threadcount_x = computeThreadsPerGroup;
    info.threadcount_y = 1;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

bool SdlGpuRenderer::initialize(std::string& error) {
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        error = std::format("SDL_ClaimWindowForGPUDevice: {}", SDL_GetError());
        return false;
    }
    targetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    setVsync(true);
    rectPipeline = createDrawPipeline(pick(rectsVertexSpirv, sizeof(rectsVertexSpirv), rectsVertexDxil, sizeof(rectsVertexDxil)),
                                      pick(rectsFragmentSpirv, sizeof(rectsFragmentSpirv), rectsFragmentDxil, sizeof(rectsFragmentDxil)),
                                      true);
    ballPipeline = createDrawPipeline(pick(ballsVertexSpirv, sizeof(ballsVertexSpirv), ballsVertexDxil, sizeof(ballsVertexDxil)),
                                      pick(ballsFragmentSpirv, sizeof(ballsFragmentSpirv), ballsFragmentDxil, sizeof(ballsFragmentDxil)),
                                      false);
    clearPipeline = createComputePipeline(
        pick(visibilityClearSpirv, sizeof(visibilityClearSpirv), visibilityClearDxil, sizeof(visibilityClearDxil)), 0, 3);
    bidPipeline = createComputePipeline(
        pick(visibilityBidSpirv, sizeof(visibilityBidSpirv), visibilityBidDxil, sizeof(visibilityBidDxil)), 1, 1);
    resolvePipeline = createComputePipeline(
        pick(visibilityResolveSpirv, sizeof(visibilityResolveSpirv), visibilityResolveDxil, sizeof(visibilityResolveDxil)), 1, 2);
    compactPipeline = createComputePipeline(
        pick(visibilityCompactSpirv, sizeof(visibilityCompactSpirv), visibilityCompactDxil, sizeof(visibilityCompactDxil)), 1, 2);
    if (!rectPipeline || !ballPipeline || !clearPipeline || !bidPipeline || !resolvePipeline || !compactPipeline) {
        error = std::format("pipeline creation failed: {}", SDL_GetError());
        return false;
    }
    rectBuffer.ensure(device, initialRectCapacity * sizeof(RendererRect));
    ballBuffer.ensure(device, initialBallCapacity * sizeof(uint32_t));
    visibleBuffer.ensure(device, initialBallCapacity * sizeof(uint32_t));
    priorityBuffer.ensure(device, initialBallCapacity * sizeof(uint32_t));
    winnerBuffer.ensure(device, initialBallCapacity * sizeof(uint32_t));
    argumentBuffer.ensure(device, drawArgumentBytes);

    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device = device;
    imguiInfo.ColorTargetFormat = targetFormat;
    imguiInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&imguiInfo);
    imguiReady = true;
    return true;
}

std::string SdlGpuRenderer::deviceDescription() const { return std::format("SDL_GPU {}", SDL_GetGPUDeviceDriver(device)); }

bool SdlGpuRenderer::setVsync(bool enabled) {
    const SDL_GPUPresentMode wanted = enabled ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
    SDL_GPUPresentMode mode = wanted;
    if (!SDL_WindowSupportsGPUPresentMode(device, window, mode)) {
        mode = enabled ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_MAILBOX;
    }
    if (!SDL_WindowSupportsGPUPresentMode(device, window, mode) ||
        !SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        return false;
    }
    vsyncEnabled = enabled;
    return true;
}

void SdlGpuRenderer::ensureRenderTarget() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    width = std::max(width, 1);
    height = std::max(height, 1);
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    logicalWidth = static_cast<float>(std::max(windowWidth, 1));
    logicalHeight = static_cast<float>(std::max(windowHeight, 1));
    if (renderTarget && width == targetWidth && height == targetHeight) {
        return;
    }
    if (renderTarget) {
        SDL_ReleaseGPUTexture(device, renderTarget);
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = targetFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    renderTarget = SDL_CreateGPUTexture(device, &info);
    targetWidth = width;
    targetHeight = height;
}

void SdlGpuRenderer::waitForOldestFrame() {
    SDL_GPUFence*& oldest = fences[fenceCursor];
    if (!oldest) {
        gpuWaitMs = 0.0;
        return;
    }
    const Clock::time_point start = Clock::now();
    SDL_WaitForGPUFences(device, true, &oldest, 1);
    const Clock::time_point completion = Clock::now();
    gpuWaitMs = std::chrono::duration<double, std::milli>(completion - start).count();
    const Clock::time_point busyStart = std::max(submitTimes[fenceCursor], lastCompletion);
    gpuBusyMs = std::chrono::duration<double, std::milli>(completion - busyStart).count();
    lastCompletion = completion;
    SDL_ReleaseGPUFence(device, oldest);
    oldest = nullptr;
}

bool SdlGpuRenderer::beginFrame() {
    waitForOldestFrame();
    ensureRenderTarget();
    pendingRects.clear();
    hasBalls = false;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

void SdlGpuRenderer::drawRects(std::span<const RendererRect> rects) {
    pendingRects.insert(pendingRects.end(), rects.begin(), rects.end());
}

void SdlGpuRenderer::drawBalls(const BallDrawRequest& request) {
    pendingBalls = request;
    hasBalls = request.count > 0 && request.packed;
    if (hasBalls) {
        ballWords.assign(request.packed, request.packed + request.count);
        pendingBalls.packed = ballWords.data();
    }
}

void SdlGpuRenderer::uploadFrameData(SDL_GPUCommandBuffer* commandBuffer) {
    const size_t rectBytes = pendingRects.size() * sizeof(RendererRect);
    const size_t ballBytes = hasBalls ? pendingBalls.count * sizeof(uint32_t) : 0;
    if (rectBytes + ballBytes == 0) {
        return;
    }
    rectBuffer.ensure(device, std::max<size_t>(rectBytes, sizeof(RendererRect)));
    if (ballBytes > 0) {
        ballBuffer.ensure(device, ballBytes);
    }
    uploadTransfer.ensure(device, rectBytes + ballBytes);
    auto* mapped = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(device, uploadTransfer.buffer, true));
    if (!mapped) {
        return;
    }
    if (rectBytes > 0) {
        std::memcpy(mapped, pendingRects.data(), rectBytes);
    }
    if (ballBytes > 0) {
        std::memcpy(mapped + rectBytes, pendingBalls.packed, ballBytes);
    }
    SDL_UnmapGPUTransferBuffer(device, uploadTransfer.buffer);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (rectBytes > 0) {
        const SDL_GPUTransferBufferLocation source{uploadTransfer.buffer, 0};
        const SDL_GPUBufferRegion destination{rectBuffer.buffer, 0, static_cast<Uint32>(rectBytes)};
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
    }
    if (ballBytes > 0) {
        const SDL_GPUTransferBufferLocation source{uploadTransfer.buffer, static_cast<Uint32>(rectBytes)};
        const SDL_GPUBufferRegion destination{ballBuffer.buffer, 0, static_cast<Uint32>(ballBytes)};
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
    }
    SDL_EndGPUCopyPass(copyPass);
}

void SdlGpuRenderer::dispatch(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUComputePipeline* pipeline,
                              std::span<SDL_GPUBuffer* const> readOnly, std::span<SDL_GPUBuffer* const> readWrite,
                              const VisibilityUniforms& uniforms, size_t threads) {
    std::array<SDL_GPUStorageBufferReadWriteBinding, 3> writable{};
    for (size_t i = 0; i < readWrite.size(); ++i) {
        writable[i].buffer = readWrite[i];
        writable[i].cycle = false;
    }
    SDL_GPUComputePass* pass =
        SDL_BeginGPUComputePass(commandBuffer, nullptr, 0, writable.data(), static_cast<Uint32>(readWrite.size()));
    SDL_BindGPUComputePipeline(pass, pipeline);
    if (!readOnly.empty()) {
        SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly.data(), static_cast<Uint32>(readOnly.size()));
    }
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &uniforms, sizeof(uniforms));
    SDL_DispatchGPUCompute(pass, groupsFor(threads), 1, 1);
    SDL_EndGPUComputePass(pass);
}

void SdlGpuRenderer::runBallVisibility(SDL_GPUCommandBuffer* commandBuffer) {
    const BallDrawRequest& request = pendingBalls;
    const float slotSize = visibilitySlotSize(request.radiusCells, request.cellPixels);
    gridWidth = std::max<uint32_t>(static_cast<uint32_t>(std::ceil(request.fieldWidth / slotSize)), 1);
    gridHeight = std::max<uint32_t>(static_cast<uint32_t>(std::ceil(request.fieldHeight / slotSize)), 1);
    gridCells = static_cast<size_t>(gridWidth) * gridHeight;
    const bool bidForSlots = !request.isVisibilityGrid && request.count > gridCells;
    const size_t visibleCapacity = bidForSlots ? gridCells : request.count;
    priorityBuffer.ensure(device, gridCells * sizeof(uint32_t));
    winnerBuffer.ensure(device, gridCells * sizeof(uint32_t));
    visibleBuffer.ensure(device, visibleCapacity * sizeof(uint32_t));

    VisibilityUniforms uniforms{};
    uniforms.gridWidth = gridWidth;
    uniforms.gridHeight = gridHeight;
    uniforms.frameSeed = ++frameSeed;
    uniforms.coordinateToSlotX = static_cast<float>(gridWidth) / static_cast<float>(renderCoordinateRange);
    uniforms.coordinateToSlotY = static_cast<float>(gridHeight) / static_cast<float>(renderCoordinateRange);
    uniforms.bidderMask = bidderMaskFor(request.count, gridCells);

    uniforms.itemCount = static_cast<uint32_t>(bidForSlots ? gridCells : 0);
    const std::array<SDL_GPUBuffer*, 3> clearTargets{priorityBuffer.buffer, winnerBuffer.buffer, argumentBuffer.buffer};
    dispatch(commandBuffer, clearPipeline, {}, clearTargets, uniforms, std::max<size_t>(uniforms.itemCount, 1));

    const std::array<SDL_GPUBuffer*, 1> balls{ballBuffer.buffer};
    uniforms.itemCount = static_cast<uint32_t>(request.count);
    if (bidForSlots) {
        const std::array<SDL_GPUBuffer*, 1> priorities{priorityBuffer.buffer};
        dispatch(commandBuffer, bidPipeline, balls, priorities, uniforms, request.count);
        const std::array<SDL_GPUBuffer*, 2> resolveTargets{priorityBuffer.buffer, winnerBuffer.buffer};
        dispatch(commandBuffer, resolvePipeline, balls, resolveTargets, uniforms, request.count);
    }
    const std::array<SDL_GPUBuffer*, 1> winners{winnerBuffer.buffer};
    const std::array<SDL_GPUBuffer*, 2> compactTargets{visibleBuffer.buffer, argumentBuffer.buffer};
    uniforms.itemCount = static_cast<uint32_t>(bidForSlots ? gridCells : request.count);
    dispatch(commandBuffer, compactPipeline, bidForSlots ? std::span<SDL_GPUBuffer* const>(winners) : balls,
             compactTargets, uniforms, uniforms.itemCount);
}

void SdlGpuRenderer::renderScene(SDL_GPUCommandBuffer* commandBuffer, ClearColor clearColor, ImDrawData* drawData) {
    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = renderTarget;
    colorTarget.clear_color = {clearColor.red, clearColor.green, clearColor.blue, 1.0f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
    const SDL_GPUViewport viewport{0.0f, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &viewport);

    if (!pendingRects.empty()) {
        const RectUniforms uniforms{{logicalWidth, logicalHeight}, {0.0f, 0.0f}};
        SDL_BindGPUGraphicsPipeline(pass, rectPipeline);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &rectBuffer.buffer, 1);
        SDL_PushGPUVertexUniformData(commandBuffer, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, verticesPerQuad, static_cast<Uint32>(pendingRects.size()), 0, 0);
    }
    if (hasBalls) {
        const BallDrawRequest& request = pendingBalls;
        BallUniforms uniforms{};
        uniforms.boardOrigin[0] = request.originX;
        uniforms.boardOrigin[1] = request.originY;
        uniforms.unpackScale[0] = request.fieldWidth / static_cast<float>(renderCoordinateRange - 1);
        uniforms.unpackScale[1] = request.fieldHeight / static_cast<float>(renderCoordinateRange - 1);
        uniforms.viewportSize[0] = logicalWidth;
        uniforms.viewportSize[1] = logicalHeight;
        uniforms.cellPixels = request.cellPixels;
        uniforms.radiusPixels = std::max(request.radiusCells * request.cellPixels, minimumBallRadiusPixels * logicalWidth / static_cast<float>(targetWidth));
        for (int color = 0; color < rendererPaletteSize; ++color) {
            std::copy(request.palette[color].begin(), request.palette[color].end(), uniforms.palette[color]);
        }
        if (request.clipWidth > 0.0f && request.clipHeight > 0.0f) {
            const float scale = static_cast<float>(targetWidth) / logicalWidth;
            const SDL_Rect scissor{static_cast<int>(request.clipX * scale), static_cast<int>(request.clipY * scale),
                                   static_cast<int>(std::ceil(request.clipWidth * scale)),
                                   static_cast<int>(std::ceil(request.clipHeight * scale))};
            SDL_SetGPUScissor(pass, &scissor);
        }
        SDL_BindGPUGraphicsPipeline(pass, ballPipeline);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &visibleBuffer.buffer, 1);
        SDL_PushGPUVertexUniformData(commandBuffer, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitivesIndirect(pass, argumentBuffer.buffer, 0, 1);
        const SDL_Rect fullTarget{0, 0, targetWidth, targetHeight};
        SDL_SetGPUScissor(pass, &fullTarget);
    }
    if (drawData && drawData->CmdListsCount > 0) {
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, pass);
    }
    SDL_EndGPURenderPass(pass);
}

void SdlGpuRenderer::captureScreenshot(SDL_GPUCommandBuffer* commandBuffer) {
    const size_t bytes = static_cast<size_t>(targetWidth) * targetHeight * bytesPerPixel;
    screenshotTransfer.ensure(device, bytes);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    SDL_GPUTextureRegion region{};
    region.texture = renderTarget;
    region.w = static_cast<Uint32>(targetWidth);
    region.h = static_cast<Uint32>(targetHeight);
    region.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = screenshotTransfer.buffer;
    SDL_DownloadFromGPUTexture(copyPass, &region, &destination);
    SDL_EndGPUCopyPass(copyPass);
    screenshotInFlight = screenshotPath;
    screenshotPath.clear();
}

void SdlGpuRenderer::finishScreenshot(SDL_GPUFence* fence) {
    SDL_WaitForGPUFences(device, true, &fence, 1);
    const void* pixels = SDL_MapGPUTransferBuffer(device, screenshotTransfer.buffer, false);
    screenshotSucceeded = false;
    if (pixels) {
        const SDL_PixelFormat format =
            targetFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_ABGR8888;
        SDL_Surface* surface = SDL_CreateSurfaceFrom(targetWidth, targetHeight, format, const_cast<void*>(pixels),
                                                     targetWidth * static_cast<int>(bytesPerPixel));
        if (surface) {
            screenshotSucceeded = SDL_SaveBMP(surface, screenshotInFlight.c_str());
            SDL_DestroySurface(surface);
        }
        SDL_UnmapGPUTransferBuffer(device, screenshotTransfer.buffer);
    }
    screenshotInFlight.clear();
}

void SdlGpuRenderer::endFrame(ClearColor clearColor) {
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer) {
        return;
    }
    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 swapchainWidth = 0;
    Uint32 swapchainHeight = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &swapchainWidth, &swapchainHeight)) {
        swapchainTexture = nullptr;
    }
    uploadFrameData(commandBuffer);
    if (hasBalls) {
        runBallVisibility(commandBuffer);
    }
    if (drawData) {
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commandBuffer);
    }
    renderScene(commandBuffer, clearColor, drawData);
    if (swapchainTexture) {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = renderTarget;
        blit.source.w = static_cast<Uint32>(targetWidth);
        blit.source.h = static_cast<Uint32>(targetHeight);
        blit.destination.texture = swapchainTexture;
        blit.destination.w = swapchainWidth;
        blit.destination.h = swapchainHeight;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(commandBuffer, &blit);
    }
    const bool capturing = !screenshotPath.empty();
    if (capturing) {
        captureScreenshot(commandBuffer);
    }
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
    if (capturing && fence) {
        finishScreenshot(fence);
    }
    fences[fenceCursor] = fence;
    submitTimes[fenceCursor] = Clock::now();
    fenceCursor = (fenceCursor + 1) % framesInFlight;
}

}

std::unique_ptr<IRenderer> createSdlGpuRenderer(SDL_Window* window, std::string& error) {
    SDL_GPUDevice* device =
        SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL, false, nullptr);
    if (!device) {
        error = std::format("SDL_CreateGPUDevice: {}", SDL_GetError());
        return nullptr;
    }
    auto renderer = std::make_unique<SdlGpuRenderer>(window, device);
    if (!renderer->initialize(error)) {
        return nullptr;
    }
    return renderer;
}
