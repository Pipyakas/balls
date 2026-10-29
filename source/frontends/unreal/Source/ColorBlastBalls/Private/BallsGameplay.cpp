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
    Host->post([](GameSession& Session) { Session.useCapacityBallLimit(); });
}

// Loads the files as they are now: a parse failure drops to the load-error screen instead of keeping the old game.
void FBallsGameplay::ReloadFromDisk() {
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
}

void FBallsGameplay::ShowToast(FString Text, bool bIsError) { App.ShowToast(MoveTemp(Text), bIsError); }

const FBallsToast& FBallsGameplay::GetToast() const { return App.GetToast(); }

bool FBallsGameplay::IsWon() const { return View && Mode == GameMode::Normal && View->state == SessionState::Won; }

void FBallsGameplay::Tick(float DeltaSeconds, double WorkSeconds) {
    if (!Host) {
        // Load-error retry is owned by the flow; nothing to do here.
        return;
    }
    // On the game thread this steps the session; on the physics thread it only hands over the frame's timings and
    // takes the newest view. A backend switch builds in the background either way.
    Host->tick({App.GetScreenFlow().isPaused(), WorkSeconds, VisibilitySlotCells}, DeltaSeconds);
    View = Host->view();
    for (const SessionEvent& Event : Host->takeEvents()) {
        if (Event.kind == SessionEventKind::BallCollisionsChanged) {
            ShowToast(BallsText::FromUtf8(ballCollisionsMessage(Event.collisionsChange)), false);
        } else if (Event.kind == SessionEventKind::BackendSwitched) {
            // A switch inside a game lasts for that game only; the Settings page owns the remembered default.
            ShowToast(BallsText::FromUtf8(physicsOnMessage(displayedPicks(Event.backend))), false);
            ApplyDefaultBallLimit();
        } else {
            ShowToast(BallsText::FromUtf8(backendFailedMessage(Event.backend)), true);
        }
    }
}

// The design gives gameplay no keyboard shortcuts: every action is a visible control. Only the load-error screen keeps
// Esc as Back, like the menus.
bool FBallsGameplay::HandleKey(const FKey& Key) {
    return false;
}

void FBallsGameplay::ApplySettings() {
    if (Host) {
        // What it did, with the game's toast, comes back as an event at the next tick.
        Host->setBallCollisions(App.GetSettings().ballCollisions);
    }
}

void FBallsGameplay::ChooseLauncher(int32 Column, int32 Row) {
    if (Host && !App.GetScreenFlow().isPaused()) {
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
            Session.cancelFill();
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

void FBallsGameplay::Restart() {
    if (Host) {
        Host->post([](GameSession& Session) { Session.restart(); });
    }
}
