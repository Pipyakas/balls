#pragma once

#include "app/Screen.h"
#include "game/GameMode.h"
#include "level/LevelData.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

class App;

class LevelSelectScreen final : public Screen {
public:
    LevelSelectScreen(App& app, GameMode mode);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "levels"; }
    bool importLevels(std::span<const std::filesystem::path> paths) override;
    std::string stateJson() const override;
    float toastTop() const override;

private:
    App& app;
    GameMode mode;
    void refreshLevels();
    float listBottom() const;
    void scrollTo(const std::string& levelName);
    void drawImportControls(float centerX, float top);

    std::vector<LevelFiles> levels;
    std::filesystem::path folder;
    std::chrono::steady_clock::time_point nextRefresh{};
    std::string highlightedLevel;
    float highlightSecondsLeft = 0.0f;
    float scroll = 0.0f;
};
