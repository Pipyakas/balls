#include "BallsApp.h"

#include "BallsBoardView.h"
#include "BallsControlServer.h"
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
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "Widgets/SWindow.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#if PLATFORM_DESKTOP
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#endif
#include "RHI.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "UI/SBallsBenchmark.h"
#include "UI/SBallsGameplayHud.h"
#include "UI/SBallsMapEditor.h"
#include "UI/SBallsMenus.h"
#include "Widgets/SOverlay.h"
#include "UI/SBallsRoot.h"

#include <format>
#include <fstream>
#include <sstream>

namespace {

constexpr int32 RootZOrder = 10;
// "First launch": the Main menu opens 1.5 s after the last backend finished, so the winner stays readable.
constexpr double BenchmarkAdvanceSeconds = 1.5;
constexpr float MaximumFrameSeconds = 0.1f;

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
    // A kind name or device picks ("AVX2@cpu+CUDA@gpu0"), as the game's backend launch option takes them.
    return parseBackendKind(TCHAR_TO_UTF8(*Name));
}

// The Settings page values the core does not own (backend, VSync) live in the per-user GameUserSettings.ini.
const TCHAR* PreferencesSection = TEXT("Balls");

std::filesystem::path SettingsFile() {
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Balls/settings.txt");
    return std::filesystem::path(TCHAR_TO_UTF8(*Path));
}

std::filesystem::path ProfileFolder() { return SettingsFile().parent_path(); }

// The Unreal port owns this one: the design has no graphics level, but its post chain costs more than the scene.
EBallsGraphics SavedGraphics() {
    // The command line wins: screenshots and measurements pin the level they want to capture.
    FString Requested;
    if (FParse::Value(FCommandLine::Get(), TEXT("BallsGraphics="), Requested)) {
        if (const std::optional<EBallsGraphics> Parsed = BallsGraphics::Parse(Requested)) {
            return *Parsed;
        }
    }
    FString Name;
    if (GConfig && GConfig->GetString(PreferencesSection, TEXT("Graphics"), Name, GGameUserSettingsIni)) {
        if (const std::optional<EBallsGraphics> Parsed = BallsGraphics::Parse(Name)) {
            return *Parsed;
        }
    }
    return EBallsGraphics::Minimum;
}

bool SavedVSync() {
    bool bEnabled = false;
    if (GConfig) {
        GConfig->GetBool(PreferencesSection, TEXT("VSync"), bEnabled, GGameUserSettingsIni);
    }
    return bEnabled;
}

// The command line wins, then the Settings default (this machine's own benchmark winner or the player's pick), then
// the scoring machine's winner, then the fastest CPU backend, skipping any that cannot load on this machine.
BackendKind StartupBackend(const std::string& SettingsDefault, const std::filesystem::path& AssetRoot) {
#if PLATFORM_WINDOWS && defined(BALLS_PLUGIN_DIR)
    AddPhysicsPluginDirectory();
#endif
    return chooseStartupBackend(BackendFromCommandLine(), SettingsDefault, AssetRoot / targetBenchmarkFileName);
}

}

