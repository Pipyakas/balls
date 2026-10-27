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
    void StartGame(GameMode Mode, const LevelFiles& Files);
    void Quit();

    BackendKind GetPreferredBackend() const { return PreferredBackend; }
    void SetPreferredBackend(BackendKind Kind) { PreferredBackend = Kind; }
    const FBallsFrameTimings& GetFrameTimings() const { return FrameTimings; }
    std::filesystem::path LevelDirectory(GameMode Mode) const;

private:
    enum class EScreen { MainMenu, LevelSelect, Gameplay, MapEditor };

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
    FBallsFrameTimings FrameTimings;
};
