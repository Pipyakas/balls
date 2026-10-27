#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

#include <memory>

class FBallsApp;
struct FBallsToast;
struct FKey;

// One level being played: owns the core GameSession, hot reload and the pause/toast state.
// Port of frontends/raylib/screens/GameplayScreen without the drawing.
class FBallsGameplay {
public:
    FBallsGameplay(FBallsApp& App, GameMode Mode, LevelFiles Files);

    void Tick(float DeltaSeconds, double FrameSeconds, double WorkSeconds);
    bool HandleKey(const FKey& Key);
    // Pushes the app-wide Settings (ball collisions) into the running session.
    void ApplySettings();

    void ChooseLauncher(int32 Column, int32 Row);
    void StartAutoRelease();
    void ToggleCapacityProbe();
    int32 GetProbeTargetFps() const { return ProbeTargetFps; }
    void SetProbeTargetFps(int32 Fps) { ProbeTargetFps = FMath::Clamp(Fps, MinimumProbeTargetFps, MaximumProbeTargetFps); }
    // True after picking the Auto ball limit while no probe result exists yet.
    bool IsAutoLimitPending() const { return bAutoLimitPending && Session && !Session->machineBallLimit(); }
    static constexpr int32 MinimumProbeTargetFps = 10;
    static constexpr int32 MaximumProbeTargetFps = 1000;
    void SelectSpawnRate(int32 Index);
    void SelectBallMultiplier(int32 Index);
    void SelectBallLimit(int32 Index);
    void SelectMachineLimit();
    void SwitchBackend(BackendKind Kind);
    void Restart();
    void SetPaused(bool bInPaused) {
        bPaused = bInPaused;
        bSettingsOpen = false;
    }
    void SetSettingsOpen(bool bOpen) { bSettingsOpen = bOpen; }
    void OpenNextLevel();

    // The level files changed under a running game. The game is never swapped mid-play: the header says so and
    // Restart loads the new version ("Changes to the level being played").
    enum class EMapNotice { None, Changed, Removed };
    EMapNotice GetMapNotice() const { return MapNotice; }

    GameMode GetMode() const { return Mode; }
    const LevelFiles& GetFiles() const { return Files; }
    GameSession* GetSession() const { return Session.get(); }
    const FString& GetLoadError() const { return LoadError; }
    const FBallsToast& GetToast() const;
    bool IsPaused() const { return bPaused; }
    bool IsSettingsOpen() const { return bSettingsOpen; }
    bool IsWon() const;

private:
    void LoadSession();
    void ApplyRememberedMachineLimit();
    void OnFilesChanged();
    void ReloadFromDisk();
    void ShowToast(FString Text, bool bIsError);

    FBallsApp& App;
    GameMode Mode;
    LevelFiles Files;
    std::unique_ptr<GameSession> Session;
    FString LoadError;
    LevelWatcher Watcher;
    bool bPaused = false;
    EMapNotice MapNotice = EMapNotice::None;
    bool bSettingsOpen = false;
    bool bProbeResultShown = true;
    bool bAutoLimitPending = false;
    int32 ProbeTargetFps = static_cast<int32>(defaultProbeTargetFps);
};
