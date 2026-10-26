#pragma once

#include "app/FrameTimings.h"
#include "app/LaunchOptions.h"
#include "app/MachineProfile.h"
#include "app/Paths.h"
#include "app/Screen.h"
#include "control/ControlServer.h"
#include "level/LevelData.h"
#include "render/BallRenderer.h"
#include "render/GpuTimer.h"
#include "sim/BackendFactory.h"

#include <memory>
#include <optional>

class App {
public:
    void run(const LaunchOptions& options);

    void showMainMenu();
    void showLevelSelect(GameMode mode);
    void startGame(GameMode mode, const LevelFiles& files);
    void openEditor(GameMode mode, std::optional<LevelFiles> files);
    void quit() { running = false; }

    BallRenderer& ballRenderer() { return balls; }
    const FrameTimings& frameTimings() const { return timings; }
    BackendKind preferredBackend() const { return backendChoice; }
    void setPreferredBackend(BackendKind kind) { backendChoice = kind; }
    bool isVsyncEnabled() const { return vsyncEnabled; }
    MachineProfile& machineProfile() { return profile; }
    void setVsync(bool enabled);

private:
    void initializeWindow(const LaunchOptions& options);
    void applyPendingScreen();
    void drawFrameTimingsOverlay() const;
    ControlResponse handleControl(const ControlRequest& request);
    std::string stateJson() const;

    std::unique_ptr<Screen> currentScreen;
    std::unique_ptr<Screen> pendingScreen;
    BallRenderer balls;
    GpuTimer gpuTimer;
    FrameTimings timings;
    ControlServer controlServer;
    MachineProfile profile;
    BackendKind backendChoice = BackendKind::Avx2;
    bool running = true;
    bool vsyncEnabled = false;
};
