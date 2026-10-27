#pragma once

#include "game/Settings.h"

#include "ui/Painter.h"

namespace {

constexpr float settingsPanelWidth = 560.0f;
constexpr float settingsPanelHeight = 500.0f;

} // namespace

struct SettingsPanelEdit {
    Settings settings;
    bool backPressed = false;
    bool changed = false;
};

SettingsPanelEdit drawSettingsPanel(Rect area, Settings current);
