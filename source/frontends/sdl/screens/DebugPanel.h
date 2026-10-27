#pragma once

#include "app/FrameTimings.h"
#include "game/GameSession.h"
#include "sim/Simulation.h"

#include "app/Input.h"
#include "ui/Painter.h"

struct DebugPanelState {
    float scroll = 0.0f;
};

void drawDebugPanel(Rect area, const GameSession& session, const FrameTimings& frames,
                    DebugPanelState& state);
void drawPortraitDebugStrip(Rect area, const GameSession& session, const FrameTimings& frames);
float debugContentHeight(const Simulation& simulation);
