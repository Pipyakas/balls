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

// showBackend follows the control gadget's Advanced Settings section: closed, the backend and device rows are hidden.
// deviceDescription: what the renderer runs on, shown instead of the physics device when set.
void drawDebugPanel(Rect area, const GameSession& session, const FrameTimings& frames, DebugPanelState& state,
                    bool showBackend, std::string_view deviceDescription = {});
void drawPortraitDebugStrip(Rect area, const GameSession& session, const FrameTimings& frames, bool showBackend);
float debugContentHeight(const Simulation& simulation, bool showBackend);
