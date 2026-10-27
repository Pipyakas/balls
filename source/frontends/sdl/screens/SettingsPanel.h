#pragma once

#include "game/Settings.h"
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
};

SettingsPanelEdit drawSettingsPanel(Rect area, Settings current, BackendKind selectedBackend);
