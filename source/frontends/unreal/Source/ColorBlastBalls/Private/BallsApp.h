#pragma once

#include "BallsBenchmark.h"
#include "BallsCoreIncludes.h"
#include "BallsGraphics.h"
#include "BallsFrameTimings.h"
#include "CoreMinimal.h"

#include <filesystem>
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
    const FBallsToast& GetToast() const { return Toast; }
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
    // The capacity probe's result per backend, saved per machine and reapplied on the next run.
    struct FMachineLimit {
        double TargetFps = 0.0;
        BallCount Balls = 0;
    };
    std::optional<FMachineLimit> GetMachineLimit(BackendKind Kind) const;
    void RememberMachineLimit(BackendKind Kind, double TargetFps, BallCount Balls);
    // A forced orientation resizes the window on desktop (portrait: 90% of the display height at 9:16; landscape:
    // 1440 x 900); where it cannot, the layout letterboxes instead.
    void ApplyOrientation();
    bool IsVSyncEnabled() const { return bVSync; }
    void SetVSyncEnabled(bool bEnabled);
    // The port starts at Minimum: the UE post chain costs more than this scene, and the player can trade it back.
    EBallsGraphics GetGraphicsQuality() const { return GraphicsQuality; }
    void SetGraphicsQuality(EBallsGraphics Quality);
    FBallsGameplay* GetGameplay() const { return Gameplay.Get(); }
    const FBallsFrameTimings& GetFrameTimings() const { return FrameTimings; }
    const FBallsFrameSample& GetLatestSample() const { return LatestSample; }
    // The assets folder (levels, fonts, the scoring machine's benchmark record).
    std::filesystem::path AssetRoot() const;
    std::filesystem::path LevelDirectory(GameMode Mode) const;

    // "First launch: machine benchmark". The winner becomes the default backend and is stored with the machine
    // profile, so later launches skip the measurement until the hardware fingerprint changes.
    void ShowBenchmark();
    void SkipBenchmark();
    void FinishBenchmark(bool bComplete);
    // Back to the Main menu, or to Settings when the run came from there.
    void LeaveBenchmark();
    // Settings' "Run machine benchmark" returns to Settings when it finishes.
    void SetBenchmarkReturnToSettings(bool bValue) { bBenchmarkReturnToSettings = bValue; }
    FBallsMachineBenchmark& GetBenchmark() { return Benchmark; }
    bool IsBenchmarkSettled() const { return bBenchmarkSettled; }
    FString GetBenchmarkWinnerLabel() const;

    // "Launch options and automation": one handler for every control API path, called on the game thread.
    // OutBody is JSON; OutCode is the HTTP status (200 on success).
    void HandleControl(const FString& Path, const TMap<FString, FString>& Query, FString& OutBody, int32& OutCode);
    FString GetScreenName() const;

private:
    enum class EScreen { MainMenu, LevelSelect, Gameplay, MapEditor, Settings, Benchmark };

    void SetScreen(EScreen Screen, TSharedRef<SWidget> Content);
    void RecordFrame(float DeltaSeconds);
    FString BuildControlStateJson() const;

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
    EBallsGraphics GraphicsQuality = EBallsGraphics::Minimum;
    bool bOrientationApplied = false;
    TWeakPtr<SBallsLevelSelect> LevelSelect;
    FBallsToast Toast;
    FBallsFrameTimings FrameTimings;
    FBallsFrameSample LatestSample;
    FBallsMachineBenchmark Benchmark;
    bool bBenchmarkSettled = false;
    bool bBenchmarkReturnToSettings = false;
    double BenchmarkSettledAt = 0.0;
};
