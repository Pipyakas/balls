#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

#include <memory>

class FBallsApp;
struct FKey;

// One level being played: owns the core GameSession, hot reload and the pause/toast state.
// Port of frontends/raylib/screens/GameplayScreen without the drawing.
class FBallsGameplay {
public:
    struct FToast {
        FString Text;
        float SecondsLeft = 0.0f;
        bool bIsError = false;
    };

    FBallsGameplay(FBallsApp& App, GameMode Mode, LevelFiles Files);

    void Tick(float DeltaSeconds, double FrameSeconds, double WorkSeconds);
    bool HandleKey(const FKey& Key);
    // Pushes the app-wide Settings (ball collisions) into the running session.
    void ApplySettings();

    void ChooseLauncher(int32 Column, int32 Row);
    void StartAutoRelease();
    void ToggleCapacityProbe();
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

    GameMode GetMode() const { return Mode; }
    const LevelFiles& GetFiles() const { return Files; }
    GameSession* GetSession() const { return Session.get(); }
    const FString& GetLoadError() const { return LoadError; }
    const FToast& GetToast() const { return Toast; }
    bool IsPaused() const { return bPaused; }
    bool IsSettingsOpen() const { return bSettingsOpen; }
    bool IsWon() const;

private:
    void LoadSession();
    void HotReload();
    void ShowToast(FString Text, bool bIsError);

    FBallsApp& App;
    GameMode Mode;
    LevelFiles Files;
    std::unique_ptr<GameSession> Session;
    FString LoadError;
    LevelWatcher Watcher;
    FToast Toast;
    bool bPaused = false;
    bool bSettingsOpen = false;
    bool bProbeResultShown = true;
};
