#include "render/IRenderer.h"

#include "sim/BrickCellImage.h"
#include "sim/PhysicsBackend.h"
#include "render/nvrhi/GraphicsDevice.h"
#include "render/nvrhi/ImguiNvrhi.h"
#include "render/nvrhi/ShaderBytecode.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>

namespace {

constexpr uint32_t computeThreadsPerGroup = 256;
constexpr uint32_t verticesPerQuad = 6;
constexpr uint32_t renderCoordinateRange = 16384;
constexpr size_t biddersPerSlot = 32;
constexpr size_t initialBallCapacity = 1 << 16;
constexpr size_t initialRectCapacity = 4096;
constexpr uint32_t drawArgumentWords = 4;
constexpr size_t framesInFlight = 2;
constexpr uint32_t constantVersions = 64;
constexpr uint32_t storageWord = sizeof(uint32_t);
constexpr uint32_t rectStride = sizeof(RendererRect);

using Clock = std::chrono::steady_clock;

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

struct CellImageUniforms {
    float drawRect[4];
    float boardOrigin[2];
    float viewportSize[2];
    float cellPixels;
    uint32_t columns;
    uint32_t rows;
    uint32_t tilesAcross;
    float palette[rendererPaletteSize * 2][4];
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

nvrhi::BindingLayoutHandle createSpaceLayout(nvrhi::IDevice* device, nvrhi::ShaderType visibility, uint32_t space,
                                             std::initializer_list<nvrhi::BindingLayoutItem> items) {
    nvrhi::BindingLayoutDesc description;
    description.setVisibility(visibility)
        .setRegisterSpaceAndDescriptorSet(space)
        .setBindingOffsets(nvrhi::VulkanBindingOffsets()
                               .setShaderResourceOffset(0)
                               .setSamplerOffset(0)
                               .setConstantBufferOffset(0)
                               .setUnorderedAccessViewOffset(0));
    for (const nvrhi::BindingLayoutItem& item : items) {
        description.addItem(item);
    }
    return device->createBindingLayout(description);
}

class NvrhiRenderer final : public IRenderer {
public:
    NvrhiRenderer(SDL_Window* window, std::unique_ptr<GraphicsDevice> graphics, GraphicsApi api)
        : window(window), api(api), graphics(std::move(graphics)), device(this->graphics->device()) {}
    ~NvrhiRenderer() override;

    bool prepare(std::string& error);
    bool supportsDeferredAttach() const override { return true; }
    bool attachToWindow(std::string& error) override;
    void detachFromWindow() override;
    bool beginFrame() override;
    void drawRects(std::span<const RendererRect> rects) override;
    void drawBalls(const BallDrawRequest& request) override;
    bool drawCellImage(const CellImageDrawRequest& request) override;
    void endFrame(ClearColor clearColor) override;

    float viewWidth() const override { return logicalWidth; }
    float viewHeight() const override { return logicalHeight; }
    bool setVsync(bool enabled) override { return graphics->setVsync(enabled); }
    bool isVsyncEnabled() const override { return graphics->isVsyncEnabled(); }
    void requestScreenshot(const std::string& path) override { screenshotPath = path; }
    bool lastScreenshotSucceeded() const override { return screenshotSucceeded; }
    double lastGpuWaitMs() const override { return gpuWaitMs; }
    double lastGpuBusyMs() const override { return gpuBusyMs; }
    bool lastFramePresented() const override { return framePresented; }
    double lastPresentWaitMs() const override { return presentWaitMs; }
    std::string deviceDescription() const override { return std::format("NVRHI {}", graphics->description()); }

private:
    bool createPipelines(std::string& error);
    void ensureRenderTarget();
    bool ensureBuffer(nvrhi::BufferHandle& buffer, size_t& capacity, size_t bytes, uint32_t stride, bool writable,
                      bool indirect, const char* name);
    void ensureBindingSets();
    void waitForFrameSlot();
    void runBallVisibility();
    void uploadCellImage(const BrickCellImage& image);
    void dispatch(nvrhi::IComputePipeline* pipeline, nvrhi::IBindingSet* inputs, nvrhi::IBindingSet* outputs,
                  const VisibilityUniforms& uniforms, size_t threads);
    void drawScene(ClearColor clearColor);
    void captureScreenshot();

