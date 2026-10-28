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
    const std::optional<MachineProfile::MeasuredLimit> Limit = App.GetMachineProfile().ballLimit(Session->backend());
    if (Limit && static_cast<int32>(Limit->targetFps) == ProbeTargetFps) {
        Session->cancelCapacityProbe();
        Session->useMachineBallLimit(Limit->balls);
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

// Loads the files as they are now: a parse failure drops to the load-error screen instead of keeping the old game.
void FBallsGameplay::ReloadFromDisk() {
    Watcher.markLoaded();
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
    // With a game running the change only shows as the watcher's disk state; the load-error screen retries at once.
    if (Watcher.pollChanged(std::chrono::steady_clock::now()) && !Session) {
        ReloadFromDisk();
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
            ApplyDefaultBallLimit();
        } else {
            ShowToast(FString::Printf(TEXT("%s failed: %s"), *BallsText::FromUtf8(backendKindName(Outcome->kind)),
                                      *BallsText::FromUtf8(Session->lastBackendFault())),
                      true);
        }
    }

    const CapacityProbe& Probe = Session->capacityProbe();
    if (Probe.isActive()) {
        bProbeResultShown = false;
    } else if (!bProbeResultShown && Probe.phase() == ProbePhase::Done) {
        App.GetMachineProfile().rememberBallLimit(Session->backend(), Probe.targetFps(), Probe.bestPassingCount());
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
    const BallCollisionsChange Change = Session->setBallCollisions(App.GetSettings().ballCollisions);
    if (Change != BallCollisionsChange::None) {
        ShowToast(BallsText::FromUtf8(ballCollisionsMessage(Change)), false);
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

std::shared_ptr<const SessionView> FBallsGameplay::sessionView() {
    if (!Session) {
        return nullptr;
    }
    auto View = std::make_shared<SessionView>();
    describeSession(*Session, *View);
    return View;
}

void FBallsGameplay::post(std::function<void(GameSession&)> Command) {
    if (Session) {
        Command(*Session);
    }
}

void FBallsGameplay::startCapacityProbe(int TargetFps) {
    ProbeTargetFps = TargetFps;
    StartCapacityProbe();
}

void FBallsGameplay::Restart() {
    if (Watcher.diskState() == LevelDiskState::Removed) {
        App.ShowLevelSelect(Mode);
        return;
    }
    if (Watcher.diskState() == LevelDiskState::Changed) {
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
