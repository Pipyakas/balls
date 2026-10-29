#pragma once

#include "BallsApp.h"
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

    void Tick(float DeltaSeconds, double WorkSeconds);
    bool HandleKey(const FKey& Key);
    // Pushes the app-wide Settings (ball collisions) into the running session.
    void ApplySettings();

    void ChooseLauncher(int32 Column, int32 Row);
    void StartAutoRelease();
    // Auto sets the physics backend's capacity as the ball limit. Every Performance game starts on it.
    void UseAutoBallLimit();
    // True while the Auto ball limit is chosen.
    bool IsAutoLimit() const { return View && View->usingMachineLimit; }
    void SelectSpawnRate(int32 Index);
    void SelectBallMultiplier(int32 Index);
    void SelectBallLimit(int32 Index);
    void SwitchBackend(BackendKind Kind);
    void Restart();

    // The level files changed under a running game. The game is never swapped mid-play: the header says so and
    // Restart loads the new version ("Changes to the level being played").
    LevelDiskState GetDiskState() const { return App.GetScreenFlow().diskState(); }

    // ControlGame: the HTTP control API's view of this game, answered by the core.
    GameMode gameMode() const override { return Mode; }
    std::string levelName() const override { return Files.name; }
    std::string levelLoadError() const override { return TCHAR_TO_UTF8(*LoadError); }
    LevelDiskState diskState() const override { return App.GetScreenFlow().diskState(); }
    std::shared_ptr<const SessionView> sessionView() override { return View; }
    void post(std::function<void(GameSession&)> Command) override;
    bool isPaused() const override { return App.GetScreenFlow().isPaused(); }
    void setPaused(bool bPause) override {
        if (bPause != App.GetScreenFlow().isPaused()) {
            App.GetScreenFlow().activate(FlowAction::TogglePause);
            App.ProcessFlowRequests();
            App.ApplyFlowScreen();
        }
    }
    void restart() override { Restart(); }

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
    bool IsPaused() const { return App.GetScreenFlow().isPaused(); }
    bool IsSettingsOpen() const { return App.GetScreenFlow().overlay() == FlowOverlay::Settings; }
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
};
