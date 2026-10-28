#pragma once

#include "app/FrameTimings.h"
#include "game/GameSession.h"
#include "sim/Simulation.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <string_view>

struct DebugPanelState {
    float scroll = 0.0f;
};

// deviceDescription: what the renderer runs on, shown instead of the physics device when set.
void drawDebugPanel(Rect area, const GameSession& session, const FrameTimings& frames,
                    DebugPanelState& state, std::string_view deviceDescription = {});
void drawPortraitDebugStrip(Rect area, const GameSession& session, const FrameTimings& frames);
float debugContentHeight(const Simulation& simulation);
