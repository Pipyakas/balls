#pragma once

#include "game/GameMode.h"
#include "game/GameSession.h"
#include "level/LevelData.h"
#include "level/LevelImporter.h"
#include "level/LevelWatcher.h"
#include "ui/Theme.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

enum class FlowScreen { MainMenu, LevelSelect, Gameplay, LoadError, MapEditor, Settings, MachineBenchmark };

enum class FlowOverlay { None, Pause, Win, Settings };

enum class FlowAction {

    NormalMode,
    PerformanceMode,
    MapEditor,
    Settings,
    Quit,

    PlayLevel,
    EditLevel,
    NewLevel,
    Import,
    OpenFolder,

    TogglePause,
    Resume,
    Restart,
    PauseSettings,
    MainMenu,
    NextLevel,

    RunMachineBenchmark,

    SkipBenchmark,
    ContinueFromBenchmark,

    Back,
};

enum class FlowRequestKind {

    StartGame,

    ReloadGame,

    RestartGame,

    EndGame,

    OpenEditor,

    StartBenchmark,

    SkipBenchmark,

    PickLevelFiles,

    OpenFolder,
    Quit,
};

struct FlowRequest {
    FlowRequestKind kind = FlowRequestKind::Quit;
    GameMode mode = GameMode::Normal;
    std::optional<LevelFiles> files;
    std::filesystem::path folder;
};

struct FlowButton {
    FlowAction action = FlowAction::Back;
    std::string label;
    bool enabled = true;
};

struct FlowLevelRow {
    std::string name;

    bool highlighted = false;
};

struct FlowToast {
    std::string text;
    bool isError = false;
    float secondsLeft = 0.0f;
};

inline constexpr float levelListRefreshSeconds = 0.5f;
inline constexpr float importHighlightSeconds = 2.0f;
inline constexpr float benchmarkAdvanceSeconds = 1.5f;

class ScreenFlow {
public:

    ScreenFlow(std::filesystem::path normalLevels, std::filesystem::path performanceLevels);

    void update(float deltaSeconds, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    bool activate(FlowAction action, int row = -1);

    bool escape();

    void showMainMenu();
    void showLevelSelect(GameMode mode);
    void startGame(GameMode mode, const LevelFiles& files);
    void openEditor(GameMode mode, std::optional<LevelFiles> files);
    void showSettings();
    void runMachineBenchmark();

    void levelLoaded(std::string repairNotice = {});
    void levelFailed(std::string error);
    void sessionStateChanged(SessionState state);
    void benchmarkFinished();

    void playFromEditor(GameMode mode, const LevelFiles& files);

    void importLevels(std::span<const std::filesystem::path> picked);

    std::optional<FlowRequest> takeRequest();

    FlowScreen screen() const { return currentScreen; }
    FlowOverlay overlay() const { return currentOverlay; }
    bool isPaused() const { return currentOverlay != FlowOverlay::None; }
    GameMode mode() const { return currentMode; }

    const std::optional<LevelFiles>& level() const { return currentLevel; }
    const std::string& loadError() const { return lastLoadError; }

    std::string title() const;

    std::vector<FlowButton> buttons() const;

    std::string footnote() const;

    std::span<const FlowLevelRow> levelRows() const { return rows; }
    const std::filesystem::path& levelFolder() const { return folderFor(currentMode); }
    const std::filesystem::path& levelFolderFor(GameMode mode) const { return folderFor(mode); }

    std::optional<int> takeScrollToRow();

    std::string gameTitle() const;
    std::string_view diskNotice() const;
    LevelDiskState diskState() const { return watcher.diskState(); }

    bool canRunMachineBenchmark() const { return currentScreen == FlowScreen::Settings; }
    bool benchmarkSettled() const { return benchmarkDone; }

    void showToast(std::string text, bool isError = false, float seconds = theme::toastSeconds);
    const FlowToast& toast() const { return currentToast; }

private:
    const std::filesystem::path& folderFor(GameMode mode) const;
    void request(FlowRequestKind kind, std::optional<LevelFiles> files = std::nullopt, std::filesystem::path folder = {});
    void enterScreen(FlowScreen screen);
    void leaveGame();
    void refreshLevels();
    void restart();
    void nextLevel();
    bool activateInGame(FlowAction action);
    std::optional<LevelFiles> levelAt(int row) const;

    std::array<std::filesystem::path, 2> folders;
    FlowScreen currentScreen = FlowScreen::MainMenu;
    FlowOverlay currentOverlay = FlowOverlay::None;
    GameMode currentMode = GameMode::Normal;
    std::optional<LevelFiles> currentLevel;
    std::string lastLoadError;
    bool gameRunning = false;
    LevelWatcher watcher;

    std::vector<LevelFiles> levels;
    std::vector<FlowLevelRow> rows;
    float refreshSecondsLeft = 0.0f;
    std::string highlightedLevel;
    float highlightSecondsLeft = 0.0f;
    std::optional<int> scrollToRow;

    bool benchmarkDone = false;
    float benchmarkAdvanceLeft = 0.0f;

    FlowToast currentToast;
    std::vector<FlowRequest> requests;
};

std::string_view flowScreenName(FlowScreen screen);
