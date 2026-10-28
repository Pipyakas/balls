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
    // Auto means the most balls this machine holds at the target frame rate: the remembered probe result when it
    // was measured at this target, otherwise a new probe. Every Performance game starts on it.
    void UseAutoBallLimit();
    void StartCapacityProbe();
    void CancelCapacityProbe();
    // True while the Auto ball limit is chosen (resolved or still probing).
    bool IsAutoLimit() const {
        return Session && (Session->isUsingMachineBallLimit() || Session->capacityProbe().isActive());
    }
    // The muted line under the Auto dropdown: running, done, or the remembered result.
    FString GetProbeStatus() const;
    int32 GetProbeTargetFps() const { return ProbeTargetFps; }
    // Changing the target probes again while Auto is chosen.
    void SetProbeTargetFps(int32 Fps);
    static constexpr int32 MinimumProbeTargetFps = 10;
    static constexpr int32 MaximumProbeTargetFps = 1000;
    void SelectSpawnRate(int32 Index);
    void SelectBallMultiplier(int32 Index);
    void SelectBallLimit(int32 Index);
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
    void ApplyDefaultBallLimit();
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
    // Set while waiting for the Scalar switch that ball collisions need, so Tick can confirm it once it lands.
    bool bCollisionsNeedScalar = false;
    int32 ProbeTargetFps = static_cast<int32>(defaultProbeTargetFps);
};
