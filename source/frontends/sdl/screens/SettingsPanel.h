#pragma once

#include "game/Settings.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"

#include "ui/Painter.h"

namespace {

constexpr float settingsPanelWidth = 560.0f;
constexpr float settingsPanelHeight = 940.0f;

}

struct SettingsPanelEdit {
    Settings settings;
    bool backPressed = false;
    bool changed = false;
    std::optional<BackendKind> backend;
    bool runBenchmark = false;
};

SettingsPanelEdit drawSettingsPanel(Rect area, Settings current, BackendKind selectedBackend,
                                    std::span<const CandidateResult> benchmarkScores, bool webGpuSupported,
                                    float& scroll, bool canRunBenchmark = true);
