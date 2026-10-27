#include "render/nvrhi/ShaderBytecode.h"

#include "shaders/generated/ShaderBlobs.h"

#include <span>

namespace {

struct ShaderBlobs {
    std::span<const unsigned char> spirv;
    std::span<const unsigned char> dxil;
    const char* entryName;
};

ShaderBlobs blobsFor(ShaderProgram program) {
    switch (program) {
    case ShaderProgram::BallsVertex:
        return {ballsVertexSpirv, ballsVertexDxil, "vertexMain"};
    case ShaderProgram::BallsFragment:
        return {ballsFragmentSpirv, ballsFragmentDxil, "fragmentMain"};
    case ShaderProgram::RectsVertex:
        return {rectsVertexSpirv, rectsVertexDxil, "vertexMain"};
    case ShaderProgram::RectsFragment:
        return {rectsFragmentSpirv, rectsFragmentDxil, "fragmentMain"};
    case ShaderProgram::VisibilityClear:
        return {visibilityClearSpirv, visibilityClearDxil, "computeMain"};
    case ShaderProgram::VisibilityBid:
        return {visibilityBidSpirv, visibilityBidDxil, "computeMain"};
    case ShaderProgram::VisibilityResolve:
        return {visibilityResolveSpirv, visibilityResolveDxil, "computeMain"};
    case ShaderProgram::VisibilityCompact:
        return {visibilityCompactSpirv, visibilityCompactDxil, "computeMain"};
    case ShaderProgram::ImguiVertex:
        return {imguiVertexSpirv, imguiVertexDxil, "vertexMain"};
    case ShaderProgram::ImguiFragment:
        return {imguiFragmentSpirv, imguiFragmentDxil, "fragmentMain"};
    }
    return {};
}

}

nvrhi::ShaderHandle createShader(nvrhi::IDevice* device, nvrhi::ShaderType type, ShaderProgram program) {
    const ShaderBlobs blobs = blobsFor(program);
    const std::span<const unsigned char> code =
        device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN ? blobs.spirv : blobs.dxil;
    return device->createShader(nvrhi::ShaderDesc().setShaderType(type).setEntryName(blobs.entryName), code.data(),
                                code.size());
}