    SDL_Window* window;
    GraphicsApi api;
    std::unique_ptr<GraphicsDevice> graphics;
    nvrhi::IDevice* device;
    nvrhi::CommandListHandle commandList;
    nvrhi::CommandListHandle ballUploadList;
    bool ballsUploaded = false;
    nvrhi::CommandListHandle cellUploadList;
    bool cellsUploaded = false;
    ImguiNvrhi imgui;
    bool imguiReady = false;

    nvrhi::TextureHandle renderTarget;
    nvrhi::FramebufferHandle framebuffer;
    nvrhi::StagingTextureHandle screenshotStaging;
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    float logicalWidth = 1.0f;
    float logicalHeight = 1.0f;

    nvrhi::BindingLayoutHandle drawStorageLayout;
    nvrhi::BindingLayoutHandle drawConstantsLayout;
    nvrhi::BindingLayoutHandle computeInputLayout;
    nvrhi::BindingLayoutHandle computeOutputLayout;
    nvrhi::BindingLayoutHandle computeConstantsLayout;
    nvrhi::BindingLayoutHandle cellStorageLayout;
    nvrhi::BindingLayoutHandle cellConstantsLayout;
    nvrhi::GraphicsPipelineHandle rectPipeline;
    nvrhi::GraphicsPipelineHandle ballPipeline;
    nvrhi::GraphicsPipelineHandle cellImagePipeline;
    nvrhi::ComputePipelineHandle clearPipeline;
    nvrhi::ComputePipelineHandle bidPipeline;
    nvrhi::ComputePipelineHandle resolvePipeline;
    nvrhi::ComputePipelineHandle compactPipeline;

    nvrhi::BufferHandle rectConstants;
    nvrhi::BufferHandle ballConstants;
    nvrhi::BufferHandle visibilityConstants;
    nvrhi::BufferHandle cellConstants;
    nvrhi::BufferHandle cellBuffer;
    size_t cellCapacity = 0;
    nvrhi::BufferHandle rectBuffer;
    nvrhi::BufferHandle ballBuffer;
    nvrhi::BufferHandle priorityBuffer;
    nvrhi::BufferHandle winnerBuffer;
    nvrhi::BufferHandle visibleBuffer;
    nvrhi::BufferHandle argumentBuffer;
    size_t rectCapacity = 0;
    size_t ballCapacity = 0;
    size_t priorityCapacity = 0;
    size_t winnerCapacity = 0;
    size_t visibleCapacity = 0;
    size_t argumentCapacity = 0;
    bool bindingSetsStale = true;

    nvrhi::BindingSetHandle rectStorageSet;
    nvrhi::BindingSetHandle rectConstantsSet;
    nvrhi::BindingSetHandle ballStorageSet;
    nvrhi::BindingSetHandle ballConstantsSet;
    nvrhi::BindingSetHandle ballsInputSet;
    nvrhi::BindingSetHandle winnersInputSet;
    nvrhi::BindingSetHandle clearOutputSet;
    nvrhi::BindingSetHandle bidOutputSet;
    nvrhi::BindingSetHandle resolveOutputSet;
    nvrhi::BindingSetHandle compactOutputSet;
    nvrhi::BindingSetHandle computeConstantsSet;
    nvrhi::BindingSetHandle cellStorageSet;
    nvrhi::BindingSetHandle cellConstantsSet;

    std::vector<RendererRect> pendingRects;
    BallDrawRequest pendingBalls;
    bool hasBalls = false;
    uint32_t frameSeed = 0;
    CellImageUniforms pendingCells{};
    bool hasCellImage = false;

    uint64_t uploadedLayoutSerial = 0;
    std::vector<uint64_t> uploadedTileRevisions;

