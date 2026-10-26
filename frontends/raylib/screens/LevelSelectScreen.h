#pragma once

#include "app/Screen.h"
#include "game/GameMode.h"
#include "level/LevelData.h"

#include <vector>

class App;

class LevelSelectScreen final : public Screen {
public:
    LevelSelectScreen(App& app, GameMode mode);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "levels"; }

private:
    App& app;
    GameMode mode;
    std::vector<LevelFiles> levels;
    float scroll = 0.0f;
};
