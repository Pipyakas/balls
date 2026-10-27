#include "BallsApp.h"

#include "BallsBoardView.h"
#include "BallsGameplay.h"
#include "BallsLayout.h"
#include "BallsMapEditor.h"
#include "BallsText.h"
#include "DynamicRHI.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "RenderTimer.h"
#include "UI/SBallsGameplayHud.h"
#include "UI/SBallsMapEditor.h"
#include "UI/SBallsMenus.h"
#include "UI/SBallsRoot.h"

namespace {

constexpr int32 RootZOrder = 10;

#if PLATFORM_ANDROID
// Android keeps non-UFS files inside the APK, and balls_core reads levels with std::filesystem.
// Copy the packaged assets once to the writable saved directory before any level lookup.
void StageAndroidLevels() {
    IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
    // Non-UFS project directories are staged under Content (ProjectPackagingSettings paths are
    // Content-relative), so the packaged levels arrive in ProjectContentDir()/assets.
    const FString Source = FPaths::ProjectContentDir() / TEXT("assets");
    const FString Destination = FPaths::ProjectSavedDir() / TEXT("assets");
    const bool bSourceExists = File.DirectoryExists(*Source);
    UE_LOG(LogTemp, Verbose, TEXT("BallsAndroid: project=%s content=%s saved=%s persistent=%s src=%s exists=%d"),
           *FPaths::ProjectDir(), *FPaths::ProjectContentDir(), *FPaths::ProjectSavedDir(),
           *FPaths::ProjectPersistentDownloadDir(), *Source, bSourceExists);
    if (bSourceExists) {
        const bool bCopied = File.CopyDirectoryTree(*Destination, *Source, true);
        UE_LOG(LogTemp, Verbose, TEXT("BallsAndroid: copy %s -> %s = %d, dest exists=%d"), *Source, *Destination,
               bCopied, File.DirectoryExists(*Destination));
    }
}
#endif

// Desktop quality preset. Lumen, Nanite and virtual shadow maps need SM6 (the fork currently targets
// PCD3D_SM5 and has a broken DXC path), but TSR and dynamic resolution are available on SM5.
// See data/docs/ue5-high-end-features.md.
void ApplyHighEndPreset(UWorld* World) {
    const TCHAR* Commands[] = {
        TEXT("r.AntiAliasingMethod 4"),
        TEXT("r.DynamicRes.OperationMode 2"),
        TEXT("r.DynamicRes.MinScreenPercentage 50"),
        TEXT("r.DynamicRes.MaxScreenPercentage 100"),
        TEXT("r.DynamicRes.FrameTimeBudget 16.67"),
    };
    for (const TCHAR* Command : Commands) {
        GEngine->Exec(World, Command);
    }
    const auto Read = [](const TCHAR* Name) {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        return Variable ? Variable->GetInt() : -1;
    };
    UE_LOG(LogTemp, Display, TEXT("BallsHighEnd: anti-aliasing=%d dynamic-res=%d min=%d max=%d"),
           Read(TEXT("r.AntiAliasingMethod")), Read(TEXT("r.DynamicRes.OperationMode")),
           Read(TEXT("r.DynamicRes.MinScreenPercentage")), Read(TEXT("r.DynamicRes.MaxScreenPercentage")));
}

#if PLATFORM_WINDOWS && defined(BALLS_PLUGIN_DIR)
// The core loads its physics backends (CPU, CUDA) as DLLs by file name. Packaged builds stage them beside the
// executable; the editor and development builds find them in the core's build output.
void AddPhysicsPluginDirectory() {
    static const bool bAdded = [] {
        FPlatformProcess::PushDllDirectory(*FString(UTF8_TO_TCHAR(BALLS_PLUGIN_DIR)));
        return true;
    }();
    (void)bAdded;
}
#endif

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

// The Settings page values the core does not own (backend, VSync) live in the per-user GameUserSettings.ini.
const TCHAR* PreferencesSection = TEXT("Balls");

std::filesystem::path SettingsFile() {
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Balls/settings.txt");
    return std::filesystem::path(TCHAR_TO_UTF8(*Path));
}

std::optional<BackendKind> SavedBackend() {
    FString Name;
    if (!GConfig || !GConfig->GetString(PreferencesSection, TEXT("Backend"), Name, GGameUserSettingsIni)) {
        return std::nullopt;
    }
    for (const BackendKind Kind : allBackendKinds) {
        if (Name.Equals(BallsText::FromUtf8(backendKindName(Kind)), ESearchCase::IgnoreCase) && isBackendAvailable(Kind)) {
            return Kind;
        }
    }
    return std::nullopt;
}

bool SavedVSync() {
    bool bEnabled = false;
    if (GConfig) {
        GConfig->GetBool(PreferencesSection, TEXT("VSync"), bEnabled, GGameUserSettingsIni);
    }
    return bEnabled;
}

BackendKind InitialBackend() {
#if PLATFORM_WINDOWS && defined(BALLS_PLUGIN_DIR)
    AddPhysicsPluginDirectory();
#endif
    if (const std::optional<BackendKind> Requested = BackendFromCommandLine()) {
        return *Requested;
    }
    return SavedBackend().value_or(fastestAvailableCpuBackend());
}

}

