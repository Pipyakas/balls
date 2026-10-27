#include "screens/SettingsScreen.h"

#include "app/App.h"
#include "app/Input.h"
#include "render/Palette.h"
#include "screens/SettingsPanel.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"

#include <algorithm>

void SettingsScreen::update(float) {
    if (input::keyPressed(SDL_SCANCODE_ESCAPE)) {
        app.showMainMenu();
    }
}

void SettingsScreen::draw() {
    const float width = static_cast<float>(gfx::screenWidth());
    const float height = static_cast<float>(gfx::screenHeight());
    const float panelWidth = std::min(settingsPanelWidth, width - 32.0f);
    const float panelHeight = std::min(settingsPanelHeight, height - 32.0f);
    const Rect panel{(width - panelWidth) * 0.5f, (height - panelHeight) * 0.5f, panelWidth, panelHeight};
    ui::drawPanel(panel, palette::panel, 0.08f);
    const SettingsPanelEdit edit = drawSettingsPanel(panel, app.settings(), app.preferredBackend());
    if (edit.changed) {
        app.applySettings(edit.settings, edit.backend);
    }
    if (edit.backPressed) {
        app.showMainMenu();
    }
}
