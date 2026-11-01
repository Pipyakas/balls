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

void drawDebugPanel(Rect area, const SessionView& session, const FrameTimings& frames, DebugPanelState& state,
                    bool showBackend, std::string_view deviceDescription = {}, std::string_view renderer = {});
void drawPortraitDebugStrip(Rect area, const SessionView& session, const FrameTimings& frames, bool showBackend);
