#include "BallsGameplay.h"

#include "BallsApp.h"
#include "BallsText.h"
#include "InputCoreTypes.h"

#include <algorithm>
#include <chrono>

namespace {


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
    ApplyDefaultBallLimit();
}

void FBallsGameplay::ApplyDefaultBallLimit() {
    if (Mode == GameMode::Performance && Session) {
        UseAutoBallLimit();
    }
}

void FBallsGameplay::UseAutoBallLimit() {
    if (!Session) {
        return;
    }
    const std::optional<FBallsApp::FMachineLimit> Limit = App.GetMachineLimit(Session->backend());
    if (Limit && static_cast<int32>(Limit->TargetFps) == ProbeTargetFps) {
        Session->cancelCapacityProbe();
        Session->useMachineBallLimit(Limit->Balls);
    } else {
        StartCapacityProbe();
    }
}

void FBallsGameplay::StartCapacityProbe() {
    if (Session) {
        Session->startCapacityProbe(static_cast<double>(ProbeTargetFps));
    }
}

void FBallsGameplay::CancelCapacityProbe() {
    if (Session) {
        Session->cancelCapacityProbe();
    }
}

void FBallsGameplay::SetProbeTargetFps(int32 Fps) {
    const int32 Clamped = FMath::Clamp(Fps, MinimumProbeTargetFps, MaximumProbeTargetFps);
    if (Clamped == ProbeTargetFps) {
        return;
    }
    ProbeTargetFps = Clamped;
    if (IsAutoLimit()) {
        StartCapacityProbe();
    }
}

FString FBallsGameplay::GetProbeStatus() const {
    if (!Session) {
        return FString();
    }
    const CapacityProbe& Probe = Session->capacityProbe();
    if (Probe.isActive()) {
        return FString::Printf(TEXT("Probing... %s balls, %.0f fps"),
                               *BallsText::Grouped(Session->simulation().liveBallCount()), Probe.lastFps());
    }
    if (Probe.phase() == ProbePhase::Done) {
        return FString::Printf(TEXT("Max @%.0f FPS: %s"), Probe.targetFps(), *BallsText::Grouped(Probe.bestPassingCount()));
    }
    if (const std::optional<BallCount> Measured = Session->machineBallLimit()) {
        return FString::Printf(TEXT("Max: %s"), *BallsText::Grouped(*Measured));
    }
    return FString();
}

void FBallsGameplay::OnFilesChanged() {
    if (!Session) {
        // The load-error screen retries by itself whenever the files change again.
        ReloadFromDisk();
        return;
    }
    std::error_code Error;
    const bool bGone = !std::filesystem::exists(Files.bricksPath, Error) || !std::filesystem::exists(Files.launchersPath, Error);
    MapNotice = bGone ? EMapNotice::Removed : EMapNotice::Changed;
}

// Loads the files as they are now: a parse failure drops to the load-error screen instead of keeping the old game.
void FBallsGameplay::ReloadFromDisk() {
    MapNotice = EMapNotice::None;
    Result<LevelData> Level = loadLevel(Files);
    if (!Level) {
        Session.reset();
        LoadError = BallsText::FromUtf8(Level.error());
        return;
    }
    LoadError.Reset();
    if (Session) {
        Session->reload(std::move(*Level));
    } else {
        Session = std::make_unique<GameSession>(Mode, std::move(*Level), App.GetPreferredBackend());
        ApplySettings();
        ApplyDefaultBallLimit();
    }
    bPaused = false;
}

void FBallsGameplay::ShowToast(FString Text, bool bIsError) { App.ShowToast(MoveTemp(Text), bIsError); }

const FBallsToast& FBallsGameplay::GetToast() const { return App.GetToast(); }

bool FBallsGameplay::IsWon() const {
    return Session && Mode == GameMode::Normal && Session->state() == SessionState::Won;
}

void FBallsGameplay::Tick(float DeltaSeconds, double FrameSeconds, double WorkSeconds) {
    if (Watcher.pollChanged(std::chrono::steady_clock::now())) {
        OnFilesChanged();
    }
    if (!Session || bPaused) {
        return;
    }
    Session->reportFrameSeconds(FrameSeconds, WorkSeconds);
    // A backend switch builds in the background; the balls stay with the old backend until it reports in.
    Session->pollBackgroundWork();
    Session->update(DeltaSeconds);

    if (const std::optional<BackendSwitchOutcome> Outcome = Session->takeBackendSwitchOutcome()) {
        if (Outcome->succeeded) {
            // A switch inside a game lasts for that game only; the Settings page owns the remembered default.
            ShowToast(TEXT("Backend: ") + BallsText::FromUtf8(backendKindName(Outcome->kind)), false);
            if (bCollisionsNeedScalar) {
                bCollisionsNeedScalar = false;
                Session->simulation().setBallCollisions(true);
                ShowToast(TEXT("Ball collisions need the Scalar backend - switched to Scalar."), false);
            }
            ApplyDefaultBallLimit();
        } else {
            bCollisionsNeedScalar = false;
            ShowToast(FString::Printf(TEXT("%s failed: %s"), *BallsText::FromUtf8(backendKindName(Outcome->kind)),
                                      *BallsText::FromUtf8(Session->lastBackendFault())),
                      true);
        }
    }

    const CapacityProbe& Probe = Session->capacityProbe();
    if (Probe.isActive()) {
        bProbeResultShown = false;
    } else if (!bProbeResultShown && Probe.phase() == ProbePhase::Done) {
        App.RememberMachineLimit(Session->backend(), Probe.targetFps(), Probe.bestPassingCount());
        ShowToast(FString::Printf(TEXT("%s limit on this machine: %s balls @%.0f FPS"),
                                  *BallsText::FromUtf8(backendKindName(Session->backend())),
                                  *BallsText::Grouped(Probe.bestPassingCount()), Probe.targetFps()),
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
    // Only the Scalar backend implements collisions; asking for them elsewhere moves the game to Scalar. The switch
    // finishes on a later frame, so Tick reports it once the new backend has adopted the balls.
    if (bWanted && !Sim.ballCollisionsActive() && Session->backend() != BackendKind::Scalar && isBackendAvailable(BackendKind::Scalar)) {
        bCollisionsNeedScalar = true;
        Session->requestBackend(BackendKind::Scalar);
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
        Session->cancelCapacityProbe();
        Session->selectBallLimit(Index);
    }
}

void FBallsGameplay::SwitchBackend(BackendKind Kind) {
    if (!Session) {
        return;
    }
    const FString Name = BallsText::FromUtf8(backendKindName(Kind));
    if (!isBackendAvailable(Kind)) {
        ShowToast(FString::Printf(TEXT("%s unavailable: %s"), *Name,
                                  *BallsText::FromUtf8(backendUnavailableReason(Kind))),
                  true);
        return;
    }
    // Building the backend takes long enough to run in the background, so the game keeps playing on the old one and
    // Tick reports the switch when it lands.
    Session->requestBackend(Kind);
}

void FBallsGameplay::Restart() {
    if (MapNotice == EMapNotice::Removed) {
        App.ShowLevelSelect(Mode);
        return;
    }
    if (MapNotice == EMapNotice::Changed) {
        ReloadFromDisk();
        return;
    }
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
