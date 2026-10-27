#pragma once

#include "app/Screen.h"
#include "game/GameSession.h"
#include "level/LevelData.h"
#include "level/LevelWatcher.h"
#include "render/ScreenLayout.h"
#include "screens/DebugPanel.h"

#include <memory>
#include <span>
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
    void refreshSettings();
    void openSettingsOverlay();

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
    float performanceContentHeight() const;
    void drawNormalControls(Rect area);
    float normalContentHeight() const;
    void drawBackendList(Rect area, BackendKind selected);
    int drawOptionDropdown(Rect area, std::string_view current, std::span<const std::string> options,
                           int selected, bool& open);
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
    bool settingsOpen = false;
    bool probeResultSaved = true;
    int probeTargetFps = 60;
    float controlsScroll = 0.0f;
    float normalScroll = 0.0f;
    bool spawnDropdownOpen = false;
    bool countDropdownOpen = false;
    bool limitDropdownOpen = false;
    DebugPanelState debugState;
};