    std::array<nvrhi::EventQueryHandle, framesInFlight> frameEvents;
    std::array<nvrhi::TimerQueryHandle, framesInFlight> frameTimers;
    std::array<bool, framesInFlight> frameSubmitted{};
    size_t frameSlot = 0;
    double gpuWaitMs = 0.0;
    double gpuBusyMs = 0.0;
    bool framePresented = true;
    double presentWaitMs = 0.0;
    std::string screenshotPath;
    bool screenshotSucceeded = false;
};

NvrhiRenderer::~NvrhiRenderer() {
    detachFromWindow();
    device->waitForIdle();
}

bool NvrhiRenderer::createPipelines(std::string& error) {
    const auto vertexStorage = nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0);
    drawStorageLayout = createSpaceLayout(device, nvrhi::ShaderType::Vertex, 0, {vertexStorage});
    drawConstantsLayout =
        createSpaceLayout(device, nvrhi::ShaderType::Vertex, 1, {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)});
    computeInputLayout =
        createSpaceLayout(device, nvrhi::ShaderType::Compute, 0, {nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0)});
    computeOutputLayout = createSpaceLayout(device, nvrhi::ShaderType::Compute, 1,
                                            {nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
                                             nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
                                             nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2)});
    computeConstantsLayout = createSpaceLayout(device, nvrhi::ShaderType::Compute, 2,
                                               {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)});
    cellStorageLayout = createSpaceLayout(device, nvrhi::ShaderType::Pixel, 0, {vertexStorage});
    cellConstantsLayout = createSpaceLayout(device, nvrhi::ShaderType::AllGraphics, 1,
                                            {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)});

    auto graphicsPipeline = [&](ShaderProgram vertex, ShaderProgram fragment, bool blended, bool scissored,
                                nvrhi::IBindingLayout* storage, nvrhi::IBindingLayout* constants) {
        nvrhi::GraphicsPipelineDesc description;
        description.primType = nvrhi::PrimitiveType::TriangleList;
        description.VS = createShader(device, nvrhi::ShaderType::Vertex, vertex);
        description.PS = createShader(device, nvrhi::ShaderType::Pixel, fragment);
        description.bindingLayouts = {storage, constants};
        description.renderState.rasterState.setCullNone().setScissorEnable(scissored);
        description.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
        if (blended) {
            description.renderState.blendState.targets[0]
                .setBlendEnable(true)
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
        }
        return description.VS && description.PS
                   ? device->createGraphicsPipeline(description, framebuffer->getFramebufferInfo())
                   : nullptr;
    };
    auto computePipeline = [&](ShaderProgram program) -> nvrhi::ComputePipelineHandle {
        nvrhi::ComputePipelineDesc description;
        description.CS = createShader(device, nvrhi::ShaderType::Compute, program);
        description.bindingLayouts = {computeInputLayout, computeOutputLayout, computeConstantsLayout};
        return description.CS ? device->createComputePipeline(description) : nullptr;
    };
    rectPipeline = graphicsPipeline(ShaderProgram::RectsVertex, ShaderProgram::RectsFragment, true, false,
                                    drawStorageLayout, drawConstantsLayout);
    ballPipeline = graphicsPipeline(ShaderProgram::BallsVertex, ShaderProgram::BallsFragment, false, true,
                                    drawStorageLayout, drawConstantsLayout);
    cellImagePipeline = graphicsPipeline(ShaderProgram::CellImageVertex, ShaderProgram::CellImageFragment, false, true,
                                         cellStorageLayout, cellConstantsLayout);
    clearPipeline = computePipeline(ShaderProgram::VisibilityClear);
    bidPipeline = computePipeline(ShaderProgram::VisibilityBid);
    resolvePipeline = computePipeline(ShaderProgram::VisibilityResolve);
    compactPipeline = computePipeline(ShaderProgram::VisibilityCompact);
    if (!rectPipeline || !ballPipeline || !cellImagePipeline || !clearPipeline || !bidPipeline || !resolvePipeline || !compactPipeline) {
        error = "NVRHI pipeline creation failed";
        return false;
    }
    rectConstants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(RectUniforms), "rect constants", constantVersions));
    ballConstants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(BallUniforms), "ball constants", constantVersions));
    visibilityConstants = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(VisibilityUniforms), "visibility constants", constantVersions));
    cellConstants = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(CellImageUniforms), "cell image constants", constantVersions));
    return true;
}

