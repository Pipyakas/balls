#include "screens/LevelSelectScreen.h"

#include "app/App.h"
#include "app/Paths.h"
#include "level/LevelRepository.h"
#include "render/Palette.h"
#include "ui/Widgets.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <algorithm>

namespace {

constexpr float listWidth = 560.0f;
constexpr float rowHeight = 58.0f;
constexpr float rowGap = 12.0f;
constexpr float editButtonWidth = 110.0f;
constexpr float scrollSpeed = 40.0f;

}

LevelSelectScreen::LevelSelectScreen(App& app, GameMode mode)
    : app(app), mode(mode), levels(findLevels(levelDirectory(mode))) {}

void LevelSelectScreen::update(float) {
    scroll = std::max(0.0f, scroll - input::wheelMove() * scrollSpeed);
    if (input::keyPressed(SDL_SCANCODE_ESCAPE)) {
        app.showMainMenu();
    }
}

void LevelSelectScreen::draw() {
    const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
    const char* title = mode == GameMode::Normal ? "Normal Mode" : "Performance Mode";
    ui::drawText(title, {centerX, 40.0f}, 52.0f, palette::accent, ui::Align::Center);

    const float top = 130.0f;
    const float bottom = static_cast<float>(gfx::screenHeight()) - 110.0f;
    gfx::beginClip({0.0f, top, static_cast<float>(gfx::screenWidth()), bottom - top});
    for (size_t i = 0; i < levels.size(); ++i) {
        const float y = top + static_cast<float>(i) * (rowHeight + rowGap) - scroll;
        const Rect playRect{centerX - listWidth * 0.5f, y, listWidth - editButtonWidth - rowGap, rowHeight};
        const Rect editRect{playRect.x + playRect.width + rowGap, y, editButtonWidth, rowHeight};
        const bool visible = y + rowHeight > top && y < bottom;
        if (ui::button(playRect, levels[i].name, visible)) {
            app.startGame(mode, levels[i]);
        }
        if (ui::button(editRect, "Edit", visible)) {
            app.openEditor(mode, levels[i]);
        }
    }
    gfx::endClip();
    if (levels.empty()) {
        ui::drawText("No levels found", {centerX, top + 40.0f}, 28.0f, palette::textMuted, ui::Align::Center);
    }
    const Rect backRect{centerX - listWidth * 0.5f, bottom + 30.0f, 200.0f, 56.0f};
    if (ui::button(backRect, "Back")) {
        app.showMainMenu();
    }
    const Rect newRect{centerX + listWidth * 0.5f - 200.0f, bottom + 30.0f, 200.0f, 56.0f};
    if (ui::button(newRect, "New Level")) {
        app.openEditor(mode, std::nullopt);
    }
}
