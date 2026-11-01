#pragma once

#include "BallsApp.h"
#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

#include <memory>

class FBallsApp;
struct FBallsToast;
struct FKey;

class FBallsGameplay : public ControlGame {
public:
    FBallsGameplay(FBallsApp& App, GameMode Mode, LevelFiles Files);

    void Tick(float DeltaSeconds, double WorkSeconds);
    bool HandleKey(const FKey& Key);

    void ApplySettings();

    void ChooseLauncher(int32 Column, int32 Row);
    void StartAutoRelease();
    void SelectSpawnRate(int32 Index);
    void SelectBallLimit(int32 Index);
    void SwitchBackend(BackendKind Kind);
    void Restart();

    LevelDiskState GetDiskState() const { return App.GetScreenFlow().diskState(); }

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

    const SessionView* GetView() const { return View.get(); }

    bool HasGame() const { return Host != nullptr; }

    GameSession* GetLiveSession() const { return Host ? Host->liveSession() : nullptr; }

    void SetVisibilitySlotCells(float Cells) { VisibilitySlotCells = Cells; }
    const FString& GetLoadError() const { return LoadError; }

    const LoadProgress* GetLoadProgress() const { return Progress.get(); }
    const FBallsToast& GetToast() const;
    bool IsPaused() const { return App.GetScreenFlow().isPaused(); }
    bool IsSettingsOpen() const { return App.GetScreenFlow().overlay() == FlowOverlay::Settings; }
    bool IsWon() const;

private:
    void LoadSession();

    void AdoptLoadOutcome();
    void RefreshAutoBallLimit();
    void ReloadFromDisk();
    void ShowToast(FString Text, bool bIsError);

    FBallsApp& App;
    GameMode Mode;
    LevelFiles Files;
    std::unique_ptr<SessionHost> Host;
    std::shared_ptr<const SessionView> View;
    float VisibilitySlotCells = 0.0f;
    FString LoadError;
    std::shared_ptr<LoadProgress> Progress;
};
