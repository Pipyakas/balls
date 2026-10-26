#include "BallsApp.h"

#include "BallsBoardView.h"
#include "BallsGameplay.h"
#include "BallsLayout.h"
#include "BallsText.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderTimer.h"
#include "UI/SBallsGameplayHud.h"
#include "UI/SBallsMenus.h"
#include "UI/SBallsRoot.h"

namespace {

constexpr int32 RootZOrder = 10;

std::optional<BackendKind> BackendFromCommandLine() {
    FString Name;
    if (!FParse::Value(FCommandLine::Get(), TEXT("BallsBackend="), Name)) {
        return std::nullopt;
    }
    for (const BackendKind Kind : allBackendKinds) {
        if (Name.Equals(BallsText::FromUtf8(backendKindName(Kind)), ESearchCase::IgnoreCase) && isBackendAvailable(Kind)) {
            return Kind;
        }
    }
    return std::nullopt;
}

}

FBallsApp::FBallsApp(UWorld* InWorld)
    : World(InWorld), PreferredBackend(BackendFromCommandLine().value_or(fastestAvailableCpuBackend())) {
    BoardView = MakeUnique<FBallsBoardView>(World);
    Root = SNew(SBallsRoot).OnKey_Lambda([this](const FKey& Key) { return HandleKey(Key); });
    if (UGameViewportClient* Viewport = World->GetGameViewport()) {
        Viewport->AddViewportWidgetContent(Root.ToSharedRef(), RootZOrder);
    }
    if (APlayerController* Player = World->GetFirstPlayerController()) {
        FInputModeUIOnly InputMode;
        InputMode.SetWidgetToFocus(Root);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Player->SetInputMode(InputMode);
        Player->SetShowMouseCursor(true);
    }
    ShowMainMenu();

    // -BallsPlay=normal:DA_FirstLevel opens a level directly, like the raylib build's --play.
    FString Play;
    FString ModeName;
    FString LevelName;
    if (FParse::Value(FCommandLine::Get(), TEXT("BallsPlay="), Play) && Play.Split(TEXT(":"), &ModeName, &LevelName)) {
        const GameMode Mode = ModeName.Equals(TEXT("performance"), ESearchCase::IgnoreCase) ? GameMode::Performance
                                                                                             : GameMode::Normal;
        for (const LevelFiles& Files : findLevels(LevelDirectory(Mode))) {
            if (LevelName.Equals(BallsText::FromUtf8(Files.name), ESearchCase::IgnoreCase)) {
                StartGame(Mode, Files);
                break;
            }
        }
    }
}

FBallsApp::~FBallsApp() {
    if (UGameViewportClient* Viewport = World->GetGameViewport(); Viewport && Root) {
        Viewport->RemoveViewportWidgetContent(Root.ToSharedRef());
    }
    Gameplay.Reset();
    BoardView.Reset();
}

std::filesystem::path FBallsApp::LevelDirectory(GameMode Mode) const {
    std::filesystem::path Assets(BALLS_ASSET_DIR);
    if (!std::filesystem::exists(Assets / "levels")) {
        Assets = std::filesystem::path(*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("assets")));
    }
    return Assets / "levels" / (Mode == GameMode::Normal ? "normal" : "performance");
}

void FBallsApp::SetScreen(EScreen Screen, TSharedRef<SWidget> Content) {
    CurrentScreen = Screen;
    if (Screen != EScreen::Gameplay) {
        Gameplay.Reset();
        BoardView->Hide();
    }
    Root->SetScreen(Content);
}

void FBallsApp::ShowMainMenu() {
    SetScreen(EScreen::MainMenu, SNew(SBallsMainMenu).App(this));
}

void FBallsApp::ShowLevelSelect(GameMode Mode) {
    LevelSelectMode = Mode;
    SetScreen(EScreen::LevelSelect, SNew(SBallsLevelSelect).App(this).Mode(Mode).Levels(findLevels(LevelDirectory(Mode))));
}

void FBallsApp::StartGame(GameMode Mode, const LevelFiles& Files) {
    // Build the new game before swapping so the old one stays alive while its widgets are replaced.
    TSharedPtr<FBallsGameplay> Next = MakeShared<FBallsGameplay>(*this, Mode, Files);
    SetScreen(EScreen::Gameplay, SNew(SBallsGameplayHud).App(this).Gameplay(Next));
    Gameplay = MoveTemp(Next);
}

void FBallsApp::Quit() {
    UKismetSystemLibrary::QuitGame(World, World->GetFirstPlayerController(), EQuitPreference::Quit, false);
}

bool FBallsApp::HandleKey(const FKey& Key) {
    switch (CurrentScreen) {
    case EScreen::Gameplay:
        return Gameplay && Gameplay->HandleKey(Key);
    case EScreen::LevelSelect:
        if (Key == EKeys::Escape) {
            ShowMainMenu();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void FBallsApp::RecordFrame(float DeltaSeconds) {
    FBallsFrameSample Sample;
    Sample.FrameMs = FApp::GetDeltaTime() * 1000.0;
    Sample.GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
    Sample.RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
    Sample.GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
    FrameTimings.Record(Sample);
}

void FBallsApp::Tick(float DeltaSeconds) {
    RecordFrame(DeltaSeconds);
    if (!Gameplay) {
        return;
    }
    // Hold a reference: the tick can open the next level, which replaces Gameplay.
    const TSharedPtr<FBallsGameplay> Current = Gameplay;
    Current->Tick(DeltaSeconds, FApp::GetDeltaTime());
    GameSession* Session = Current->GetSession();
    const FGeometry& Geometry = Root->GetCachedGeometry();
    const FVector2D ViewportSize = Geometry.GetLocalSize();
    if (!Session || Current != Gameplay || ViewportSize.X <= 1.0 || ViewportSize.Y <= 1.0) {
        return;
    }
    const float DpiScale = Geometry.GetAccumulatedLayoutTransform().GetScale();
    Simulation& Sim = Session->simulation();
    const FBallsGameplayLayout Layout = ComputeGameplayLayout(ViewportSize, Sim.board().columns(), Sim.board().rows());
    BoardView->Update(Sim, Layout, ViewportSize, DpiScale);
    BoardView->SubmitBalls(Sim, Layout, DpiScale);
}