FBallsApp::FBallsApp(UWorld* InWorld)
    : World(InWorld), CurrentSettings(loadSettings(SettingsFile())), bVSync(SavedVSync()),
      GraphicsQuality(SavedGraphics()), Profile(ProfileFolder()) {
    Profile.load();
#if PLATFORM_ANDROID
    StageAndroidLevels();
#endif
    PreferredBackend = StartupBackend(CurrentSettings.defaultBackend, AssetRoot());
    // Loading a plugin (and the first CUDA or Vulkan device) takes hundreds of milliseconds to seconds, and the
    // gameplay HUD asks about every backend when a level opens. Do it once here, while the Main menu is still
    // coming up, instead of freezing the first level start.
    for (const BackendKind Kind : allBackendKinds) {
        isBackendAvailable(Kind);
    }
    BoardView = MakeUnique<FBallsBoardView>(World);
    Root = SNew(SBallsRoot)
               .OnKey_Lambda([this](const FKey& Key) { return HandleKey(Key); })
               .OnFilesDropped_Lambda([this](const TArray<FString>& Paths) { ImportLevels(Paths); });
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
        // The port starts at Minimum quality; the player raises it in the Performance gadget or the Settings page.
        BoardView->ApplyGraphics(GraphicsQuality);
        BallsGraphics::ApplyToWorld(World, GraphicsQuality);
        // The preset is desktop-only: Vulkan ES3.1 on Android has neither TSR nor usable DRS.
#if !PLATFORM_ANDROID
        if (FParse::Param(FCommandLine::Get(), TEXT("BallsHighEnd"))) {
            ApplyHighEndPreset(World);
        }
#endif
    }
    ApplyOrientation();
    Flow = std::make_unique<ScreenFlow>(LevelDirectory(GameMode::Normal), LevelDirectory(GameMode::Performance));
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
                Flow->startGame(Mode, Files);
                ProcessFlowRequests();
                ApplyFlowScreen();
                break;
            }
        }
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("BallsTestShot")) && Gameplay) {
        Gameplay->ChooseLauncher(0, 0);
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("BallsEditor"))) {
        Flow->openEditor(GameMode::Normal, std::nullopt);
        ProcessFlowRequests();
        ApplyFlowScreen();
    }
    // -BallsScreen=<settings|levels-normal|levels-performance|pause|pause-settings> opens a screen directly, for
    // screenshots and conformance checks (the pause ones need -BallsPlay too).
    FString Screen;
    if (FParse::Value(FCommandLine::Get(), TEXT("BallsScreen="), Screen)) {
        if (Screen == TEXT("settings")) {
            Flow->showSettings();
            ProcessFlowRequests();
            ApplyFlowScreen();
        } else if (Screen == TEXT("levels-normal")) {
            Flow->showLevelSelect(GameMode::Normal);
            ProcessFlowRequests();
            ApplyFlowScreen();
        } else if (Screen == TEXT("levels-performance")) {
            Flow->showLevelSelect(GameMode::Performance);
            ProcessFlowRequests();
            ApplyFlowScreen();
        } else if (Gameplay && (Screen == TEXT("pause") || Screen == TEXT("pause-settings"))) {
            if (!Flow->isPaused()) {
                Flow->activate(FlowAction::TogglePause);
            }
            if (Screen == TEXT("pause-settings")) {
                Flow->activate(FlowAction::PauseSettings);
            }
            ProcessFlowRequests();
            ApplyFlowScreen();
        }
    }
    // "Default backend and machine benchmark": the game opens straight on the Main menu and never measures by
    // itself; Settings > Run machine benchmark measures this machine on demand.
    Benchmark.LoadRecord();

    // -BallsControlPort=<port> serves the control API; nothing listens unless a port is given, like SDL's
    // --control-port.
    int32 ControlPort = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("BallsControlPort="), ControlPort) && ControlPort > 0) {
        Control = MakeShared<FBallsControlServer>(*this);
        if (!Control->Start(ControlPort)) {
            UE_LOG(LogTemp, Warning, TEXT("BallsControl: could not listen on port %d"), ControlPort);
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

std::filesystem::path FBallsApp::AssetRoot() const {
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
    return Assets;
}

std::filesystem::path FBallsApp::LevelDirectory(GameMode Mode) const {
    return AssetRoot() / "levels" / (Mode == GameMode::Normal ? "normal" : "performance");
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
    if (Screen == EScreen::Gameplay) {
        Root->SetScreen(Content);
        return;
    }
    Root->SetScreen(SNew(SOverlay)
                    + SOverlay::Slot()[Content]
                    + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(16.0f, 0.0f, 0.0f, 6.0f)
                    [
                        BallsWidgets::FrameTimingsStrip(Frames)
                    ]);
}

void FBallsApp::ProcessFlowRequests() {
    std::optional<FlowRequest> Request;
    while ((Request = Flow->takeRequest()).has_value()) {
        const FlowRequest& Req = *Request;
        switch (Req.kind) {
        case FlowRequestKind::StartGame:
        case FlowRequestKind::ReloadGame: {
            TSharedPtr<FBallsGameplay> Next = MakeShared<FBallsGameplay>(*this, Req.mode, *Req.files);
            Gameplay = Next;
            // Next level and reloads stay on the gameplay screen, so its widgets must be rebuilt for the new game.
            bFlowScreenApplied = false;
            if (Next->GetLoadError().IsEmpty()) {
                Flow->levelLoaded();
            } else {
                Flow->levelFailed(TCHAR_TO_UTF8(*Next->GetLoadError()));
            }
            break;
        }
        case FlowRequestKind::RestartGame: {
            if (Gameplay) {
                Gameplay->Restart();
            }
            break;
        }
        case FlowRequestKind::EndGame: {
            Gameplay.Reset();
            if (BoardView) {
                BoardView->Hide();
            }
            break;
        }
        case FlowRequestKind::OpenEditor: {
            MapEditorState = MakeShared<FBallsMapEditor>(*this, Req.mode, Req.files);
            bFlowScreenApplied = false;
            break;
        }
        case FlowRequestKind::StartBenchmark: {
            FString Error;
            for (const BackendKind Kind : allBackendKinds) {
                isBackendAvailable(Kind);
            }
            if (!Benchmark.Start(LevelDirectory(GameMode::Performance), Error)) {
                ShowToast(FString::Printf(TEXT("Benchmark: %s"), *Error), true);
                Flow->showMainMenu();
            }
            break;
        }
        case FlowRequestKind::SkipBenchmark: {
            Benchmark.Skip();
            FinishBenchmark(false);
            break;
        }
        case FlowRequestKind::PickLevelFiles: {
            OpenLevelImportDialog();
            break;
        }
        case FlowRequestKind::OpenFolder: {
            OpenLevelFolder(Req.mode);
            break;
        }
        case FlowRequestKind::Quit: {
            Quit();
            break;
        }
        }
    }
}

void FBallsApp::ApplyFlowScreen() {
    const FlowScreen Screen = Flow->screen();
    const GameMode Mode = Flow->mode();
    if (bFlowScreenApplied && Screen == LastAppliedFlowScreen && Mode == LastAppliedFlowMode) {
        return;
    }
    LastAppliedFlowScreen = Screen;
    LastAppliedFlowMode = Mode;
    bFlowScreenApplied = true;

    switch (Screen) {
    case FlowScreen::MainMenu:
        CurrentScreen = EScreen::MainMenu;
        SetScreen(EScreen::MainMenu, SNew(SBallsMainMenu).App(this));
        break;
    case FlowScreen::LevelSelect:
        LevelSelectMode = Mode;
        {
            TSharedRef<SBallsLevelSelect> S = SNew(SBallsLevelSelect).App(this).Mode(Mode);
            LevelSelect = S;
            SetScreen(EScreen::LevelSelect, S);
        }
        break;
    case FlowScreen::Gameplay:
    case FlowScreen::LoadError:
        if (!Gameplay) {
            // Nothing to show yet; leave the previous screen.
            break;
        }
        SetScreen(EScreen::Gameplay, SNew(SBallsGameplayHud).App(this).Gameplay(Gameplay));
        break;
    case FlowScreen::MapEditor:
        if (!MapEditorState) {
            MapEditorState = MakeShared<FBallsMapEditor>(*this, Mode, Flow->level());
        }
        SetScreen(EScreen::MapEditor, SNew(SBallsMapEditor).App(this).Editor(MapEditorState));
        break;
    case FlowScreen::Settings:
        SetScreen(EScreen::Settings, SNew(SBallsSettingsPage).App(this));
        break;
    case FlowScreen::MachineBenchmark:
        SetScreen(EScreen::Benchmark, SNew(SBallsBenchmark).App(this));
        break;
    }
}

bool FBallsApp::ActivateFlow(FlowAction Action, int32 Row) {
    const bool Result = Flow->activate(Action, Row);
    ProcessFlowRequests();
    ApplyFlowScreen();
    return Result;
}

void FBallsApp::ShowMainMenu() {
    Flow->showMainMenu();
    ProcessFlowRequests();
    ApplyFlowScreen();
}

void FBallsApp::ShowLevelSelect(GameMode Mode) {
    Flow->showLevelSelect(Mode);
    ProcessFlowRequests();
    ApplyFlowScreen();
}

void FBallsApp::ShowToast(FString Text, bool bIsError) {
    Flow->showToast(TCHAR_TO_UTF8(*Text), bIsError);
}

void FBallsApp::ImportLevels(const TArray<FString>& Paths) {
    std::vector<std::filesystem::path> Picked;
    for (const FString& Path : Paths) {
        Picked.emplace_back(TCHAR_TO_UTF8(*Path));
    }
    Flow->importLevels(Picked);
    ProcessFlowRequests();
    ApplyFlowScreen();
}

void FBallsApp::OpenLevelImportDialog() {
#if PLATFORM_DESKTOP
    IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
    if (!Platform) {
        ShowToast(TEXT("Import failed: no file picker on this platform"), true);
        return;
    }
    TArray<FString> Picked;
    const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    const FString Directory = FString(UTF8_TO_TCHAR(Flow->levelFolder().string().c_str()));
    if (Platform->OpenFileDialog(ParentWindow, TEXT("Import levels"), Directory, FString(), TEXT("Level files (*.txt)|*.txt"),
                                 EFileDialogFlags::Multiple, Picked) &&
        Picked.Num() > 0) {
        ImportLevels(Picked);
    }
#else
    ShowToast(TEXT("Import failed: no file picker on this platform"), true);
#endif
}

void FBallsApp::OpenLevelFolder(GameMode Mode) {
    const std::filesystem::path Directory = LevelDirectory(Mode);
    std::error_code Error;
    std::filesystem::create_directories(Directory, Error);
    FPlatformProcess::ExploreFolder(UTF8_TO_TCHAR(Directory.string().c_str()));
}

void FBallsApp::ShowSettings() {
    Flow->showSettings();
    ProcessFlowRequests();
    ApplyFlowScreen();
}

void FBallsApp::ShowBenchmark() {
    Flow->runMachineBenchmark();
    ProcessFlowRequests();
    ApplyFlowScreen();
}

void FBallsApp::FinishBenchmark(bool bComplete) {
    const std::optional<BenchmarkCandidate> Winner = Benchmark.PickWinner();
    Benchmark.Save(bComplete, Winner);
    if (Winner && isBackendAvailable(Winner->kind)) {
        SetPreferredBackend(Winner->kind);
    }
}

FString FBallsApp::GetBenchmarkWinnerLabel() const {
    const std::optional<BenchmarkCandidate> Winner = Benchmark.PickWinner();
    return Winner ? BallsText::FromUtf8(Winner->label) : FString();
}

void FBallsApp::SetPreferredBackend(BackendKind Kind) {
    PreferredBackend = Kind;
    // The default backend is a core setting, shared with the SDL front end's Settings page; Hybrid is saved as its
    // device picks.
    CurrentSettings.defaultBackend = storedBackendName(Kind);
    if (const Result<void> Saved = saveSettings(SettingsFile(), CurrentSettings); !Saved) {
        UE_LOG(LogTemp, Warning, TEXT("BallsSettings: %s"), *BallsText::FromUtf8(Saved.error()));
    }
}

void FBallsApp::ApplyOrientation() {
    SetLayoutOrientation(CurrentSettings.layout);
#if !PLATFORM_ANDROID
    UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    const TSharedPtr<SWindow> Window = Viewport ? Viewport->GetWindow() : nullptr;
    if (!Window || CurrentSettings.layout == LayoutOrientation::Auto || Window->IsWindowMaximized() ||
        Window->GetWindowMode() != EWindowMode::Windowed) {
        return;
    }
    FDisplayMetrics Metrics;
    FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    const bool bPortrait = CurrentSettings.layout == LayoutOrientation::Portrait;
    const float Height = bPortrait ? static_cast<float>(Metrics.PrimaryDisplayHeight) * 0.9f : 900.0f;
    const float Width = bPortrait ? Height * 9.0f / 16.0f : 1440.0f;
    Window->Resize(FVector2D(Width, Height));
#endif
}

void FBallsApp::UpdateSettings(const Settings& Next) {
    const bool bLayoutChanged = Next.layout != CurrentSettings.layout;
    CurrentSettings = Next;
    if (bLayoutChanged) {
        ApplyOrientation();
    }
    if (const Result<void> Saved = saveSettings(SettingsFile(), CurrentSettings); !Saved) {
        UE_LOG(LogTemp, Warning, TEXT("BallsSettings: %s"), *BallsText::FromUtf8(Saved.error()));
    }
    if (Gameplay) {
        Gameplay->ApplySettings();
    }
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

void FBallsApp::SetGraphicsQuality(EBallsGraphics Quality) {
    GraphicsQuality = Quality;
    if (GConfig) {
        GConfig->SetString(PreferencesSection, TEXT("Graphics"), *BallsGraphics::Name(Quality), GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    BoardView->ApplyGraphics(Quality);
    BallsGraphics::ApplyToWorld(World, Quality);
}

void FBallsApp::ShowMapEditor(GameMode Mode, std::optional<LevelFiles> Files) {
    Flow->openEditor(Mode, std::move(Files));
    ProcessFlowRequests();
    ApplyFlowScreen();
}

void FBallsApp::StartGame(GameMode Mode, const LevelFiles& Files) {
    Flow->startGame(Mode, Files);
    ProcessFlowRequests();
    ApplyFlowScreen();
}

void FBallsApp::Quit() {
    UKismetSystemLibrary::QuitGame(World, World->GetFirstPlayerController(), EQuitPreference::Quit, false);
}

bool FBallsApp::HandleKey(const FKey& Key) {
    if (CurrentScreen == EScreen::MapEditor) {
        return MapEditorState && MapEditorState->HandleKey(Key);
    }
    if (Key != EKeys::Escape) {
        if (CurrentScreen == EScreen::Gameplay) {
            return Gameplay && Gameplay->HandleKey(Key);
        }
        return false;
    }
    // Esc on every other screen goes through the core's flow.
    const bool Result = Flow->escape();
    ProcessFlowRequests();
    ApplyFlowScreen();
    return Result;
}

void FBallsApp::RecordFrame(float DeltaSeconds) {
    FrameSample Sample;
    Sample.frameMs = FApp::GetDeltaTime() * 1000.0;
    Sample.cpuMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
    Sample.renderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
    Sample.gpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
    // Time the engine slept to hold a frame-rate cap (t.MaxFPS): the one wait that is not the game's own work.
    Sample.waitMs = FApp::GetIdleTime() * 1000.0;
    // The limiting thread, or the whole frame minus the cap's sleep when that is longer: the thread times alone would
    // hide a stall (memory paging, a long core step on a worker) that makes the real frame far longer.
    Sample.workMs = FMath::Max(FMath::Max3(Sample.cpuMs, Sample.renderMs, Sample.gpuMs),
                               FMath::Max(0.0, Sample.frameMs - Sample.waitMs));
    Frames.record(Sample);
}

void FBallsApp::Tick(float DeltaSeconds) {
    RecordFrame(DeltaSeconds);
    // The core owns the benchmark's advance: it returns to the Main menu 1.5 s after the result.
    if (Flow->screen() == FlowScreen::MachineBenchmark) {
        if (!Flow->benchmarkSettled() && Benchmark.IsFinished()) {
            FinishBenchmark(true);
            Flow->benchmarkFinished();
        }
        Flow->update(DeltaSeconds);
        ProcessFlowRequests();
        ApplyFlowScreen();
        return;
    }
    // The viewport window may not exist yet in the constructor, so a saved forced orientation resizes it here.
    if (!bOrientationApplied && World && World->GetGameViewport() && World->GetGameViewport()->GetWindow()) {
        bOrientationApplied = true;
        ApplyOrientation();
    }
    Flow->update(DeltaSeconds);
    ProcessFlowRequests();
    ApplyFlowScreen();

    if (CurrentScreen == EScreen::MapEditor) {
        if (MapEditorState) {
            MapEditorState->Tick(DeltaSeconds);
        }
        return;
    }
    if (!Gameplay) {
        return;
    }
    // Report the session state to the flow each tick, so the Win overlay appears in Normal mode.
    if (Gameplay->GetView()) {
        Flow->sessionStateChanged(Gameplay->GetView()->state);
    }
    ProcessFlowRequests();
    ApplyFlowScreen();

    // Hold a reference: the tick can open the next level, which replaces Gameplay.
    const TSharedPtr<FBallsGameplay> Current = Gameplay;
    if (!Current) {
        return;
    }
    // After a hitch the spawn budget and brick animation must not jump by the whole stall: clamp like the SDL front end.
    Current->Tick(FMath::Min(DeltaSeconds, MaximumFrameSeconds), Frames.latest().workMs / 1000.0);
    const SessionView* View = Current->GetView();
    const FGeometry& Geometry = Root->GetCachedGeometry();
    const FVector2D ViewportSize = Geometry.GetLocalSize();
    if (!View || !View->board || Current != Gameplay || ViewportSize.X <= 1.0 || ViewportSize.Y <= 1.0) {
        return;
    }
    const float DpiScale = Geometry.GetAccumulatedLayoutTransform().GetScale();
    const BrickBoard& Board = *View->board;
    const int32 LauncherRows = Current->GetMode() == GameMode::Normal ? View->launchers.visibleRows() : 0;
    const FBallsGameplayLayout Layout =
        ComputeGameplayLayout(ViewportSize, Board.columns(), Board.rows(), DpiScale, LauncherRows);
    BoardView->Update(Board, Layout, ViewportSize, DpiScale);
    if (GameSession* Live = Current->GetLiveSession()) {
        BoardView->SubmitBalls(Live->simulation(), Layout, DpiScale);
    } else {
        // The physics thread copied this frame's balls into the view; it sizes the visibility grid from the slot.
        Current->SetVisibilitySlotCells(FBallsBoardView::VisibilitySlotCells(View->ballRadius, Layout, DpiScale));
        const BallFrame& Balls = View->balls;
        BoardView->SubmitBalls({Balls.packed.data(), Balls.packed.size(), Balls.fieldWidth, Balls.fieldHeight, false,
                                Balls.visibilityGrid},
                               View->ballRadius, Layout, DpiScale);
    }
}

const FBallsToast& FBallsApp::GetToast() const {
    static FBallsToast Cached;
    const FlowToast& T = Flow->toast();
    Cached.Text = BallsText::FromUtf8(T.text);
    Cached.SecondsLeft = T.secondsLeft;
    Cached.bIsError = T.isError;
    return Cached;
}

struct FBallsApp::FControlBridge final : ControlHost {
    FBallsApp& App;

    explicit FControlBridge(FBallsApp& Owner) : App(Owner) {}

    ControlFrontEndState frontEndState() const override {
        const FVector2D WindowSize = App.Root->GetCachedGeometry().GetLocalSize();
        ControlFrontEndState State;
        State.frontEnd = "unreal";
        State.renderer = TCHAR_TO_UTF8(*GRHIAdapterName);
        State.screen = flowScreenName(App.GetScreenFlow().screen());
        State.windowWidth = static_cast<int>(WindowSize.X);
        State.windowHeight = static_cast<int>(WindowSize.Y);
        State.frames = &App.Frames;
        State.preferredBackend = App.PreferredBackend;
        State.vsync = App.bVSync;
        const FlowToast& Toast = App.GetScreenFlow().toast();
        State.toast = Toast.secondsLeft > 0.0f ? Toast.text : std::string();
        return State;
    }
    ControlGame* game() override {
        return App.CurrentScreen == EScreen::Gameplay && App.Gameplay ? App.Gameplay.Get() : nullptr;
    }
    std::filesystem::path levelDirectory(GameMode Mode) const override { return App.LevelDirectory(Mode); }

    std::optional<ControlResponse> takeScreenshot(const std::string& Path) override {
        FScreenshotRequest::RequestScreenshot(UTF8_TO_TCHAR(Path.c_str()), true, false);
        return ControlResponse::ok(std::format("{{\"ok\":true,\"path\":\"{}\"}}", jsonEscape(Path)));
    }
    void play(GameMode Mode, const LevelFiles& Level) override {
        App.GetScreenFlow().startGame(Mode, Level);
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
    }
    void showMainMenu() override {
        App.GetScreenFlow().showMainMenu();
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
    }
    bool showLevelSelect(GameMode Mode) override {
        App.GetScreenFlow().showLevelSelect(Mode);
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
        return true;
    }
    bool openEditor(GameMode Mode, const std::optional<LevelFiles>& Level) override {
        App.GetScreenFlow().openEditor(Mode, Level);
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
        return true;
    }
    void quit() override { App.Quit(); }

    const Settings& settings() const override { return App.CurrentSettings; }
    void applySettings(const Settings& Next, std::optional<BackendKind> DefaultBackend) override {
        if (Next.vsync != App.bVSync) {
            App.SetVSyncEnabled(Next.vsync);
        }
        if (Next != App.CurrentSettings) {
            App.UpdateSettings(Next);
        }
        if (DefaultBackend) {
            App.SetPreferredBackend(*DefaultBackend);
        }
    }
    void openSettings() override {
        if (App.CurrentScreen == EScreen::Gameplay && App.Gameplay) {
            if (!App.GetScreenFlow().isPaused()) {
                App.GetScreenFlow().activate(FlowAction::TogglePause);
            }
            App.GetScreenFlow().activate(FlowAction::PauseSettings);
        } else if (App.CurrentScreen != EScreen::Settings) {
            App.GetScreenFlow().showSettings();
        }
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
    }
    void setVsync(bool bEnabled) override { App.SetVSyncEnabled(bEnabled); }

    ControlBenchmarkState benchmarkState() const override {
        ControlBenchmarkState State;
        const std::optional<BenchmarkRecord>& Record = App.Benchmark.GetRecord();
        State.state = App.Benchmark.IsRunning() ? "running"
                      : App.GetScreenFlow().benchmarkSettled() || (Record && Record->complete) ? "done"
                      : Record                                             ? "skipped"
                                                                           : "none";
        State.results = App.Benchmark.Results();
        State.winner = App.Benchmark.PickWinner();
        return State;
    }
    void runBenchmark() override {
        if (!App.Benchmark.IsRunning()) {
            App.GetScreenFlow().runMachineBenchmark();
            App.ProcessFlowRequests();
            App.ApplyFlowScreen();
        }
    }
    bool skipBenchmark() override {
        if (App.CurrentScreen != EScreen::Benchmark || App.GetScreenFlow().benchmarkSettled()) {
            return false;
        }
        App.GetScreenFlow().activate(FlowAction::SkipBenchmark);
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
        return true;
    }
    bool continueFromBenchmark() override {
        if (App.CurrentScreen != EScreen::Benchmark || !App.GetScreenFlow().benchmarkSettled()) {
            return false;
        }
        App.GetScreenFlow().activate(FlowAction::ContinueFromBenchmark);
        App.ProcessFlowRequests();
        App.ApplyFlowScreen();
        return true;
    }

    std::optional<ControlResponse> handleOwnRoute(const ControlRequest& Request) override {
        return App.HandleOwnControl(Request);
    }
};

TArray<FString> FBallsApp::OwnControlPaths() { return {TEXT("/cvar"), TEXT("/graphics")}; }

void FBallsApp::HandleControl(const FString& Path, const TMap<FString, FString>& Query, FString& OutBody,
                              int32& OutCode) {
    if (!ControlBridge) {
        ControlBridge = MakeShared<FControlBridge>(*this);
    }
    ControlRequest Request;
    Request.path = TCHAR_TO_UTF8(*Path);
    for (const TPair<FString, FString>& Entry : Query) {
        Request.query[std::string(TCHAR_TO_UTF8(*Entry.Key))] = TCHAR_TO_UTF8(*Entry.Value);
    }
    // Unreal answers a screenshot at once, so the core never leaves a request waiting here.
    const ControlResponse Response = handleControlRequest(*ControlBridge, Request).value_or(ControlResponse::ok());
    OutCode = Response.status;
    OutBody = UTF8_TO_TCHAR(Response.json.c_str());
}

std::optional<ControlResponse> FBallsApp::HandleOwnControl(const ControlRequest& Request) {
    const FString Path = UTF8_TO_TCHAR(Request.path.c_str());
    const auto Param = [&Request](const char* Name) { return FString(UTF8_TO_TCHAR(Request.text(Name).value_or("").c_str())); };
    if (Path == TEXT("/cvar")) {
        // Profiling hook for the ball pass and the frame cap: r.Balls.DrawPasses and r.Balls.VertexOnly split its
        // vertex and fragment cost, and t.MaxFPS lifts the display's cap so a measurement can see the real frame
        // time instead of the vsync clamp. Nothing else may be set from HTTP.
        const FString CvarName = Param("name");
        const FString Value = Param("value");
        const bool bAllowed =
            CvarName.StartsWith(TEXT("r.Balls.")) || CvarName.Equals(TEXT("t.MaxFPS"), ESearchCase::IgnoreCase);
        IConsoleVariable* Variable =
            bAllowed && !Value.IsEmpty() ? IConsoleManager::Get().FindConsoleVariable(*CvarName) : nullptr;
        if (!Variable) {
            return ControlResponse::error(400, "only r.Balls.* and t.MaxFPS can be set, with a value");
        }
        Variable->Set(*Value, ECVF_SetByCode);
        return ControlResponse::ok(std::format("{{\"ok\":true,\"name\":\"{}\",\"value\":{}}}",
                                               jsonEscape(TCHAR_TO_UTF8(*CvarName)), Variable->GetInt()));
    }
    if (Path == TEXT("/graphics")) {
        const FString Requested = Param("level");
        if (!Requested.IsEmpty()) {
            const std::optional<EBallsGraphics> Parsed = BallsGraphics::Parse(Requested);
            if (!Parsed) {
                return ControlResponse::error(400, "level must be minimum, balanced or high");
            }
            SetGraphicsQuality(*Parsed);
        }
        const int32 ScreenPercentage = GraphicsQuality == EBallsGraphics::Minimum    ? 70
                                       : GraphicsQuality == EBallsGraphics::Balanced ? 85
                                                                                     : 100;
        return ControlResponse::ok(std::format("{{\"ok\":true,\"level\":\"{}\",\"screenPercentage\":{}}}",
                                               TCHAR_TO_UTF8(*BallsGraphics::Name(GraphicsQuality)), ScreenPercentage));
    }
    return std::nullopt;
}

