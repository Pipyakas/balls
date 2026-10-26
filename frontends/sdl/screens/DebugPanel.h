#pragma once

#include "app/FrameTimings.h"
#include "game/GameSession.h"

#include "app/Input.h"
#include "ui/Painter.h"

void drawDebugPanel(Rect area, const GameSession& session, const FrameTimings& frames);
