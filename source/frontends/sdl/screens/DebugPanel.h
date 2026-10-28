#pragma once

#include "game/FrameTimings.h"
#include "game/SessionView.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <string_view>

struct DebugPanelState {
    float scroll = 0.0f;
    float contentHeight = 0.0f;
};

// showBackend follows the control gadget's Advanced Settings section: closed, the backend and device rows are hidden.
// deviceDescription: what the renderer runs on, shown instead of the physics device when set.
void drawDebugPanel(Rect area, const SessionView& session, const FrameTimings& frames, DebugPanelState& state,
                    bool showBackend, std::string_view deviceDescription = {});
void drawPortraitDebugStrip(Rect area, const SessionView& session, const FrameTimings& frames, bool showBackend);
