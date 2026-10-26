#pragma once

#include <nvrhi/nvrhi.h>

struct ImDrawData;
struct ImTextureData;

class ImguiNvrhi {
public:
    ~ImguiNvrhi();

    bool initialize(nvrhi::IDevice* device, nvrhi::IFramebuffer* framebuffer);
    void updateTextures(nvrhi::ICommandList* commandList, ImDrawData* drawData);
    void render(nvrhi::ICommandList* commandList, ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer);

private:
    void createTexture(nvrhi::ICommandList* commandList, ImTextureData* texture);
    void destroyTexture(ImTextureData* texture);
    bool ensureGeometryCapacity(size_t vertices, size_t indices);

    nvrhi::IDevice* device = nullptr;
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle bindingLayout;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::SamplerHandle sampler;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle vertexBuffer;
    nvrhi::BufferHandle indexBuffer;
    size_t vertexCapacity = 0;
    size_t indexCapacity = 0;
};
