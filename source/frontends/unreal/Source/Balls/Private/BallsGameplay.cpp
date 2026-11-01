#include "BallsGameplay.h"

#include "BallsApp.h"
#include "BallsText.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <algorithm>
#include <chrono>

namespace {

SessionThreading ThreadingFromCommandLine() {
    int32 Value = 1;
    FParse::Value(FCommandLine::Get(), TEXT("BallsPhysicsThread="), Value);
    return Value != 0 ? SessionThreading::PhysicsThread : SessionThreading::UiThread;
}

}

FBallsGameplay::FBallsGameplay(FBallsApp& InApp, GameMode InMode, LevelFiles InFiles)
    : App(InApp), Mode(InMode), Files(MoveTemp(InFiles)) {
    LoadSession();
}

void FBallsGameplay::LoadSession() {
    LoadError.Reset();
    Progress = std::make_shared<LoadProgress>();
    Host = std::make_unique<SessionHost>(Mode, Files, App.GetPreferredBackend(), ThreadingFromCommandLine(), Progress);
    AdoptLoadOutcome();
    if (!Host) {
        return;
    }
    View = Host->view();
    ApplySettings();
}

void FBallsGameplay::AdoptLoadOutcome() {
    const std::optional<LevelLoadOutcome> Outcome = Host->takeLoadOutcome();
    if (!Outcome) {
        return;
    }
    if (!Outcome->loaded) {
        Host.reset();
        View.reset();
        LoadError = BallsText::FromUtf8(Outcome->message);
        App.GetScreenFlow().levelFailed(Outcome->message);
        return;
    }
    App.GetScreenFlow().levelLoaded(Outcome->message);
}

void FBallsGameplay::RefreshAutoBallLimit() {
    if (!Host) {
        return;
    }
    Host->post([](GameSession& Session) {
        if (Session.isUsingMachineBallLimit()) {
            Session.useCapacityBallLimit();
        }
    });
}

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
    }
}

void FBallsGameplay::ShowToast(FString Text, bool bIsError) { App.ShowToast(MoveTemp(Text), bIsError); }

const FBallsToast& FBallsGameplay::GetToast() const { return App.GetToast(); }

bool FBallsGameplay::IsWon() const { return View && Mode == GameMode::Normal && View->state == SessionState::Won; }

void FBallsGameplay::Tick(float DeltaSeconds, double WorkSeconds) {
    if (Host) {
        AdoptLoadOutcome();
    }
    if (!Host) {

        return;
    }

    Host->tick({App.GetScreenFlow().isPaused(), WorkSeconds, VisibilitySlotCells}, DeltaSeconds);
    View = Host->view();
    for (const SessionEvent& Event : Host->takeEvents()) {
        if (Event.kind == SessionEventKind::BallCollisionsChanged) {
            ShowToast(BallsText::FromUtf8(ballCollisionsMessage(Event.collisionsChange)), false);
        } else if (Event.kind == SessionEventKind::BackendSwitched) {

            ShowToast(BallsText::FromUtf8(physicsOnMessage(displayedPicks(Event.backend))), false);
            RefreshAutoBallLimit();
        } else {
            ShowToast(BallsText::FromUtf8(backendFailedMessage(Event.backend)), true);
        }
    }
}

bool FBallsGameplay::HandleKey(const FKey& Key) {
    return false;
}

void FBallsGameplay::ApplySettings() {
    if (Host) {

        Host->setBallCollisions(ballCollisionsOffered() && App.GetSettings().ballCollisions);
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

void FBallsGameplay::SelectBallLimit(int32 Index) {
    if (Host) {
        Host->post([Index](GameSession& Session) { Session.selectBallLimit(Index); });
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