FBallsApp::FBallsApp(UWorld* InWorld)
    : World(InWorld), PreferredBackend(InitialBackend()), CurrentSettings(loadSettings(SettingsFile())),
      bVSync(SavedVSync()) {
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
        // The engine's default on-screen thumbsticks have no use here and swallow touches.
        Player->ActivateTouchInterface(nullptr);
    }
    // The fork renders its "multiple directional lights" warning over the board; this game never needs
    // engine on-screen messages.
    if (GEngine) {
        GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
        GEngine->Exec(World, bVSync ? TEXT("r.VSync 1") : TEXT("r.VSync 0"));
        // The preset is desktop-only: Vulkan ES3.1 on Android has neither TSR nor usable DRS.
#if !PLATFORM_ANDROID
        if (FParse::Param(FCommandLine::Get(), TEXT("BallsHighEnd"))) {
            ApplyHighEndPreset(World);
        }
#endif
    }
#if PLATFORM_ANDROID
    StageAndroidLevels();
#endif
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
    if (FParse::Param(FCommandLine::Get(), TEXT("BallsTestShot")) && Gameplay) {
        Gameplay->ChooseLauncher(0, 0);
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("BallsEditor"))) {
        ShowMapEditor(GameMode::Normal, std::nullopt);
    }
    // -BallsScreen=<settings|levels-normal|levels-performance|pause|pause-settings> opens a screen directly, for
    // screenshots and conformance checks (the pause ones need -BallsPlay too).
    FString Screen;
    if (FParse::Value(FCommandLine::Get(), TEXT("BallsScreen="), Screen)) {
        if (Screen == TEXT("settings")) {
            ShowSettings();
        } else if (Screen == TEXT("levels-normal")) {
            ShowLevelSelect(GameMode::Normal);
        } else if (Screen == TEXT("levels-performance")) {
            ShowLevelSelect(GameMode::Performance);
        } else if (Gameplay && (Screen == TEXT("pause") || Screen == TEXT("pause-settings"))) {
            Gameplay->SetPaused(true);
            Gameplay->SetSettingsOpen(Screen == TEXT("pause-settings"));
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
#if PLATFORM_ANDROID
    if (!std::filesystem::exists(Assets / "levels")) {
        Assets = std::filesystem::path(*FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("assets")));
    }
    // ProjectSavedDir() stays virtual on Android, so std::filesystem cannot walk it. The app's external
    // files directory is a native path; the AFS deploy may nest the staged build under the project name.
    if (!std::filesystem::exists(Assets / "levels")) {
        const FString Persistent = FPaths::ProjectPersistentDownloadDir();
        const std::filesystem::path PersistentPath(TCHAR_TO_UTF8(*Persistent));
        const std::filesystem::path ProjectName(TCHAR_TO_UTF8(FApp::GetProjectName()));
        const std::filesystem::path Candidates[] = {
            PersistentPath / "Saved" / "assets",
            PersistentPath / ProjectName / "Saved" / "assets",
            PersistentPath / "UnrealGame" / ProjectName / "Saved" / "assets",
            PersistentPath / "UnrealGame" / ProjectName / ProjectName / "Saved" / "assets",
        };
        for (const std::filesystem::path& Candidate : Candidates) {
            if (std::filesystem::exists(Candidate / "levels")) {
                Assets = Candidate;
                break;
            }
        }
    }
#endif
    return Assets / "levels" / (Mode == GameMode::Normal ? "normal" : "performance");
}

void FBallsApp::SetScreen(EScreen Screen, TSharedRef<SWidget> Content) {
    CurrentScreen = Screen;
    if (Screen != EScreen::MapEditor) {
        MapEditorState.Reset();
    }
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
    const std::filesystem::path Directory = LevelDirectory(Mode);
    const std::vector<LevelFiles> Levels = findLevels(Directory);
    UE_LOG(LogTemp, Verbose, TEXT("BallsLevels: %s -> %d"), ANSI_TO_TCHAR(Directory.string().c_str()),
           static_cast<int32>(Levels.size()));
    SetScreen(EScreen::LevelSelect, SNew(SBallsLevelSelect).App(this).Mode(Mode).Levels(Levels));
}

void FBallsApp::ShowSettings() {
    SetScreen(EScreen::Settings, SNew(SBallsSettingsPage).App(this));
}

