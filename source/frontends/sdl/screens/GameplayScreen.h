#pragma once

#include "app/Screen.h"
#include "game/GameSession.h"
#include "level/LevelData.h"
#include "level/LevelWatcher.h"
#include "render/ScreenLayout.h"
#include "screens/DebugPanel.h"

#include <memory>
#include <string>

class App;

class GameplayScreen final : public Screen {
public:
    GameplayScreen(App& app, GameMode mode, LevelFiles files);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "gameplay"; }
    bool drawsFrameTimings() const override { return true; }
    std::string stateJson() const override;
    std::optional<ControlResponse> handleControl(const ControlRequest& request) override;

private:
    struct Toast {
        std::string text;
        float secondsLeft = 0.0f;
        bool isError = false;
    };

    // What happened to the level's files since this session loaded them. The level in play is never swapped
    // mid-game; the header says so and Restart picks the new files up.
    enum class DiskState { Unchanged, Changed, Removed };

    void loadSession();
    void noticeDiskChange();
    void restartLevel();
    std::string_view diskNotice() const;
    void handleShortcuts();
    void handleLauncherClick(const GameplayLayout& layout);
    void showToast(std::string text, bool isError);
    void openNextLevel();
    void applyRememberedMachineLimit();
    void rememberFinishedProbe();
    void startProbe();

    void drawHeader(const GameplayLayout& layout);
    void drawMainSection(const GameplayLayout& layout);
    void drawInteractionSection(const GameplayLayout& layout);
    void drawPerformanceControls(Rect area);
    void drawOverlay();
    void drawToast();

    App& app;
    GameMode mode;
    LevelFiles files;
    std::unique_ptr<GameSession> session;
    std::string loadError;
    LevelWatcher watcher;
    DiskState diskState = DiskState::Unchanged;
    Toast toast;
    bool paused = false;
    bool probeResultSaved = true;
    int probeTargetFps = 60;
};
