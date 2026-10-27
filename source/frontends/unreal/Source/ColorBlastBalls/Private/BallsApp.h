#pragma once

#include "BallsCoreIncludes.h"
#include "BallsFrameTimings.h"
#include "CoreMinimal.h"

#include <filesystem>
#include <optional>

class FBallsBoardView;
class FBallsGameplay;
class FBallsMapEditor;
class SBallsRoot;
class SWidget;
class UWorld;
struct FKey;

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
    void StartGame(GameMode Mode, const LevelFiles& Files);
    void Quit();

    BackendKind GetPreferredBackend() const { return PreferredBackend; }
    // Saved per machine and used by both modes. ChooseBackend switches the running game too.
    void SetPreferredBackend(BackendKind Kind);
    void ChooseBackend(BackendKind Kind);
    const Settings& GetSettings() const { return CurrentSettings; }
    // Saves the settings and applies them to the running game.
    void UpdateSettings(const Settings& Next);
    bool IsVSyncEnabled() const { return bVSync; }
    void SetVSyncEnabled(bool bEnabled);
    FBallsGameplay* GetGameplay() const { return Gameplay.Get(); }
    const FBallsFrameTimings& GetFrameTimings() const { return FrameTimings; }
    std::filesystem::path LevelDirectory(GameMode Mode) const;

private:
    enum class EScreen { MainMenu, LevelSelect, Gameplay, MapEditor, Settings };

    void SetScreen(EScreen Screen, TSharedRef<SWidget> Content);
    void RecordFrame(float DeltaSeconds);

    UWorld* World = nullptr;
    TSharedPtr<SBallsRoot> Root;
    TUniquePtr<FBallsBoardView> BoardView;
    TSharedPtr<FBallsGameplay> Gameplay;
    TSharedPtr<FBallsMapEditor> MapEditorState;
    EScreen CurrentScreen = EScreen::MainMenu;
    GameMode LevelSelectMode = GameMode::Normal;
    BackendKind PreferredBackend = BackendKind::Scalar;
    Settings CurrentSettings;
    bool bVSync = false;
    FBallsFrameTimings FrameTimings;
    FBallsFrameSample LatestSample;
};
