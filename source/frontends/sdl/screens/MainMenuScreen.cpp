#include "screens/MainMenuScreen.h"

#include "app/App.h"
#include "render/Palette.h"
#include "ui/Widgets.h"

#include "app/Input.h"
#include "ui/Painter.h"

#include <cmath>

namespace {

constexpr float buttonWidth = 380.0f;
constexpr float buttonHeight = 64.0f;
constexpr float buttonGap = 18.0f;
constexpr float titleSize = 72.0f;
constexpr int decorativeBallCount = 28;

void drawDecorativeBalls(float time) {
    const float width = static_cast<float>(gfx::screenWidth());
    const float height = static_cast<float>(gfx::screenHeight());
    for (int i = 0; i < decorativeBallCount; ++i) {
        const float phase = static_cast<float>(i) * 1.618f;
        const float x = width * (0.5f + 0.45f * std::sin(time * 0.3f + phase * 2.0f));
        const float y = height * (0.5f + 0.45f * std::cos(time * 0.23f + phase * 3.0f));
        gfx::fillCircle({x, y}, 10.0f + static_cast<float>(i % 4) * 4.0f,
                        gfx::fade(palette::ballColor(i % ballColorCount), 0.35f));
    }
}

} // namespace

void MainMenuScreen::update(float deltaTime) {
    time += deltaTime;
}

void MainMenuScreen::draw() {
    drawDecorativeBalls(time);
    const float centerX = static_cast<float>(gfx::screenWidth()) * 0.5f;
    const float top = static_cast<float>(gfx::screenHeight()) * 0.22f;
    ui::drawText(app.flow().title(), {centerX, top + titleSize * 0.5f}, titleSize, palette::accent, ui::Align::Center);

    const float firstButtonY = top + titleSize * 2.2f;
    const auto buttons = app.flow().buttons();
    for (size_t i = 0; i < buttons.size(); ++i) {
        const Rect rect{centerX - buttonWidth * 0.5f,
                        firstButtonY + static_cast<float>(i) * (buttonHeight + buttonGap), buttonWidth,
                        buttonHeight};
        if (ui::button(rect, buttons[i].label, buttons[i].enabled)) {
            app.flow().activate(buttons[i].action);
        }
    }
}
