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
#include "UI/SBallsRoot.h"

namespace {

constexpr int32 RootZOrder = 10;
// "First launch": the Main menu opens 1.5 s after the last backend finished, so the winner stays readable.
constexpr double BenchmarkAdvanceSeconds = 1.5;

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

BackendKind InitialBackend() {
#if PLATFORM_WINDOWS && defined(BALLS_PLUGIN_DIR)
    AddPhysicsPluginDirectory();
#endif
    if (const std::optional<BackendKind> Requested = BackendFromCommandLine()) {
        return *Requested;
    }
    return SavedBackend().value_or(fastestAvailableCpuBackend());
}

// The control API answers JSON, so level names and toasts have to be escaped.
FString EscapeJson(const FString& Text) {
    FString Out;
    Out.Reserve(Text.Len() + 8);
    for (const TCHAR Character : Text) {
        switch (Character) {
        case TEXT('"'):
            Out += TEXT("\\\"");
            break;
        case TEXT('\\'):
            Out += TEXT("\\\\");
            break;
        case TEXT('\n'):
            Out += TEXT("\\n");
            break;
        case TEXT('\r'):
            Out += TEXT("\\r");
            break;
        case TEXT('\t'):
            Out += TEXT("\\t");
            break;
        default:
            Out.AppendChar(Character);
        }
    }
    return Out;
}

}

FBallsApp::FBallsApp(UWorld* InWorld)
    : World(InWorld), PreferredBackend(InitialBackend()), CurrentSettings(loadSettings(SettingsFile())),
      bVSync(SavedVSync()), GraphicsQuality(SavedGraphics()) {
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
#if PLATFORM_ANDROID
    StageAndroidLevels();
#endif
    ApplyOrientation();
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
    // "First launch: machine benchmark": the first run on a machine measures every backend before the Main menu
    // so the target PC starts on its best one. -BallsPlay, -BallsBackend and -BallsSkipBenchmark state their own
    // choice and never measure.
    Benchmark.LoadRecord();
    FString ChosenBackend;
    const bool bChoiceOnCommandLine =
        FParse::Param(FCommandLine::Get(), TEXT("BallsSkipBenchmark")) ||
        FParse::Value(FCommandLine::Get(), TEXT("BallsPlay="), Play) ||
        FParse::Value(FCommandLine::Get(), TEXT("BallsBackend="), ChosenBackend);
    if (!bChoiceOnCommandLine && CurrentScreen == EScreen::MainMenu && Benchmark.NeedsRun()) {
        ShowBenchmark();
    }

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
    TSharedRef<SBallsLevelSelect> Screen = SNew(SBallsLevelSelect).App(this).Mode(Mode);
    LevelSelect = Screen;
    SetScreen(EScreen::LevelSelect, Screen);
}

void FBallsApp::ShowToast(FString Text, bool bIsError) { Toast = {MoveTemp(Text), 3.0f, bIsError}; }

void FBallsApp::ImportLevels(const TArray<FString>& Paths) {
    const TSharedPtr<SBallsLevelSelect> Screen = LevelSelect.Pin();
    if (CurrentScreen != EScreen::LevelSelect || !Screen) {
        ShowToast(TEXT("Open Level select to import levels"), false);
        return;
    }
    std::vector<std::filesystem::path> Picked;
    for (const FString& Path : Paths) {
        Picked.emplace_back(TCHAR_TO_UTF8(*Path));
    }
    const LevelImportReport Report = importLevels(Picked, LevelDirectory(LevelSelectMode), LevelSelectMode);
    ShowToast(BallsText::FromUtf8(Report.summary()), Report.anyFailed() && Report.importedCount() == 0);
    Screen->Refresh();
    if (const std::optional<std::string> Highlight = Report.levelToHighlight()) {
        Screen->Highlight(BallsText::FromUtf8(*Highlight));
    }
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
    const FString Directory = FString(UTF8_TO_TCHAR(LevelDirectory(LevelSelectMode).string().c_str()));
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
    SetScreen(EScreen::Settings, SNew(SBallsSettingsPage).App(this));
}

