#pragma once

#include "app/Screen.h"
#include "game/GameMode.h"

#include <string>

class App;

class LevelSelectScreen final : public Screen {
public:
    LevelSelectScreen(App& app, GameMode mode);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "levels"; }
    std::string stateJson() const override;
    float toastTop() const override;

private:
    float listBottom() const;
    void drawImportControls(float centerX, float top);

    App& app;
    GameMode mode;
    float scroll = 0.0f;
};
