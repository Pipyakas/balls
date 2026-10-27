#pragma once

#include "app/FrameTimings.h"
#include "game/GameSession.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <string_view>

void drawDebugPanel(Rect area, const GameSession& session, const FrameTimings& frames,
                    std::string_view deviceDescription = {});
void drawPortraitDebugStrip(Rect area, const GameSession& session, const FrameTimings& frames);

