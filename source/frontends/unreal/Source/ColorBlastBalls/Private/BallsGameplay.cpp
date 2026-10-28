#include "BallsGameplay.h"

#include "BallsApp.h"
#include "BallsText.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <algorithm>
#include <chrono>

namespace {

// -BallsPhysicsThread=1 runs the session on the core's physics thread (SessionRunner), as the SDL front end does;
// 0 keeps it on the game thread, inside the frame.
SessionThreading ThreadingFromCommandLine() {
    int32 Value = 0;
    FParse::Value(FCommandLine::Get(), TEXT("BallsPhysicsThread="), Value);
    return Value != 0 ? SessionThreading::PhysicsThread : SessionThreading::UiThread;
}

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
    Host = std::make_unique<SessionHost>(Mode, std::move(*Level), App.GetPreferredBackend(), ThreadingFromCommandLine());
    View = Host->view();
    ApplySettings();
    ApplyDefaultBallLimit();
}

void FBallsGameplay::ApplyDefaultBallLimit() {
    if (Mode == GameMode::Performance && Host) {
        UseAutoBallLimit();
    }
}

void FBallsGameplay::UseAutoBallLimit() {
    if (!Host) {
        return;
    }
    // The backend may still be settling (a physics thread, a fallback), so the session picks its own limit.
    Host->post([Profile = App.GetMachineProfile(), TargetFps = ProbeTargetFps](GameSession& Session) {
        const std::optional<MachineProfile::MeasuredLimit> Limit = Profile.ballLimit(Session.backend());
        if (Limit && static_cast<int32>(Limit->targetFps) == TargetFps) {
            Session.cancelCapacityProbe();
            Session.useMachineBallLimit(Limit->balls);
        } else {
            Session.startCapacityProbe(static_cast<double>(TargetFps));
        }
    });
}

void FBallsGameplay::StartCapacityProbe() {
    if (Host) {
        Host->post([TargetFps = static_cast<double>(ProbeTargetFps)](GameSession& Session) {
            Session.startCapacityProbe(TargetFps);
        });
    }
}