bool NvrhiRenderer::prepare(std::string& error) {
    commandList = device->createCommandList();
    ballUploadList = device->createCommandList();
    cellUploadList = device->createCommandList();
    for (size_t slot = 0; slot < framesInFlight; ++slot) {
        frameEvents[slot] = device->createEventQuery();
        frameTimers[slot] = device->createTimerQuery();
    }
    ensureRenderTarget();
    if (!createPipelines(error)) {
        return false;
    }
    ensureBuffer(rectBuffer, rectCapacity, initialRectCapacity * sizeof(RendererRect), rectStride, false, false, "rects");
    ensureBuffer(ballBuffer, ballCapacity, initialBallCapacity * storageWord, storageWord, false, false, "balls");
    ensureBuffer(priorityBuffer, priorityCapacity, initialBallCapacity * storageWord, storageWord, true, false, "priorities");
    ensureBuffer(winnerBuffer, winnerCapacity, initialBallCapacity * storageWord, storageWord, true, false, "winners");
    ensureBuffer(visibleBuffer, visibleCapacity, initialBallCapacity * storageWord, storageWord, true, false, "visible balls");
    ensureBuffer(argumentBuffer, argumentCapacity, drawArgumentWords * storageWord, storageWord, true, true, "draw arguments");
    ensureBuffer(cellBuffer, cellCapacity, BrickCellImage::cellsPerTile, storageWord, false, false, "cell image");
    if (!imgui.initialize(device, framebuffer)) {
        error = "ImGui NVRHI pipeline creation failed";
        return false;
    }
    graphics->setVsync(true);
    return true;
}

bool NvrhiRenderer::attachToWindow(std::string& error) {
    if (!graphics->attach(error)) {
        return false;
    }
    if (api == GraphicsApi::Vulkan) {
        ImGui_ImplSDL3_InitForVulkan(window);
    } else {
        ImGui_ImplSDL3_InitForD3D(window);
    }
    imgui.bindToContext();
    imguiReady = true;
    return true;
}

void NvrhiRenderer::detachFromWindow() {
    if (!imguiReady) {
        return;
    }
    device->waitForIdle();
    imgui.releaseContext();
    ImGui_ImplSDL3_Shutdown();
    imguiReady = false;
    graphics->detach();
}

void NvrhiRenderer::ensureRenderTarget() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    logicalWidth = static_cast<float>(std::max(windowWidth, 1));
    logicalHeight = static_cast<float>(std::max(windowHeight, 1));
    const auto pixelWidth = static_cast<uint32_t>(std::max(width, 1));
    const auto pixelHeight = static_cast<uint32_t>(std::max(height, 1));
    if (renderTarget && pixelWidth == targetWidth && pixelHeight == targetHeight) {
        return;
    }
    targetWidth = pixelWidth;
    targetHeight = pixelHeight;
    nvrhi::TextureDesc description;
    description.width = targetWidth;
    description.height = targetHeight;
    description.format = graphics->backBufferFormat();
    description.isRenderTarget = true;
    description.debugName = "scene";
    description.initialState = nvrhi::ResourceStates::RenderTarget;
    description.keepInitialState = true;
    description.setClearValue(nvrhi::Color(0.0f));
    renderTarget = device->createTexture(description);
    framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(renderTarget));
    screenshotStaging = nullptr;
}

bool NvrhiRenderer::ensureBuffer(nvrhi::BufferHandle& buffer, size_t& capacity, size_t bytes, uint32_t stride,
                                 bool writable, bool indirect, const char* name) {
    if (buffer && bytes <= capacity) {
        return false;
    }
    capacity = std::max(bytes, capacity * 2);
    nvrhi::BufferDesc description;
    description.byteSize = capacity;
    description.structStride = stride;
    description.canHaveUAVs = writable;
    description.isDrawIndirectArgs = indirect;
    description.debugName = name;
    description.initialState = writable ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource;
    description.keepInitialState = true;
    buffer = device->createBuffer(description);
    bindingSetsStale = true;
    return true;
}

