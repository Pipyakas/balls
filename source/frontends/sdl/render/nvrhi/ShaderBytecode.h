#pragma once

#include <nvrhi/nvrhi.h>

enum class ShaderProgram {
    BallsVertex,
    BallsFragment,
    RectsVertex,
    RectsFragment,
    VisibilityClear,
    VisibilityBid,
    VisibilityResolve,
    VisibilityCompact,
    CellImageVertex,
    CellImageFragment,
    ImguiVertex,
    ImguiFragment,
};

nvrhi::ShaderHandle createShader(nvrhi::IDevice* device, nvrhi::ShaderType type, ShaderProgram program);
