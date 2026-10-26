#include "render/nvrhi/ImguiNvrhi.h"

#include "render/nvrhi/ShaderBytecode.h"

#include <imgui.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <vector>

namespace {

constexpr size_t geometryGrowth = 5000;
constexpr uint32_t constantVersions = 16;

struct ImguiConstants {
    float scale[2];
    float translate[2];
};

struct TextureBinding {
    nvrhi::TextureHandle texture;
    nvrhi::BindingSetHandle bindingSet;
};

}

ImguiNvrhi::~ImguiNvrhi() {
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        destroyTexture(texture);
    }
}

bool ImguiNvrhi::initialize(nvrhi::IDevice* targetDevice, nvrhi::IFramebuffer* framebuffer) {
    device = targetDevice;
    vertexShader = createShader(device, nvrhi::ShaderType::Vertex, ShaderProgram::ImguiVertex);
    pixelShader = createShader(device, nvrhi::ShaderType::Pixel, ShaderProgram::ImguiFragment);
    if (!vertexShader || !pixelShader) {
        return false;
    }
    const nvrhi::VertexAttributeDesc attributes[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT)
            .setOffset(offsetof(ImDrawVert, pos)).setElementStride(sizeof(ImDrawVert)),
        nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT)
            .setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert)),
        nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM)
            .setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert)),
    };
    inputLayout = device->createInputLayout(attributes, static_cast<uint32_t>(std::size(attributes)), vertexShader);

    bindingLayout = device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::All)
            .setRegisterSpaceAndDescriptorSet(0)
            .setBindingOffsets(nvrhi::VulkanBindingOffsets()
                                   .setConstantBufferOffset(0)
                                   .setShaderResourceOffset(1)
                                   .setSamplerOffset(2)
                                   .setUnorderedAccessViewOffset(3))
            .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
            .addItem(nvrhi::BindingLayoutItem::Sampler(0)));
    constants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ImguiConstants), "imgui constants", constantVersions));
    sampler = device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
                                        .setAllFilters(true));

    nvrhi::BlendState blend;
    blend.targets[0]
        .setBlendEnable(true)
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.VS = vertexShader;
    pipelineDesc.PS = pixelShader;
    pipelineDesc.bindingLayouts = {bindingLayout};
    pipelineDesc.renderState.blendState = blend;
    pipelineDesc.renderState.rasterState.setCullNone().setScissorEnable(true);
    pipelineDesc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
    pipeline = device->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    ImGui::GetIO().BackendRendererName = "balls_nvrhi";
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    return pipeline != nullptr;
}

void ImguiNvrhi::createTexture(nvrhi::ICommandList* commandList, ImTextureData* texture) {
    auto* binding = static_cast<TextureBinding*>(texture->BackendUserData);
    if (!binding) {
        binding = new TextureBinding();
        nvrhi::TextureDesc description;
        description.width = static_cast<uint32_t>(texture->Width);
        description.height = static_cast<uint32_t>(texture->Height);
        description.format = nvrhi::Format::RGBA8_UNORM;
        description.debugName = "imgui texture";
        description.initialState = nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        binding->texture = device->createTexture(description);
        binding->bindingSet = device->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, constants))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(0, binding->texture))
                .addItem(nvrhi::BindingSetItem::Sampler(0, sampler)),
            bindingLayout);
        texture->BackendUserData = binding;
        texture->SetTexID(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(binding)));
    }
    commandList->writeTexture(binding->texture, 0, 0, texture->GetPixels(),
                              static_cast<size_t>(texture->Width) * texture->BytesPerPixel);
    texture->SetStatus(ImTextureStatus_OK);
}

void ImguiNvrhi::destroyTexture(ImTextureData* texture) {
    delete static_cast<TextureBinding*>(texture->BackendUserData);
    texture->BackendUserData = nullptr;
    texture->SetTexID(ImTextureID_Invalid);
    texture->SetStatus(ImTextureStatus_Destroyed);
}

void ImguiNvrhi::updateTextures(nvrhi::ICommandList* commandList, ImDrawData* drawData) {
    if (!drawData->Textures) {
        return;
    }
    for (ImTextureData* texture : *drawData->Textures) {
        if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
            createTexture(commandList, texture);
        } else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0) {
            destroyTexture(texture);
        }
    }
}