void NvrhiRenderer::ensureBindingSets() {
    if (!bindingSetsStale) {
        return;
    }
    bindingSetsStale = false;
    auto storage = [&](nvrhi::IBuffer* buffer, nvrhi::IBindingLayout* layout) {
        return device->createBindingSet(nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffer)),
                                        layout);
    };
    auto outputs = [&](nvrhi::IBuffer* first, nvrhi::IBuffer* second, nvrhi::IBuffer* third) {
        return device->createBindingSet(nvrhi::BindingSetDesc()
                                            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, first))
                                            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(1, second))
                                            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(2, third)),
                                        computeOutputLayout);
    };
    auto constants = [&](nvrhi::IBuffer* buffer, nvrhi::IBindingLayout* layout) {
        return device->createBindingSet(nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, buffer)),
                                        layout);
    };
    rectStorageSet = storage(rectBuffer, drawStorageLayout);
    ballStorageSet = storage(visibleBuffer, drawStorageLayout);
    rectConstantsSet = constants(rectConstants, drawConstantsLayout);
    ballConstantsSet = constants(ballConstants, drawConstantsLayout);
    ballsInputSet = storage(ballBuffer, computeInputLayout);
    winnersInputSet = storage(winnerBuffer, computeInputLayout);
    clearOutputSet = outputs(priorityBuffer, winnerBuffer, argumentBuffer);
    bidOutputSet = outputs(priorityBuffer, winnerBuffer, argumentBuffer);
    resolveOutputSet = outputs(priorityBuffer, winnerBuffer, argumentBuffer);
    compactOutputSet = outputs(visibleBuffer, argumentBuffer, priorityBuffer);
    computeConstantsSet = constants(visibilityConstants, computeConstantsLayout);
    cellStorageSet = storage(cellBuffer, cellStorageLayout);
    cellConstantsSet = constants(cellConstants, cellConstantsLayout);
}

void NvrhiRenderer::waitForFrameSlot() {
    gpuWaitMs = 0.0;
    if (!frameSubmitted[frameSlot]) {
        return;
    }
    const Clock::time_point start = Clock::now();
    device->waitEventQuery(frameEvents[frameSlot]);
    gpuWaitMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (device->pollTimerQuery(frameTimers[frameSlot])) {
        gpuBusyMs = device->getTimerQueryTime(frameTimers[frameSlot]) * 1000.0;
    }
    device->resetEventQuery(frameEvents[frameSlot]);
    device->resetTimerQuery(frameTimers[frameSlot]);
    frameSubmitted[frameSlot] = false;
}