void FBallsApp::ShowBenchmark() {
    FString Error;
    // Load every physics plugin on the game thread first: loading a DLL from the benchmark's worker thread while the
    // engine is running has crashed the process.
    for (const BackendKind Kind : allBackendKinds) {
        isBackendAvailable(Kind);
    }
    if (!Benchmark.Start(LevelDirectory(GameMode::Performance), Error)) {
        // Without a level to measure on the run is pointless; say why and go on to the menu.
        ShowToast(FString::Printf(TEXT("Benchmark: %s"), *Error), true);
        ShowMainMenu();
        return;
    }
    bBenchmarkSettled = false;
    SetScreen(EScreen::Benchmark, SNew(SBallsBenchmark).App(this));
}

void FBallsApp::SkipBenchmark() {
    // Keep the best result so far, but leave the record incomplete so the next launch measures again.
    Benchmark.Skip();
    FinishBenchmark(false);
    LeaveBenchmark();
}

void FBallsApp::FinishBenchmark(bool bComplete) {
    const std::optional<BenchmarkCandidate> Winner = Benchmark.PickWinner();
    Benchmark.Save(bComplete, Winner);
    if (Winner && isBackendAvailable(Winner->kind)) {
        SetPreferredBackend(Winner->kind);
    }
    bBenchmarkSettled = true;
    BenchmarkSettledAt = FPlatformTime::Seconds();
}

void FBallsApp::LeaveBenchmark() {
    const bool bToSettings = bBenchmarkReturnToSettings;
    bBenchmarkReturnToSettings = false;
    if (bToSettings) {
        ShowSettings();
    } else {
        ShowMainMenu();
    }
}

FString FBallsApp::GetBenchmarkWinnerLabel() const {
    const std::optional<BenchmarkCandidate> Winner = Benchmark.PickWinner();
    return Winner ? BallsText::FromUtf8(Winner->label) : FString();
}

