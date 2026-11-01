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

    void ShowToast(FString Text, bool bIsError);
    const FBallsToast& GetToast() const;

    ScreenFlow& GetScreenFlow() const { return *Flow; }
    void ProcessFlowRequests();
    void ApplyFlowScreen();
    bool ActivateFlow(FlowAction Action, int32 Row = -1);

    void ImportLevels(const TArray<FString>& Paths);
    void OpenLevelImportDialog();
    void OpenLevelFolder(GameMode Mode);
    void StartGame(GameMode Mode, const LevelFiles& Files);
    void Quit();

    BackendKind GetPreferredBackend() const { return PreferredBackend; }

    void SetPreferredBackend(BackendKind Kind);
    const Settings& GetSettings() const { return CurrentSettings; }

    void UpdateSettings(const Settings& Next);

    MachineProfile& GetMachineProfile() { return Profile; }

    void ApplyOrientation();
    bool IsVSyncEnabled() const { return bVSync; }
    void SetVSyncEnabled(bool bEnabled);

    const FBallsGraphicsSettings& GetGraphics() const { return Graphics; }
    void SetGraphics(const FBallsGraphicsSettings& Next);

    void ShowGraphicsPage(bool bShow);
    bool IsGraphicsPageOpen() const { return bGraphicsPageOpen; }
    FBallsGameplay* GetGameplay() const { return Gameplay.Get(); }
    const FrameTimings& GetFrameTimings() const { return Frames; }
    const FrameSample& GetLatestSample() const { return Frames.latest(); }

    std::filesystem::path AssetRoot() const;
    std::filesystem::path LevelDirectory(GameMode Mode) const;

    void ShowBenchmark();
    void FinishBenchmark(bool bComplete);
    FBallsMachineBenchmark& GetBenchmark() { return Benchmark; }
    FString GetBenchmarkWinnerLabel() const;

    void HandleControl(const FString& Path, const TMap<FString, FString>& Query, FString& OutBody, int32& OutCode);

    static TArray<FString> OwnControlPaths();

private:
    enum class EScreen { MainMenu, LevelSelect, Gameplay, MapEditor, Settings, Benchmark };

    void SetScreen(EScreen Screen, TSharedRef<SWidget> Content);
    void RecordFrame(float DeltaSeconds);

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