bool ImguiNvrhi::ensureGeometryCapacity(size_t vertices, size_t indices) {
    if (!vertexBuffer || vertexCapacity < vertices) {
        vertexCapacity = vertices + geometryGrowth;
        vertexBuffer = device->createBuffer(nvrhi::BufferDesc()
                                                .setByteSize(vertexCapacity * sizeof(ImDrawVert))
                                                .setIsVertexBuffer(true)
                                                .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                                .setKeepInitialState(true)
                                                .setDebugName("imgui vertices"));
    }
    if (!indexBuffer || indexCapacity < indices) {
        indexCapacity = indices + geometryGrowth;
        indexBuffer = device->createBuffer(nvrhi::BufferDesc()
                                               .setByteSize(indexCapacity * sizeof(ImDrawIdx))
                                               .setIsIndexBuffer(true)
                                               .setInitialState(nvrhi::ResourceStates::IndexBuffer)
                                               .setKeepInitialState(true)
                                               .setDebugName("imgui indices"));
    }
    return vertexBuffer && indexBuffer;
}

void ImguiNvrhi::render(nvrhi::ICommandList* commandList, ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer) {
    const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    if (framebufferWidth <= 0.0f || framebufferHeight <= 0.0f || drawData->TotalVtxCount <= 0 ||
        !ensureGeometryCapacity(static_cast<size_t>(drawData->TotalVtxCount),
                                static_cast<size_t>(drawData->TotalIdxCount))) {
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
    commandList->writeBuffer(vertexBuffer, vertices.data(), vertices.size() * sizeof(ImDrawVert));
    commandList->writeBuffer(indexBuffer, indices.data(), indices.size() * sizeof(ImDrawIdx));
    const ImguiConstants values{{2.0f / drawData->DisplaySize.x, -2.0f / drawData->DisplaySize.y},
                                {-1.0f - drawData->DisplayPos.x * 2.0f / drawData->DisplaySize.x,
                                 1.0f + drawData->DisplayPos.y * 2.0f / drawData->DisplaySize.y}};
    commandList->writeBuffer(constants, &values, sizeof(values));

    nvrhi::GraphicsState state;
    state.pipeline = pipeline;
    state.framebuffer = framebuffer;
    state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(vertexBuffer).setSlot(0).setOffset(0)};
    state.indexBuffer = nvrhi::IndexBufferBinding()
                            .setBuffer(indexBuffer)
                            .setFormat(sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT)
                            .setOffset(0);
    const nvrhi::Viewport viewport(framebufferWidth, framebufferHeight);
    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    uint32_t vertexBase = 0;
    uint32_t indexBase = 0;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback != ImDrawCallback_ResetRenderState) {
                    command.UserCallback(list, &command);
                }
                continue;
            }
            const float left = std::max((command.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f);
            const float top = std::max((command.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f);
            const float right = std::min((command.ClipRect.z - clipOffset.x) * clipScale.x, framebufferWidth);
            const float bottom = std::min((command.ClipRect.w - clipOffset.y) * clipScale.y, framebufferHeight);
            if (right <= left || bottom <= top) {
                continue;
            }
            const auto* binding = reinterpret_cast<const TextureBinding*>(static_cast<intptr_t>(command.GetTexID()));
            if (!binding) {
                continue;
            }
            state.bindings = {binding->bindingSet};
            state.viewport = nvrhi::ViewportState().addViewport(viewport).addScissorRect(
                nvrhi::Rect(static_cast<int>(left), static_cast<int>(right), static_cast<int>(top),
                            static_cast<int>(bottom)));
            commandList->setGraphicsState(state);
            commandList->drawIndexed(nvrhi::DrawArguments()
                                         .setVertexCount(command.ElemCount)
                                         .setStartIndexLocation(indexBase + command.IdxOffset)
                                         .setStartVertexLocation(vertexBase + command.VtxOffset));
        }
        vertexBase += static_cast<uint32_t>(list->VtxBuffer.Size);
        indexBase += static_cast<uint32_t>(list->IdxBuffer.Size);
    }
}