void FBallsApp::SetPreferredBackend(BackendKind Kind) {
    PreferredBackend = Kind;
    if (GConfig) {
        GConfig->SetString(PreferencesSection, TEXT("Backend"), *BallsText::FromUtf8(backendKindName(Kind)),
                           GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
}

void FBallsApp::ChooseBackend(BackendKind Kind) {
    if (Gameplay && Gameplay->GetSession()) {
        Gameplay->SwitchBackend(Kind);
    } else if (isBackendAvailable(Kind)) {
        SetPreferredBackend(Kind);
    }
}

void FBallsApp::UpdateSettings(const Settings& Next) {
    CurrentSettings = Next;
    if (const Result<void> Saved = saveSettings(SettingsFile(), CurrentSettings); !Saved) {
        UE_LOG(LogTemp, Warning, TEXT("BallsSettings: %s"), *BallsText::FromUtf8(Saved.error()));
    }
    if (Gameplay) {
        Gameplay->ApplySettings();
    }
}

std::optional<FBallsApp::FMachineLimit> FBallsApp::GetMachineLimit(BackendKind Kind) const {
    FString Value;
    if (!GConfig || !GConfig->GetString(PreferencesSection, *(TEXT("Limit.") + BallsText::FromUtf8(backendKindName(Kind))),
                                        Value, GGameUserSettingsIni)) {
        return std::nullopt;
    }
    FString Target;
    FString Balls;
    if (!Value.Split(TEXT(" "), &Target, &Balls)) {
        return std::nullopt;
    }
    return FMachineLimit{FCString::Atod(*Target), static_cast<BallCount>(FCString::Strtoui64(*Balls, nullptr, 10))};
}

void FBallsApp::RememberMachineLimit(BackendKind Kind, double TargetFps, BallCount Balls) {
    if (!GConfig) {
        return;
    }
    GConfig->SetString(PreferencesSection, *(TEXT("Limit.") + BallsText::FromUtf8(backendKindName(Kind))),
                       *FString::Printf(TEXT("%g %llu"), TargetFps, static_cast<unsigned long long>(Balls)),
                       GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

void FBallsApp::SetVSyncEnabled(bool bEnabled) {
    bVSync = bEnabled;
    if (GConfig) {
        GConfig->SetBool(PreferencesSection, TEXT("VSync"), bVSync, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    if (GEngine) {
        GEngine->Exec(World, bVSync ? TEXT("r.VSync 1") : TEXT("r.VSync 0"));
    }
}

void FBallsApp::ShowMapEditor(GameMode Mode, std::optional<LevelFiles> Files) {
    TSharedPtr<FBallsMapEditor> Next = MakeShared<FBallsMapEditor>(*this, Mode, std::move(Files));
    MapEditorState = Next;
    SetScreen(EScreen::MapEditor, SNew(SBallsMapEditor).App(this).Editor(Next));
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
    case EScreen::MapEditor:
        return MapEditorState && MapEditorState->HandleKey(Key);
    case EScreen::LevelSelect:
    case EScreen::Settings:
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
    LatestSample = Sample;
    FrameTimings.Record(Sample);
}

void FBallsApp::Tick(float DeltaSeconds) {
    RecordFrame(DeltaSeconds);
    if (CurrentScreen == EScreen::MapEditor) {
        if (MapEditorState) {
            MapEditorState->Tick(DeltaSeconds);
        }
        return;
    }
    if (!Gameplay) {
        return;
    }
    // Hold a reference: the tick can open the next level, which replaces Gameplay.
    const TSharedPtr<FBallsGameplay> Current = Gameplay;
    // The probe sizes its jumps from the slower of the CPU and GPU work per frame, like the SDL front end.
    const double WorkSeconds = FMath::Max3(LatestSample.GameMs, LatestSample.RenderMs, LatestSample.GpuMs) / 1000.0;
    Current->Tick(DeltaSeconds, FApp::GetDeltaTime(), WorkSeconds);
    GameSession* Session = Current->GetSession();
    const FGeometry& Geometry = Root->GetCachedGeometry();
    const FVector2D ViewportSize = Geometry.GetLocalSize();
    if (!Session || Current != Gameplay || ViewportSize.X <= 1.0 || ViewportSize.Y <= 1.0) {
        return;
    }
    const float DpiScale = Geometry.GetAccumulatedLayoutTransform().GetScale();
    Simulation& Sim = Session->simulation();
    const FBallsGameplayLayout Layout =
        ComputeGameplayLayout(ViewportSize, Sim.board().columns(), Sim.board().rows(), DpiScale,
                              Current->GetMode() == GameMode::Normal);
    BoardView->Update(Sim, Layout, ViewportSize, DpiScale);
    BoardView->SubmitBalls(Sim, Layout, DpiScale);
}
