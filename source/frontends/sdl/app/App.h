#pragma once

#include "app/FrameTimings.h"
#include "app/LaunchOptions.h"
#include "app/MachineProfile.h"
#include "app/Paths.h"
#include "app/Screen.h"
#include "control/ControlServer.h"
#include "game/Settings.h"
#include "level/LevelData.h"
#include "render/BallView.h"
#include "render/IRenderer.h"
#include "sim/BackendFactory.h"
#include "ui/Painter.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

struct SDL_Window;
union SDL_Event;
struct ImFont;

int currentRefreshRate();

class App {
public:
    ~App();

    bool initialize(const LaunchOptions& options, std::string& error);
    bool frame();
    void handleEvent(const SDL_Event& event);

    void showMainMenu();
    void showLevelSelect(GameMode mode);
    void startGame(GameMode mode, const LevelFiles& files);
    void openEditor(GameMode mode, std::optional<LevelFiles> files);
    void showSettings();
    void quit() { running = false; }

    const Settings& settings() const { return currentSettings; }
    void applySettings(const Settings& settings, std::optional<BackendKind> backend = std::nullopt);
    std::string settingsJson() const;
    void notifySettingsConsumers();
    void applyOrientationToWindow();
    void applyOrientationHint(LayoutOrientation orientation);

    BallView& ballRenderer() { return *balls; }
    const FrameTimings& frameTimings() const { return timings; }
    float lastFrameSeconds() const { return frameSeconds; }
    BackendKind preferredBackend() const { return backendChoice; }
    void setPreferredBackend(BackendKind kind) { backendChoice = kind; }
    bool isVsyncEnabled() const { return renderer && renderer->isVsyncEnabled(); }
    MachineProfile& machineProfile() { return profile; }
    void setVsync(bool enabled);

private:
    bool openWindowAndRenderer(const LaunchOptions& options, std::string_view api, std::string& error);
    using Clock = std::chrono::steady_clock;

    void applyPendingScreen();
    void drawFrameTimingsOverlay() const;
    void updateTextInput();
    std::optional<ControlResponse> handleControl(const ControlRequest& request);
    std::string stateJson() const;

    SDL_Window* window = nullptr;
    std::unique_ptr<IRenderer> renderer;
    std::unique_ptr<BallView> balls;
    gfx::FontSet fonts;
    bool imguiCreated = false;
    std::unique_ptr<Screen> currentScreen;
    std::unique_ptr<Screen> pendingScreen;
    FrameTimings timings;
    ControlServer controlServer;
    MachineProfile profile;
    Settings currentSettings;
    BackendKind backendChoice = BackendKind::Avx2;
    bool running = true;
    bool textInputActive = false;
    bool screenshotPending = false;
    std::string screenshotPath;
    float frameSeconds = 0.0f;
    Clock::time_point previousUpdateStart = Clock::now();
};
