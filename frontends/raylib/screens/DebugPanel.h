#pragma once

#include "app/FrameTimings.h"
#include "game/GameSession.h"

#include <raylib.h>

void drawDebugPanel(Rectangle area, const GameSession& session, const FrameTimings& frames);
