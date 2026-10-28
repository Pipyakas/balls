#pragma once

#include "game/Settings.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"

#include "ui/Painter.h"

namespace {

constexpr float settingsPanelWidth = 560.0f;
constexpr float settingsPanelHeight = 940.0f;

} // namespace

struct SettingsPanelEdit {
    Settings settings;
    bool backPressed = false;
    bool changed = false;
    std::optional<BackendKind> backend;
    bool runBenchmark = false;
};

// benchmarkScores: the machine benchmark's last results, shown muted next to each backend.
// webGpuSupported: lets the Renderer row offer WebGPU only in a build that links it.
SettingsPanelEdit drawSettingsPanel(Rect area, Settings current, BackendKind selectedBackend,
                                    std::span<const CandidateResult> benchmarkScores, bool webGpuSupported,
                                    float& scroll);
