#pragma once

#include "BallsBenchmark.h"
#include "BallsCoreIncludes.h"
#include "BallsGraphics.h"
#include "CoreMinimal.h"

#include <filesystem>
#include <memory>
#include <optional>

class FBallsBoardView;
class FBallsControlServer;
class FBallsGameplay;
class FBallsMapEditor;
class SBallsLevelSelect;
class SBallsRoot;
class SWidget;
class UWorld;
struct FKey;

struct FBallsToast {
    FString Text;
    float SecondsLeft = 0.0f;
    bool bIsError = false;
};

// Owns the screens (main menu, level select, gameplay) and the world view. Port of frontends/raylib/app/App.
class FBallsApp {
public:
    explicit FBallsApp(UWorld* World);
    ~FBallsApp();

    void Tick(float DeltaSeconds);
    bool HandleKey(const FKey& Key);

    void ShowMainMenu();
    void ShowLevelSelect(GameMode Mode);
    void ShowMapEditor(GameMode Mode, std::optional<LevelFiles> Files);
    void ShowSettings();

    // One toast at a time, shown over whichever screen is up (3 s).
    void ShowToast(FString Text, bool bIsError);
    const FBallsToast& GetToast() const;
    // The core's single source of navigation truth.
    ScreenFlow& GetScreenFlow() const { return *Flow; }
    void ProcessFlowRequests();
    void ApplyFlowScreen();
    bool ActivateFlow(FlowAction Action, int32 Row = -1);
    // Level import ("Importing levels"): files from the picker or dropped on the window. Outside Level select
    // the drop only toasts a hint.
    void ImportLevels(const TArray<FString>& Paths);
    void OpenLevelImportDialog();
    void OpenLevelFolder(GameMode Mode);
    void StartGame(GameMode Mode, const LevelFiles& Files);
    void Quit();

    BackendKind GetPreferredBackend() const { return PreferredBackend; }
    // The default backend every new game starts with, saved per machine. Switching inside a game
    // (FBallsGameplay::SwitchBackend) lasts for that game only.
    void SetPreferredBackend(BackendKind Kind);
    const Settings& GetSettings() const { return CurrentSettings; }
    // Saves the settings and applies them to the running game.
    void UpdateSettings(const Settings& Next);
    // The machine benchmark, saved per machine in Saved/Balls.
    MachineProfile& GetMachineProfile() { return Profile; }
    // A forced orientation resizes the window on desktop (portrait: 90% of the display height at 9:16; landscape:
    // 1440 x 900); where it cannot, the layout letterboxes instead.
    void ApplyOrientation();
    bool IsVSyncEnabled() const { return bVSync; }
    void SetVSyncEnabled(bool bEnabled);
    // The Unreal port's graphics settings (scalability, UE5 features, upscaling), saved per user and applied at
    // once. The port starts at the Low preset: the UE post chain costs more than this scene.
    const FBallsGraphicsSettings& GetGraphics() const { return Graphics; }
    void SetGraphics(const FBallsGraphicsSettings& Next);
    // The Graphics page is a popup over whatever screen is up, so it can be changed while a game runs.
    void ShowGraphicsPage(bool bShow);
    bool IsGraphicsPageOpen() const { return bGraphicsPageOpen; }
    FBallsGameplay* GetGameplay() const { return Gameplay.Get(); }
    const FrameTimings& GetFrameTimings() const { return Frames; }
    const FrameSample& GetLatestSample() const { return Frames.latest(); }
    // The assets folder (levels, fonts, the scoring machine's benchmark record).
    std::filesystem::path AssetRoot() const;
    std::filesystem::path LevelDirectory(GameMode Mode) const;

    // "First launch: machine benchmark". The winner becomes the default backend and is stored with the machine
    // profile, so later launches skip the measurement until the hardware fingerprint changes.
    void ShowBenchmark();
    void FinishBenchmark(bool bComplete);
    FBallsMachineBenchmark& GetBenchmark() { return Benchmark; }
    FString GetBenchmarkWinnerLabel() const;

    // "Launch options and automation": the core's control API handler, called on the game thread. OutBody is
    // JSON; OutCode is the HTTP status (200 on success).
    void HandleControl(const FString& Path, const TMap<FString, FString>& Query, FString& OutBody, int32& OutCode);
    // The routes only Unreal has (console variables, graphics quality), for the control server to bind.
    static TArray<FString> OwnControlPaths();

private:
    enum class EScreen { MainMenu, LevelSelect, Gameplay, MapEditor, Settings, Benchmark };

    void SetScreen(EScreen Screen, TSharedRef<SWidget> Content);
    void RecordFrame(float DeltaSeconds);
    // What the core's control API may do to this front end.
    struct FControlBridge;
    std::optional<ControlResponse> HandleOwnControl(const ControlRequest& Request);
    TSharedPtr<FControlBridge> ControlBridge;

    UWorld* World = nullptr;
    TSharedPtr<SBallsRoot> Root;
    TSharedPtr<FBallsControlServer> Control;
    TUniquePtr<FBallsBoardView> BoardView;
    TSharedPtr<FBallsGameplay> Gameplay;
    TSharedPtr<FBallsMapEditor> MapEditorState;
    EScreen CurrentScreen = EScreen::MainMenu;
    GameMode LevelSelectMode = GameMode::Normal;
    BackendKind PreferredBackend = BackendKind::Scalar;
    Settings CurrentSettings;
    bool bVSync = false;
    FBallsGraphicsSettings Graphics;
    bool bGraphicsPageOpen = false;
    bool bOrientationApplied = false;
    TWeakPtr<SBallsLevelSelect> LevelSelect;
    std::unique_ptr<ScreenFlow> Flow;
    FrameTimings Frames;
    MachineProfile Profile;
    FBallsMachineBenchmark Benchmark{Profile};
    FlowScreen LastAppliedFlowScreen = FlowScreen::MainMenu;
    GameMode LastAppliedFlowMode = GameMode::Normal;
    bool bFlowScreenApplied = false;
};