bool NvrhiRenderer::beginFrame() {
    waitForFrameSlot();
    ensureRenderTarget();
    pendingRects.clear();
    hasBalls = false;
    hasCellImage = false;
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

void NvrhiRenderer::drawRects(std::span<const RendererRect> rects) {
    pendingRects.insert(pendingRects.end(), rects.begin(), rects.end());
}

void NvrhiRenderer::drawBalls(const BallDrawRequest& request) {
    pendingBalls = request;
    hasBalls = request.count > 0 && request.packed;
    if (!hasBalls) {
        return;
    }
    ensureBuffer(ballBuffer, ballCapacity, request.count * storageWord, storageWord, false, false, "balls");
    ballUploadList->open();
    ballUploadList->writeBuffer(ballBuffer, request.packed, request.count * storageWord);
    ballUploadList->close();
    pendingBalls.packed = nullptr;
    ballsUploaded = true;
}

bool NvrhiRenderer::drawCellImage(const CellImageDrawRequest& request) {
    if (!request.image || request.image->tileBytes().empty()) {
        return true;
    }
    const BrickCellImage& image = *request.image;
    uploadCellImage(image);
    CellImageUniforms& uniforms = pendingCells;
    const bool clipped = request.clipWidth > 0.0f && request.clipHeight > 0.0f;
    const float boardRight = request.originX + static_cast<float>(image.columns()) * request.cellPixels;
    const float boardBottom = request.originY + static_cast<float>(image.rows()) * request.cellPixels;
    const float left = clipped ? std::max(request.clipX, request.originX) : request.originX;
    const float top = clipped ? std::max(request.clipY, request.originY) : request.originY;
    const float right = clipped ? std::min(request.clipX + request.clipWidth, boardRight) : boardRight;
    const float bottom = clipped ? std::min(request.clipY + request.clipHeight, boardBottom) : boardBottom;
    hasCellImage = right > left && bottom > top;
    uniforms.drawRect[0] = left;
    uniforms.drawRect[1] = top;
    uniforms.drawRect[2] = right - left;
    uniforms.drawRect[3] = bottom - top;
    uniforms.boardOrigin[0] = request.originX;
    uniforms.boardOrigin[1] = request.originY;
    uniforms.cellPixels = request.cellPixels;
    uniforms.columns = static_cast<uint32_t>(image.columns());
    uniforms.rows = static_cast<uint32_t>(image.rows());
    uniforms.tilesAcross = static_cast<uint32_t>(image.tilesAcross());
    for (size_t color = 0; color < request.palette.size(); ++color) {
        std::copy(request.palette[color].begin(), request.palette[color].end(), uniforms.palette[color]);
    }
    return true;
}

void NvrhiRenderer::uploadCellImage(const BrickCellImage& image) {
    const std::vector<uint8_t>& bytes = image.tileBytes();
    const std::vector<uint64_t>& revisions = image.tileRevisions();
    const bool sameBoard =
        image.layoutSerial() == uploadedLayoutSerial && revisions.size() == uploadedTileRevisions.size();
    cellUploadList->open();
    if (!sameBoard) {
        ensureBuffer(cellBuffer, cellCapacity, bytes.size(), storageWord, false, false, "cell image");
        cellUploadList->writeBuffer(cellBuffer, bytes.data(), bytes.size());
        uploadedLayoutSerial = image.layoutSerial();
        uploadedTileRevisions = revisions;
    } else {

        size_t tile = 0;
        while (tile < revisions.size()) {
            if (revisions[tile] == uploadedTileRevisions[tile]) {
                ++tile;
                continue;
            }
            const size_t first = tile;
            while (tile < revisions.size() && revisions[tile] != uploadedTileRevisions[tile]) {
                uploadedTileRevisions[tile] = revisions[tile];
                ++tile;
            }
            const size_t offset = first * BrickCellImage::cellsPerTile;
            cellUploadList->writeBuffer(cellBuffer, bytes.data() + offset,
                                        (tile - first) * BrickCellImage::cellsPerTile, offset);
        }
    }
    cellUploadList->close();
    cellsUploaded = true;
}

void NvrhiRenderer::dispatch(nvrhi::IComputePipeline* pipeline, nvrhi::IBindingSet* inputs, nvrhi::IBindingSet* outputs,
                             const VisibilityUniforms& uniforms, size_t threads) {
    commandList->writeBuffer(visibilityConstants, &uniforms, sizeof(uniforms));
    nvrhi::ComputeState state;
    state.pipeline = pipeline;
    state.bindings = {inputs, outputs, computeConstantsSet};
    commandList->setComputeState(state);
    commandList->dispatch(groupsFor(threads));
}

void NvrhiRenderer::runBallVisibility() {
    const BallDrawRequest& request = pendingBalls;
    const float slotSize =
        visibilitySlotSize(request.radiusCells, request.cellPixels);
    const uint32_t gridWidth = std::max<uint32_t>(static_cast<uint32_t>(std::ceil(request.fieldWidth / slotSize)), 1);
    const uint32_t gridHeight = std::max<uint32_t>(static_cast<uint32_t>(std::ceil(request.fieldHeight / slotSize)), 1);
    const size_t gridCells = static_cast<size_t>(gridWidth) * gridHeight;
    const bool bidForSlots = !request.isVisibilityGrid && request.count > gridCells;
    ensureBuffer(priorityBuffer, priorityCapacity, gridCells * storageWord, storageWord, true, false, "priorities");
    ensureBuffer(winnerBuffer, winnerCapacity, gridCells * storageWord, storageWord, true, false, "winners");
    ensureBuffer(visibleBuffer, visibleCapacity, (bidForSlots ? gridCells : request.count) * storageWord, storageWord, true, false,
                 "visible balls");
    ensureBindingSets();

    VisibilityUniforms uniforms{};
    uniforms.gridWidth = gridWidth;
    uniforms.gridHeight = gridHeight;
    uniforms.frameSeed = ++frameSeed;
    uniforms.coordinateToSlotX = static_cast<float>(gridWidth) / static_cast<float>(renderCoordinateRange);
    uniforms.coordinateToSlotY = static_cast<float>(gridHeight) / static_cast<float>(renderCoordinateRange);
    uniforms.bidderMask = bidderMaskFor(request.count, gridCells);

    uniforms.itemCount = static_cast<uint32_t>(bidForSlots ? gridCells : 0);
    dispatch(clearPipeline, ballsInputSet, clearOutputSet, uniforms, std::max<size_t>(uniforms.itemCount, 1));
    uniforms.itemCount = static_cast<uint32_t>(request.count);
    if (bidForSlots) {
        dispatch(bidPipeline, ballsInputSet, bidOutputSet, uniforms, request.count);
        dispatch(resolvePipeline, ballsInputSet, resolveOutputSet, uniforms, request.count);
    }
    uniforms.itemCount = static_cast<uint32_t>(bidForSlots ? gridCells : request.count);
    dispatch(compactPipeline, bidForSlots ? winnersInputSet : ballsInputSet, compactOutputSet, uniforms,
             uniforms.itemCount);
}

void NvrhiRenderer::drawScene(ClearColor clearColor) {
    commandList->clearTextureFloat(renderTarget, nvrhi::AllSubresources,
                                   nvrhi::Color(clearColor.red, clearColor.green, clearColor.blue, 1.0f));
    const nvrhi::Viewport viewport(static_cast<float>(targetWidth), static_cast<float>(targetHeight));
    if (!pendingRects.empty()) {
        const RectUniforms uniforms{{logicalWidth, logicalHeight}, {0.0f, 0.0f}};
        commandList->writeBuffer(rectConstants, &uniforms, sizeof(uniforms));
        nvrhi::GraphicsState state;
        state.pipeline = rectPipeline;
        state.framebuffer = framebuffer;

        state.viewport = nvrhi::ViewportState().addViewport(viewport).addScissorRect(nvrhi::Rect(viewport));
        state.bindings = {rectStorageSet, rectConstantsSet};
        commandList->setGraphicsState(state);
        commandList->draw(nvrhi::DrawArguments().setVertexCount(verticesPerQuad).setInstanceCount(
            static_cast<uint32_t>(pendingRects.size())));
    }
    if (hasCellImage) {
        CellImageUniforms& uniforms = pendingCells;
        uniforms.viewportSize[0] = logicalWidth;
        uniforms.viewportSize[1] = logicalHeight;
        commandList->writeBuffer(cellConstants, &uniforms, sizeof(uniforms));
        nvrhi::GraphicsState state;
        state.pipeline = cellImagePipeline;
        state.framebuffer = framebuffer;
        state.viewport = nvrhi::ViewportState().addViewport(viewport).addScissorRect(nvrhi::Rect(viewport));
        state.bindings = {cellStorageSet, cellConstantsSet};
        commandList->setGraphicsState(state);
        commandList->draw(nvrhi::DrawArguments().setVertexCount(verticesPerQuad));
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
        uniforms.radiusPixels = std::max(request.radiusCells * request.cellPixels,
                                         minimumBallRadiusPixels * logicalWidth / static_cast<float>(targetWidth));
        for (int color = 0; color < rendererPaletteSize; ++color) {
            std::copy(request.palette[color].begin(), request.palette[color].end(), uniforms.palette[color]);
        }
        commandList->writeBuffer(ballConstants, &uniforms, sizeof(uniforms));
        const float scale = static_cast<float>(targetWidth) / logicalWidth;
        const bool clipped = request.clipWidth > 0.0f && request.clipHeight > 0.0f;
        const nvrhi::Rect scissor =
            clipped ? nvrhi::Rect(static_cast<int>(request.clipX * scale),
                                  static_cast<int>(std::ceil((request.clipX + request.clipWidth) * scale)),
                                  static_cast<int>(request.clipY * scale),
                                  static_cast<int>(std::ceil((request.clipY + request.clipHeight) * scale)))
                    : nvrhi::Rect(viewport);
        nvrhi::GraphicsState state;
        state.pipeline = ballPipeline;
        state.framebuffer = framebuffer;
        state.viewport = nvrhi::ViewportState().addViewport(viewport).addScissorRect(scissor);
        state.bindings = {ballStorageSet, ballConstantsSet};
        state.indirectParams = argumentBuffer;
        commandList->setGraphicsState(state);
        commandList->drawIndirect(0);
    }
}

void NvrhiRenderer::captureScreenshot() {
    if (!screenshotStaging) {
        screenshotStaging =
            device->createStagingTexture(renderTarget->getDesc(), nvrhi::CpuAccessMode::Read);
    }
    commandList->copyTexture(screenshotStaging, nvrhi::TextureSlice(), renderTarget, nvrhi::TextureSlice());
}

void NvrhiRenderer::endFrame(ClearColor clearColor) {
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    const bool windowVisible = (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) == 0;
    const bool canBegin = windowVisible && graphics->beginFrame();
    const Clock::time_point slotWaitStart = Clock::now();
    const bool presentable = canBegin && graphics->readyToPresent();
    presentWaitMs = std::chrono::duration<double, std::milli>(Clock::now() - slotWaitStart).count();
    framePresented = presentable;
    if (!pendingRects.empty()) {
        ensureBuffer(rectBuffer, rectCapacity, pendingRects.size() * sizeof(RendererRect), rectStride, false, false, "rects");
    }
    ensureBindingSets();
    commandList->open();
    commandList->beginTimerQuery(frameTimers[frameSlot]);
    if (drawData) {
        imgui.updateTextures(commandList, drawData);
    }
    if (!pendingRects.empty()) {
        commandList->writeBuffer(rectBuffer, pendingRects.data(), pendingRects.size() * sizeof(RendererRect));
    }
    if (hasBalls) {
        runBallVisibility();
    }
    drawScene(clearColor);
    if (drawData) {
        imgui.render(commandList, drawData, framebuffer);
    }
    const bool capturing = !screenshotPath.empty();
    if (capturing) {
        captureScreenshot();
    }
    if (presentable) {
        nvrhi::ITexture* backBuffer = graphics->backBuffer();
        if (backBuffer->getDesc().width == targetWidth && backBuffer->getDesc().height == targetHeight) {
            commandList->copyTexture(backBuffer, nvrhi::TextureSlice(), renderTarget, nvrhi::TextureSlice());
        }
    }
    commandList->endTimerQuery(frameTimers[frameSlot]);
    commandList->close();
    if (ballsUploaded) {
        device->executeCommandList(ballUploadList);
        ballsUploaded = false;
    }
    if (cellsUploaded) {
        device->executeCommandList(cellUploadList);
        cellsUploaded = false;
    }
    device->executeCommandList(commandList);
    device->setEventQuery(frameEvents[frameSlot], nvrhi::CommandQueue::Graphics);
    frameSubmitted[frameSlot] = true;
    if (presentable) {
        const Clock::time_point presentStart = Clock::now();
        graphics->present();
        presentWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - presentStart).count();
    }
    if (capturing) {
        device->waitEventQuery(frameEvents[frameSlot]);
        size_t rowPitch = 0;
        void* pixels = device->mapStagingTexture(screenshotStaging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read,
                                                 &rowPitch);
        screenshotSucceeded = false;
        if (pixels) {
            const SDL_PixelFormat format = graphics->backBufferFormat() == nvrhi::Format::BGRA8_UNORM
                                               ? SDL_PIXELFORMAT_ARGB8888
                                               : SDL_PIXELFORMAT_ABGR8888;
            SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(targetWidth), static_cast<int>(targetHeight),
                                                         format, pixels, static_cast<int>(rowPitch));
            if (surface) {
                screenshotSucceeded = SDL_SaveBMP(surface, screenshotPath.c_str());
                SDL_DestroySurface(surface);
            }
            device->unmapStagingTexture(screenshotStaging);
        }
        screenshotPath.clear();
    }
    device->runGarbageCollection();
    frameSlot = (frameSlot + 1) % framesInFlight;
}

}

std::unique_ptr<IRenderer> createNvrhiRenderer(SDL_Window* window, GraphicsApi api, std::string_view gpuPreference,
                                               std::string& error) {
    auto graphics = createGraphicsDevice(api, window, gpuPreference, error);
    if (!graphics) {
        return nullptr;
    }
    auto renderer = std::make_unique<NvrhiRenderer>(window, std::move(graphics), api);
    if (!renderer->prepare(error)) {
        return nullptr;
    }
    return renderer;
}