void FBallsGameplay::CancelCapacityProbe() {
    if (Host) {
        Host->post([](GameSession& Session) { Session.cancelCapacityProbe(); });
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
    if (!View) {
        return FString();
    }
    const CapacityProbe& Probe = View->probe;
    if (Probe.isActive()) {
        return FString::Printf(TEXT("Probing... %s balls, %.0f fps"), *BallsText::Grouped(View->liveBalls), Probe.lastFps());
    }
    if (Probe.phase() == ProbePhase::Done) {
        return FString::Printf(TEXT("Max @%.0f FPS: %s"), Probe.targetFps(), *BallsText::Grouped(Probe.bestPassingCount()));
    }
    if (View->machineLimit) {
        return FString::Printf(TEXT("Max: %s"), *BallsText::Grouped(*View->machineLimit));
    }
    return FString();
}

// Loads the files as they are now: a parse failure drops to the load-error screen instead of keeping the old game.
void FBallsGameplay::ReloadFromDisk() {
    Watcher.markLoaded();
    Result<LevelData> Level = loadLevel(Files);
    if (!Level) {
        Host.reset();
        View.reset();
        LoadError = BallsText::FromUtf8(Level.error());
        return;
    }
    LoadError.Reset();
    if (Host) {
        Host->post([Loaded = std::move(*Level)](GameSession& Session) { Session.reload(Loaded); });
    } else {
        Host = std::make_unique<SessionHost>(Mode, std::move(*Level), App.GetPreferredBackend(), ThreadingFromCommandLine());
        View = Host->view();
        ApplySettings();
        ApplyDefaultBallLimit();
    }
    bPaused = false;
}

void FBallsGameplay::ShowToast(FString Text, bool bIsError) { App.ShowToast(MoveTemp(Text), bIsError); }

const FBallsToast& FBallsGameplay::GetToast() const { return App.GetToast(); }

bool FBallsGameplay::IsWon() const { return View && Mode == GameMode::Normal && View->state == SessionState::Won; }

void FBallsGameplay::Tick(float DeltaSeconds, double FrameSeconds, double WorkSeconds) {
    // With a game running the change only shows as the watcher's disk state; the load-error screen retries at once.
    if (Watcher.pollChanged(std::chrono::steady_clock::now()) && !Host) {
        ReloadFromDisk();
    }
    if (!Host) {
        return;
    }
    // On the game thread this steps the session; on the physics thread it only hands over the frame's timings and
    // takes the newest view. A backend switch builds in the background either way.
    Host->tick({bPaused, FrameSeconds, WorkSeconds, VisibilitySlotCells}, DeltaSeconds);
    View = Host->view();
    for (const SessionEvent& Event : Host->takeEvents()) {
        if (Event.kind == SessionEventKind::BallCollisionsChanged) {
            ShowToast(BallsText::FromUtf8(ballCollisionsMessage(Event.collisionsChange)), false);
        } else if (Event.kind == SessionEventKind::BackendSwitched) {
            // A switch inside a game lasts for that game only; the Settings page owns the remembered default.
            ShowToast(TEXT("Backend: ") + BallsText::FromUtf8(backendKindName(Event.backend)), false);
            ApplyDefaultBallLimit();
        } else {
            ShowToast(FString::Printf(TEXT("%s failed: %s"), *BallsText::FromUtf8(backendKindName(Event.backend)),
                                      View ? *BallsText::FromUtf8(View->backendFault) : TEXT("")),
                      true);
        }
    }
    if (!View) {
        return;
    }

    const CapacityProbe& Probe = View->probe;
    if (Probe.isActive()) {
        bProbeResultShown = false;
    } else if (!bProbeResultShown && Probe.phase() == ProbePhase::Done) {
        App.GetMachineProfile().rememberBallLimit(View->backend, Probe.targetFps(), Probe.bestPassingCount());
        ShowToast(FString::Printf(TEXT("%s limit on this machine: %s balls @%.0f FPS"),
                                  *BallsText::FromUtf8(backendKindName(View->backend)),
                                  *BallsText::Grouped(Probe.bestPassingCount()), Probe.targetFps()),
                  false);
        bProbeResultShown = true;
    }
}

// The design gives gameplay no keyboard shortcuts: every action is a visible control. Only the load-error screen keeps
// Esc as Back, like the menus.
bool FBallsGameplay::HandleKey(const FKey& Key) {
    if (!Host && Key == EKeys::Escape) {
        App.ShowLevelSelect(Mode);
        return true;
    }
    return false;
}

void FBallsGameplay::ApplySettings() {
    if (Host) {
        // What it did, with the game's toast, comes back as an event at the next tick.
        Host->setBallCollisions(App.GetSettings().ballCollisions);
    }
}

void FBallsGameplay::ChooseLauncher(int32 Column, int32 Row) {
    if (Host && !bPaused) {
        Host->post([Column, Row](GameSession& Session) { Session.chooseLauncher(Column, Row); });
    }
}

void FBallsGameplay::StartAutoRelease() {
    if (Host) {
        Host->post([](GameSession& Session) { Session.startAutoRelease(); });
    }
}

void FBallsGameplay::SelectSpawnRate(int32 Index) {
    if (Host) {
        Host->post([Index](GameSession& Session) { Session.selectSpawnRate(Index); });
    }
}

void FBallsGameplay::SelectBallMultiplier(int32 Index) {
    if (Host) {
        Host->post([Index](GameSession& Session) { Session.selectBallMultiplier(Index); });
    }
}

void FBallsGameplay::SelectBallLimit(int32 Index) {
    if (Host) {
        Host->post([Index](GameSession& Session) {
            Session.cancelCapacityProbe();
            Session.selectBallLimit(Index);
        });
    }
}

void FBallsGameplay::SwitchBackend(BackendKind Kind) {
    if (!Host) {
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
    Host->post([Kind](GameSession& Session) { Session.requestBackend(Kind); });
}

void FBallsGameplay::post(std::function<void(GameSession&)> Command) {
    if (Host) {
        Host->post(std::move(Command));
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
    if (Host) {
        Host->post([](GameSession& Session) { Session.restart(); });
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
