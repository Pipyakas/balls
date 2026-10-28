#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

#include <memory>

class FBallsApp;
struct FBallsToast;
struct FKey;

// One level being played: owns the core session (on the game thread, or on the core's physics thread with
// -BallsPhysicsThread=1), hot reload and the pause/toast state. Everything else reads the session's latest view.
// Port of frontends/raylib/screens/GameplayScreen without the drawing.
class FBallsGameplay : public ControlGame {
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
    bool IsAutoLimit() const { return View && (View->usingMachineLimit || View->probe.isActive()); }
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
    LevelDiskState GetDiskState() const { return Watcher.diskState(); }

    // ControlGame: the HTTP control API's view of this game, answered by the core.
    GameMode gameMode() const override { return Mode; }
    std::string levelName() const override { return Files.name; }
    std::string levelLoadError() const override { return TCHAR_TO_UTF8(*LoadError); }
    LevelDiskState diskState() const override { return Watcher.diskState(); }
    std::shared_ptr<const SessionView> sessionView() override { return View; }
    void post(std::function<void(GameSession&)> Command) override;
    bool isPaused() const override { return bPaused; }
    void setPaused(bool bPause) override { SetPaused(bPause); }
    void restart() override { Restart(); }
    int capacityProbeTargetFps() const override { return ProbeTargetFps; }
    void startCapacityProbe(int TargetFps) override;

    GameMode GetMode() const { return Mode; }
    const LevelFiles& GetFiles() const { return Files; }
    // The session as of this frame's tick; null while there is no game (load error) or it is still starting.
    const SessionView* GetView() const { return View.get(); }
    // False while the level failed to load and the load-error screen shows.
    bool HasGame() const { return Host != nullptr; }
    // The session itself while it runs on the game thread, so the balls are drawn straight from its backend.
    GameSession* GetLiveSession() const { return Host ? Host->liveSession() : nullptr; }
    // Slot size of the ball visibility grid, in board cells, for a session on the physics thread to size it.
    void SetVisibilitySlotCells(float Cells) { VisibilitySlotCells = Cells; }
    const FString& GetLoadError() const { return LoadError; }
    const FBallsToast& GetToast() const;
    bool IsPaused() const { return bPaused; }
    bool IsSettingsOpen() const { return bSettingsOpen; }
    bool IsWon() const;

private:
    void LoadSession();
    void ApplyDefaultBallLimit();
    void ReloadFromDisk();
    void ShowToast(FString Text, bool bIsError);

    FBallsApp& App;
    GameMode Mode;
    LevelFiles Files;
    std::unique_ptr<SessionHost> Host;
    std::shared_ptr<const SessionView> View;
    float VisibilitySlotCells = 0.0f;
    FString LoadError;
    LevelWatcher Watcher;
    bool bPaused = false;
    bool bSettingsOpen = false;
    bool bProbeResultShown = true;
    int32 ProbeTargetFps = static_cast<int32>(defaultProbeTargetFps);
};
