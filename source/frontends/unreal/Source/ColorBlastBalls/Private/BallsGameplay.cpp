#include "BallsGameplay.h"

#include "BallsApp.h"
#include "BallsText.h"
#include "InputCoreTypes.h"

#include <algorithm>
#include <chrono>

namespace {

constexpr float ToastSeconds = 3.0f;

}

FBallsGameplay::FBallsGameplay(FBallsApp& InApp, GameMode InMode, LevelFiles InFiles)
    : App(InApp), Mode(InMode), Files(MoveTemp(InFiles)) {
    LoadSession();
    Watcher.watch(Files);
}

void FBallsGameplay::LoadSession() {
    Result<LevelData> Level = loadLevel(Files);
    if (!Level) {
        LoadError = BallsText::FromUtf8(Level.error());
        return;
    }
    LoadError.Reset();
    Session = std::make_unique<GameSession>(Mode, std::move(*Level), App.GetPreferredBackend());
    ApplySettings();
}

void FBallsGameplay::HotReload() {
    Result<LevelData> Level = loadLevel(Files);
    if (!Level) {
        ShowToast(TEXT("Reload failed: ") + BallsText::FromUtf8(Level.error()), true);
        return;
    }
    if (Session) {
        Session->reload(std::move(*Level));
    } else {
        LoadError.Reset();
        Session = std::make_unique<GameSession>(Mode, std::move(*Level), App.GetPreferredBackend());
        ApplySettings();
    }
    bPaused = false;
    ShowToast(TEXT("Reloaded ") + BallsText::FromUtf8(Files.name), false);
}

void FBallsGameplay::ShowToast(FString Text, bool bIsError) { Toast = {MoveTemp(Text), ToastSeconds, bIsError}; }

bool FBallsGameplay::IsWon() const {
    return Session && Mode == GameMode::Normal && Session->state() == SessionState::Won;
}

void FBallsGameplay::Tick(float DeltaSeconds, double FrameSeconds, double WorkSeconds) {
    Toast.SecondsLeft = FMath::Max(0.0f, Toast.SecondsLeft - DeltaSeconds);
    if (Watcher.pollChanged(std::chrono::steady_clock::now())) {
        HotReload();
    }
    if (!Session || bPaused) {
        return;
    }
    Session->reportFrameSeconds(FrameSeconds, WorkSeconds);
    Session->update(DeltaSeconds);

    const CapacityProbe& Probe = Session->capacityProbe();
    if (Probe.isActive()) {
        bProbeResultShown = false;
    } else if (!bProbeResultShown && Probe.phase() == ProbePhase::Done) {
        ShowToast(FString::Printf(TEXT("%s limit on this machine: %s balls @60 FPS"),
                                  *BallsText::FromUtf8(backendKindName(Session->backend())),
                                  *BallsText::Grouped(Probe.bestPassingCount())),
                  false);
        bProbeResultShown = true;
    }
}

// The design gives gameplay no keyboard shortcuts: every action is a visible control. Only the load-error screen keeps
// Esc as Back, like the menus.
bool FBallsGameplay::HandleKey(const FKey& Key) {
    if (!Session && Key == EKeys::Escape) {
        App.ShowLevelSelect(Mode);
        return true;
    }
    return false;
}

void FBallsGameplay::ApplySettings() {
    if (!Session) {
        return;
    }
    Simulation& Sim = Session->simulation();
    const bool bWanted = App.GetSettings().ballCollisions;
    Sim.setBallCollisions(bWanted);
    // Only the Scalar backend implements collisions; asking for them elsewhere moves the game to Scalar.
    if (bWanted && !Sim.ballCollisionsActive() && Session->switchBackend(BackendKind::Scalar)) {
        App.SetPreferredBackend(BackendKind::Scalar);
        Session->simulation().setBallCollisions(true);
        ShowToast(TEXT("Ball collisions need the Scalar backend - switched to Scalar."), false);
    }
}

void FBallsGameplay::ChooseLauncher(int32 Column, int32 Row) {
    if (Session && !bPaused) {
        Session->chooseLauncher(Column, Row);
    }
}

void FBallsGameplay::StartAutoRelease() {
    if (Session) {
        Session->startAutoRelease();
    }
}

void FBallsGameplay::ToggleCapacityProbe() {
    if (!Session) {
        return;
    }
    if (Session->capacityProbe().isActive()) {
        Session->cancelCapacityProbe();
    } else {
        Session->startCapacityProbe();
    }
}

void FBallsGameplay::SelectSpawnRate(int32 Index) {
    if (Session) {
        Session->selectSpawnRate(Index);
    }
}

void FBallsGameplay::SelectBallMultiplier(int32 Index) {
    if (Session) {
        Session->selectBallMultiplier(Index);
    }
}

void FBallsGameplay::SelectBallLimit(int32 Index) {
    if (Session) {
        Session->selectBallLimit(Index);
    }
}

void FBallsGameplay::SelectMachineLimit() {
    if (!Session) {
        return;
    }
    if (const std::optional<BallCount> Measured = Session->machineBallLimit()) {
        Session->useMachineBallLimit(*Measured);
    } else {
        Session->startCapacityProbe();
    }
}

void FBallsGameplay::SwitchBackend(BackendKind Kind) {
    if (!Session) {
        return;
    }
    const FString Name = BallsText::FromUtf8(backendKindName(Kind));
    if (Session->switchBackend(Kind)) {
        App.SetPreferredBackend(Kind);
        ShowToast(TEXT("Backend: ") + Name, false);
    } else {
        ShowToast(FString::Printf(TEXT("%s unavailable: %s"), *Name,
                                  *BallsText::FromUtf8(backendUnavailableReason(Kind))),
                  true);
    }
}

void FBallsGameplay::Restart() {
    if (Session) {
        Session->restart();
        bPaused = false;
    }
}

void FBallsGameplay::OpenNextLevel() {
    const std::vector<LevelFiles> Levels = findLevels(App.LevelDirectory(Mode));
    const auto Current = std::ranges::find(Levels, Files.name, &LevelFiles::name);
    if (Current != Levels.end() && std::next(Current) != Levels.end()) {
        App.StartGame(Mode, *std::next(Current));
    } else {
        App.ShowLevelSelect(Mode);
    }
}