void FBallsApp::SetPreferredBackend(BackendKind Kind) {
    PreferredBackend = Kind;
    if (GConfig) {
        GConfig->SetString(PreferencesSection, TEXT("Backend"), *BallsText::FromUtf8(backendKindName(Kind)),
                           GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
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
    // The first-launch benchmark owns the screen until it settles, then hands over after a beat so the winner can
    // be read ("after 1.5 s, or on Continue, the Main menu opens").
    if (CurrentScreen == EScreen::Benchmark) {
        if (!bBenchmarkSettled && Benchmark.IsFinished()) {
            FinishBenchmark(true);
        }
        if (bBenchmarkSettled && FPlatformTime::Seconds() - BenchmarkSettledAt >= BenchmarkAdvanceSeconds) {
            LeaveBenchmark();
        }
        return;
    }
    Toast.SecondsLeft = FMath::Max(0.0f, Toast.SecondsLeft - DeltaSeconds);
    // The viewport window may not exist yet in the constructor, so a saved forced orientation resizes it here.
    if (!bOrientationApplied && World && World->GetGameViewport() && World->GetGameViewport()->GetWindow()) {
        bOrientationApplied = true;
        ApplyOrientation();
    }
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

FString FBallsApp::GetScreenName() const {
    switch (CurrentScreen) {
    case EScreen::LevelSelect:
        return TEXT("level-select");
    case EScreen::Gameplay:
        return TEXT("gameplay");
    case EScreen::MapEditor:
        return TEXT("editor");
    case EScreen::Settings:
        return TEXT("settings");
    case EScreen::Benchmark:
        return TEXT("benchmark");
    default:
        return TEXT("main-menu");
    }
}

FString FBallsApp::BuildControlStateJson() const {
    const FBallsFrameSample& Sample = LatestSample;
    const FGeometry& Geometry = Root->GetCachedGeometry();
    const FVector2D WindowSize = Geometry.GetLocalSize();
    const double Fps = FrameTimings.FramesPerSecond();
    const double MinFps = FrameTimings.WorstFpsLastSecond();
    const BackendKind ActiveBackend = Gameplay && Gameplay->GetSession() ? Gameplay->GetSession()->backend()
                                                                        : PreferredBackend;

    FString Details = TEXT("{}");
    if (Gameplay && Gameplay->GetSession()) {
        const GameSession& Session = *Gameplay->GetSession();
        const Simulation& Sim = Session.simulation();
        static const TCHAR* const StateNames[] = {TEXT("ready"), TEXT("running"), TEXT("won")};
        const int32 StateIndex = static_cast<int32>(Session.state());
        Details = FString::Printf(
            TEXT("{\"mode\":\"%s\",\"level\":\"%s\",\"state\":\"%s\",\"paused\":%s,\"balls\":%llu,")
            TEXT("\"bricks\":%d,\"launchersLeft\":%d,\"physicsMs\":%.2f,\"ticks\":%d,\"spawnRate\":%d,")
            TEXT("\"ballLimit\":%llu,\"elapsed\":%.2f}"),
            Gameplay->GetMode() == GameMode::Performance ? TEXT("performance") : TEXT("normal"),
            *EscapeJson(BallsText::FromUtf8(Gameplay->GetFiles().name)),
            StateNames[FMath::Clamp(StateIndex, 0, 2)], Gameplay->IsPaused() ? TEXT("true") : TEXT("false"),
            static_cast<unsigned long long>(Sim.stats().ballCount), Sim.board().aliveBrickCount(),
            Session.launchers().launcherCount(), Sim.stats().lastFramePhysicsMs, Sim.stats().ticksLastFrame,
            spawnRateOptions[Session.spawnRateIndex()], static_cast<unsigned long long>(Session.ballLimit()),
            Session.elapsedSeconds());
    }

    const FString ToastText = Toast.SecondsLeft > 0.0f ? Toast.Text : FString();
    return FString::Printf(
        TEXT("{\"ok\":true,\"frontend\":\"unreal\",\"renderer\":\"%s\",\"screen\":\"%s\",\"window\":[%.0f,%.0f],")
        TEXT("\"fps\":%.1f,\"minFps\":%.1f,\"frameMs\":%.2f,\"cpuMs\":%.2f,\"gpuMs\":%.2f,\"renderMs\":%.2f,")
        TEXT("\"backend\":\"%s\",\"vsync\":%s,\"toast\":\"%s\",\"details\":%s}"),
        *EscapeJson(GRHIAdapterName), *GetScreenName(), WindowSize.X, WindowSize.Y, Fps, MinFps, Sample.FrameMs,
        Sample.GameMs, Sample.GpuMs, Sample.RenderMs, *BallsText::FromUtf8(backendKindName(ActiveBackend)),
        bVSync ? TEXT("true") : TEXT("false"), *EscapeJson(ToastText), *Details);
}

void FBallsApp::HandleControl(const FString& Path, const TMap<FString, FString>& Query, FString& OutBody,
                              int32& OutCode) {
    OutCode = 200;
    const auto Param = [&Query](const TCHAR* Name) {
        const FString* Value = Query.Find(Name);
        return Value ? *Value : FString();
    };
    const auto IntParam = [&Param](const TCHAR* Name, int32 Fallback) {
        const FString Value = Param(Name);
        return Value.IsEmpty() ? Fallback : FCString::Atoi(*Value);
    };

    if (Path == TEXT("/state")) {
        OutBody = BuildControlStateJson();
        return;
    }
    if (Path == TEXT("/screenshot")) {
        const FString Target = Param(TEXT("path"));
        if (Target.IsEmpty()) {
            OutCode = 400;
            OutBody = TEXT("{\"ok\":false,\"error\":\"missing path\"}");
            return;
        }
        FScreenshotRequest::RequestScreenshot(Target, true, false);
        OutBody = FString::Printf(TEXT("{\"ok\":true,\"path\":\"%s\"}"), *EscapeJson(Target));
        return;
    }
    if (Path == TEXT("/play")) {
        const FString ModeName = Param(TEXT("mode"));
        const bool bPerformance = ModeName.Equals(TEXT("performance"), ESearchCase::IgnoreCase);
        if (!bPerformance && !ModeName.IsEmpty() && !ModeName.Equals(TEXT("normal"), ESearchCase::IgnoreCase)) {
            OutCode = 400;
            OutBody = TEXT("{\"ok\":false,\"error\":\"mode must be normal or performance\"}");
            return;
        }
        const GameMode Mode = bPerformance ? GameMode::Performance : GameMode::Normal;
        const FString LevelName = Param(TEXT("level"));
        for (const LevelFiles& Files : findLevels(LevelDirectory(Mode))) {
            if (LevelName.IsEmpty() || LevelName.Equals(BallsText::FromUtf8(Files.name), ESearchCase::IgnoreCase)) {
                StartGame(Mode, Files);
                OutBody = TEXT("{\"ok\":true}");
                return;
            }
        }
        OutCode = 404;
        OutBody = FString::Printf(TEXT("{\"ok\":false,\"error\":\"no level '%s'\"}"), *EscapeJson(LevelName));
        return;
    }
    if (Path == TEXT("/start")) {
        if (!Gameplay) {
            OutCode = 409;
            OutBody = TEXT("{\"ok\":false,\"error\":\"no game\"}");
            return;
        }
        Gameplay->StartAutoRelease();
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/rate")) {
        if (!Gameplay) {
            OutCode = 409;
            OutBody = TEXT("{\"ok\":false,\"error\":\"no game\"}");
            return;
        }
        Gameplay->SelectSpawnRate(IntParam(TEXT("index"), 0));
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/limit")) {
        if (!Gameplay) {
            OutCode = 409;
            OutBody = TEXT("{\"ok\":false,\"error\":\"no game\"}");
            return;
        }
        Gameplay->SelectBallLimit(IntParam(TEXT("index"), 0));
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/fill")) {
        GameSession* Session = Gameplay ? Gameplay->GetSession() : nullptr;
        if (!Session) {
            OutCode = 409;
            OutBody = TEXT("{\"ok\":false,\"error\":\"no game\"}");
            return;
        }
        Session->fillWithBalls(static_cast<BallCount>(FMath::Max(IntParam(TEXT("count"), 0), 0)));
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/probe")) {
        if (!Gameplay) {
            OutCode = 409;
            OutBody = TEXT("{\"ok\":false,\"error\":\"no game\"}");
            return;
        }
        if (IntParam(TEXT("cancel"), 0) != 0) {
            Gameplay->ToggleCapacityProbe();
        } else {
            Gameplay->SetProbeTargetFps(IntParam(TEXT("fps"), Gameplay->GetProbeTargetFps()));
            Gameplay->ToggleCapacityProbe();
        }
        OutBody = FString::Printf(TEXT("{\"ok\":true,\"target\":%d}"), Gameplay->GetProbeTargetFps());
        return;
    }
    if (Path == TEXT("/backend")) {
        const FString Name = Param(TEXT("kind"));
        std::optional<BackendKind> Kind;
        for (const BackendKind Candidate : allBackendKinds) {
            if (Name.Equals(BallsText::FromUtf8(backendKindName(Candidate)), ESearchCase::IgnoreCase)) {
                Kind = Candidate;
            }
        }
        if (!Kind || !isBackendAvailable(*Kind)) {
            OutCode = 409;
            OutBody = TEXT("{\"ok\":false,\"error\":\"backend unknown or unavailable\"}");
            return;
        }
        if (Gameplay && Gameplay->GetSession()) {
            Gameplay->SwitchBackend(*Kind);
        } else {
            SetPreferredBackend(*Kind);
        }
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/settings")) {
        Settings Next = CurrentSettings;
        const FString Layout = Param(TEXT("layout"));
        if (!Layout.IsEmpty()) {
            if (const std::optional<LayoutOrientation> Parsed = parseOrientation(std::string(TCHAR_TO_UTF8(*Layout)))) {
                Next.layout = *Parsed;
            }
        }
        const FString Collisions = Param(TEXT("ballCollisions"));
        if (!Collisions.IsEmpty()) {
            Next.ballCollisions = Collisions != TEXT("0") && !Collisions.Equals(TEXT("false"), ESearchCase::IgnoreCase);
        }
        if (Next != CurrentSettings) {
            UpdateSettings(Next);
        }
        const FString VSync = Param(TEXT("vsync"));
        if (!VSync.IsEmpty()) {
            SetVSyncEnabled(VSync != TEXT("0") && !VSync.Equals(TEXT("false"), ESearchCase::IgnoreCase));
        }
        OutBody = FString::Printf(
            TEXT("{\"ok\":true,\"layout\":\"%s\",\"ballCollisions\":%s,\"vsync\":%s,\"backend\":\"%s\"}"),
            *BallsText::FromUtf8(orientationName(CurrentSettings.layout)),
            CurrentSettings.ballCollisions ? TEXT("true") : TEXT("false"), bVSync ? TEXT("true") : TEXT("false"),
            *BallsText::FromUtf8(backendKindName(PreferredBackend)));
        return;
    }
    if (Path == TEXT("/pause") || Path == TEXT("/resume")) {
        if (!Gameplay) {
            OutCode = 409;
            OutBody = TEXT("{\"ok\":false,\"error\":\"no game\"}");
            return;
        }
        Gameplay->SetPaused(Path == TEXT("/pause"));
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/graphics")) {
        const FString Requested = Param(TEXT("level"));
        if (!Requested.IsEmpty()) {
            const std::optional<EBallsGraphics> Parsed = BallsGraphics::Parse(Requested);
            if (!Parsed) {
                OutCode = 400;
                OutBody = TEXT("{\"ok\":false,\"error\":\"level must be minimum, balanced or high\"}");
                return;
            }
            SetGraphicsQuality(*Parsed);
        }
        OutBody = FString::Printf(TEXT("{\"ok\":true,\"level\":\"%s\",\"screenPercentage\":%d}"),
                                  *BallsGraphics::Name(GraphicsQuality),
                                  GraphicsQuality == EBallsGraphics::Minimum ? 70
                                  : GraphicsQuality == EBallsGraphics::Balanced ? 85
                                                                                : 100);
        return;
    }
    if (Path == TEXT("/benchmark")) {
        if (IntParam(TEXT("run"), 0) != 0 && !Benchmark.IsRunning()) {
            bBenchmarkReturnToSettings = false;
            ShowBenchmark();
        }
        static const TCHAR* const StatusNames[] = {TEXT("pending"), TEXT("running"), TEXT("done"), TEXT("failed"),
                                                   TEXT("unavailable")};
        FString Candidates;
        for (const CandidateResult& Result : Benchmark.Results()) {
            Candidates += FString::Printf(
                TEXT("%s{\"backend\":\"%s\",\"status\":\"%s\",\"ballStepsPerSecond\":%.0f,\"progress\":%.2f,")
                TEXT("\"failure\":\"%s\"}"),
                Candidates.IsEmpty() ? TEXT("") : TEXT(","),
                *EscapeJson(BallsText::FromUtf8(backendKindName(Result.candidate.kind))),
                StatusNames[FMath::Clamp(static_cast<int32>(Result.status), 0, 4)], Result.ballStepsPerSecond,
                Result.progress, *EscapeJson(BallsText::FromUtf8(Result.failure)));
        }
        const FString Winner = GetBenchmarkWinnerLabel();
        const TCHAR* State = Benchmark.IsRunning() ? TEXT("running") : bBenchmarkSettled ? TEXT("done") : TEXT("idle");
        OutBody = FString::Printf(
            TEXT("{\"ok\":true,\"state\":\"%s\",\"winner\":%s,\"results\":[%s]}"), State,
            Winner.IsEmpty() ? TEXT("null") : *FString::Printf(TEXT("\"%s\""), *EscapeJson(Winner)), *Candidates);
        return;
    }
    if (Path == TEXT("/benchmark/skip")) {
        SkipBenchmark();
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/benchmark/continue")) {
        LeaveBenchmark();
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/menu")) {
        ShowMainMenu();
        OutBody = TEXT("{\"ok\":true}");
        return;
    }
    if (Path == TEXT("/quit")) {
        OutBody = TEXT("{\"ok\":true}");
        Quit();
        return;
    }
    OutCode = 404;
    OutBody = FString::Printf(TEXT("{\"ok\":false,\"error\":\"unknown path %s\"}"), *EscapeJson(Path));
}

