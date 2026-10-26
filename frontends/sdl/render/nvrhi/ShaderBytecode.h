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
    ImguiVertex,
    ImguiFragment,
};

nvrhi::ShaderHandle createShader(nvrhi::IDevice* device, nvrhi::ShaderType type, ShaderProgram program);
