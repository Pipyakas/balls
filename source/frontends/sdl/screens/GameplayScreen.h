#pragma once

#include "app/Screen.h"
#include "game/GameSession.h"
#include "game/SessionRunner.h"
#include "gpu/GpuAdapters.h"
#include "level/LevelData.h"
#include "render/ScreenLayout.h"
#include "screens/DebugPanel.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

class App;

class GameplayScreen final : public Screen, public ControlGame {
public:
    GameplayScreen(App& app, GameMode mode, LevelFiles files);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "gameplay"; }
    bool drawsFrameTimings() const override { return true; }
    std::optional<ControlResponse> handleControl(const ControlRequest& request) override;
    ControlGame* controlGame() override { return this; }
    void refreshSettings();
    void restartSession();

    GameMode gameMode() const override { return mode; }
    std::string levelName() const override { return files.name; }
    std::string levelLoadError() const override { return loadError; }
    LevelDiskState diskState() const override;
    std::shared_ptr<const SessionView> sessionView() override { return runner ? runner->view() : nullptr; }
    void post(std::function<void(GameSession&)> command) override;
    bool isPaused() const override;
    void setPaused(bool pause) override;
    void restart() override;

private:
    void loadSession();

    bool adoptLoadOutcome();
    void drawLoading();
    void handleLauncherClick(const GameplayLayout& layout);
    void refreshAutoBallLimit();
    void handleSessionEvents();
    void tickPhysics(const GameplayLayout& layout);
    void selectComputeAdapter(int adapter);
    std::vector<int> renderAdapterChoices() const;
    std::vector<int> computeAdapterChoices() const;
    void drawAdapterList(Rect area, const std::vector<GpuAdapterInfo>& adapters, std::span<const int> choices,
                         int selected, bool forCompute);

    void drawHeader(const GameplayLayout& layout);
    void drawMainSection(const GameplayLayout& layout);
    void drawInteractionSection(const GameplayLayout& layout);
    void drawPortraitLauncherRow(Rect area);
    void drawPerformanceControls(Rect area);
    void drawNormalControls(Rect area);
    float normalContentHeight() const;
    void drawBackendList(Rect area, BackendKind selected);
    BackendKind shownBackend() const;
    void drawDevicePicker(Rect area);
    void drawBallCollisions(Rect area);
    int drawOptionDropdown(Rect area, std::string_view current, std::span<const std::string> options,
                           int selected, bool& open);
    void drawOverlay();

    App& app;
    GameMode mode;
    LevelFiles files;

    std::unique_ptr<SessionRunner> runner;
    std::shared_ptr<const SessionView> view;
    std::string loadError;
    std::shared_ptr<LoadProgress> loadProgress;
    float controlsScroll = 0.0f;
    float performanceControlsHeight = 0.0f;
    float normalScroll = 0.0f;
    float portraitLauncherScroll = 0.0f;
    Vec2 portraitPressStart{};
    bool portraitPressing = false;
    bool portraitDragged = false;
    bool spawnDropdownOpen = false;
    bool limitDropdownOpen = false;
    float pauseSettingsScroll = 0.0f;
    DebugPanelState debugState;
};
