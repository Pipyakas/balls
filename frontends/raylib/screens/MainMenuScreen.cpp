#include "screens/MainMenuScreen.h"

#include "app/App.h"
#include "render/Palette.h"
#include "ui/Widgets.h"

#include <raylib.h>

#include <cmath>

namespace {

constexpr float buttonWidth = 380.0f;
constexpr float buttonHeight = 64.0f;
constexpr float buttonGap = 18.0f;
constexpr float titleSize = 72.0f;
constexpr int decorativeBallCount = 28;

void drawDecorativeBalls(float time) {
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    for (int i = 0; i < decorativeBallCount; ++i) {
        const float phase = static_cast<float>(i) * 1.618f;
        const float x = width * (0.5f + 0.45f * std::sin(time * 0.3f + phase * 2.0f));
        const float y = height * (0.5f + 0.45f * std::cos(time * 0.23f + phase * 3.0f));
        DrawCircleV({x, y}, 10.0f + static_cast<float>(i % 4) * 4.0f, Fade(palette::ballColor(i % ballColorCount), 0.35f));
    }
}

}

void MainMenuScreen::update(float deltaTime) { time += deltaTime; }

void MainMenuScreen::draw() {
    drawDecorativeBalls(time);
    const float centerX = static_cast<float>(GetScreenWidth()) * 0.5f;
    const float top = static_cast<float>(GetScreenHeight()) * 0.22f;
    ui::drawText("COLOR BLAST", {centerX, top}, titleSize, palette::accent, ui::Align::Center);
    ui::drawText("BALLS", {centerX, top + titleSize}, titleSize * 0.7f, palette::text, ui::Align::Center);

    const float firstButtonY = top + titleSize * 2.2f;
    auto buttonRect = [&](int index) {
        return Rectangle{centerX - buttonWidth * 0.5f, firstButtonY + static_cast<float>(index) * (buttonHeight + buttonGap),
                         buttonWidth, buttonHeight};
    };
    if (ui::button(buttonRect(0), "Normal Mode")) {
        app.showLevelSelect(GameMode::Normal);
    }
    if (ui::button(buttonRect(1), "Performance Mode")) {
        app.showLevelSelect(GameMode::Performance);
    }
    if (ui::button(buttonRect(2), "Map Editor")) {
        app.openEditor(GameMode::Normal, std::nullopt);
    }
    if (ui::button(buttonRect(3), "Quit")) {
        app.quit();
    }
}
