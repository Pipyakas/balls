#pragma once

// The core is plain C++20 built outside UBT; keep engine warning rules away from its headers.
THIRD_PARTY_INCLUDES_START
#include "common/BallColor.h"
#include "common/BallCount.h"
#include "control/ControlApi.h"
#include "editor/LevelEditor.h"
#include "game/FrameTimings.h"
#include "game/GameSession.h"
#include "game/MachineProfile.h"
#include "game/SessionHost.h"
#include "game/Settings.h"
#include "level/LevelImporter.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWatcher.h"
#include "level/LevelWriter.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"
#include "sim/BackendSelection.h"
#include "sim/BackendWorker.h"
#include "sim/DeviceInventory.h"
#include "sim/Simulation.h"
#include "ui/GameplayLayout.h"
THIRD_PARTY_INCLUDES_END
