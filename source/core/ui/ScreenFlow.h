#pragma once

#include "game/GameMode.h"
#include "game/GameSession.h"
#include "level/LevelData.h"
#include "level/LevelImporter.h"
#include "level/LevelWatcher.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The game's screen flow, once for every front end: which screen and overlay are up, where each button, Back and Esc
// lead, the level list of Level select with its imports, the level-file watcher of the game in play, and the toast.
// A front end draws what this describes, reports clicks as FlowActions, and carries out the FlowRequests it hands
// out (start a game, open the editor, run the benchmark, quit...) with its own session, editor and windowing code.
enum class FlowScreen { MainMenu, LevelSelect, Gameplay, LoadError, MapEditor, Settings, MachineBenchmark };

// Over Gameplay only. Settings is the Settings panel opened from Pause; the game stays paused under it.
enum class FlowOverlay { None, Pause, Win, Settings };

enum class FlowAction {
    // Main menu
    NormalMode,
    PerformanceMode,
    MapEditor,
    Settings,
    Quit,
    // Level select; PlayLevel and EditLevel take the row index
    PlayLevel,
    EditLevel,
    NewLevel,
    Import,
    OpenFolder,
    // Gameplay: the header toggles pause; the overlays hold the rest
    TogglePause,
    Resume,
    Restart,
    PauseSettings,
    MainMenu,
    NextLevel,
    // Settings page
    RunMachineBenchmark,
    // Machine benchmark: Skip before the result, Continue after it
    SkipBenchmark,
    ContinueFromBenchmark,
    // Every screen with a way out
    Back,
};

// What the front end must do after an action; each is handed out once by takeRequest.
enum class FlowRequestKind {
    // Load `files` and play them in `mode`; report levelFailed if they do not parse.
    StartGame,
    // Load the files again for a Restart after they changed on disk.
    ReloadGame,
    // Restart the game in play from the files already loaded.
    RestartGame,
    // Leave the game in play (its session can be dropped).
    EndGame,
    // Open the map editor on `files`, or on a new level of `mode` when `files` is empty.
    OpenEditor,
    // Start the machine benchmark on this machine; report benchmarkFinished when it settles.
    StartBenchmark,
    // Stop a running benchmark early, keeping the best result so far.
    SkipBenchmark,
    // Show the OS file picker starting in `folder`, then pass the picks to importLevels.
    PickLevelFiles,
    // Open `folder` in the OS file manager.
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
    // Just imported: the design highlights the row for a while.
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
    // The folders the two modes read their levels from.
    ScreenFlow(std::filesystem::path normalLevels, std::filesystem::path performanceLevels);

    // Advances the toast, the import highlight, the level list refresh, the benchmark's advance and the watcher of
    // the level in play.
    void update(float deltaSeconds, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // A click on a button this flow listed, the header, or a level row (`row`). False when the action does not
    // apply to the screen that is up, so nothing changed.
    bool activate(FlowAction action, int row = -1);
    // Esc: Back where the design gives the screen one. Gameplay has none, apart from its Settings overlay and the load
    // error. The map editor's own Esc (name field, unsaved changes) is the editor's, which then calls activate(Back).
    bool escape();

    // Direct navigation for launch options and the control API.
    void showMainMenu();
    void showLevelSelect(GameMode mode);
    void startGame(GameMode mode, const LevelFiles& files);
    void openEditor(GameMode mode, std::optional<LevelFiles> files);
    void showSettings();
    void runMachineBenchmark();

    // Reports from the front end.
    void levelLoaded();
    void levelFailed(std::string error);
    void sessionStateChanged(SessionState state);
    void benchmarkFinished();
    // The editor saved and wants to play its level.
    void playFromEditor(GameMode mode, const LevelFiles& files);
    // Files picked in the dialog or dropped on the window: imported into the open Level select's mode, else a toast
    // says where imports work.
    void importLevels(std::span<const std::filesystem::path> picked);

    std::optional<FlowRequest> takeRequest();

    FlowScreen screen() const { return currentScreen; }
    FlowOverlay overlay() const { return currentOverlay; }
    bool isPaused() const { return currentOverlay != FlowOverlay::None; }
    GameMode mode() const { return currentMode; }
    // The level in play, being edited, or that failed to load.
    const std::optional<LevelFiles>& level() const { return currentLevel; }
    const std::string& loadError() const { return lastLoadError; }
    // The title of the screen or overlay that is up; empty where the design gives none (gameplay, editor).
    std::string title() const;
    // The buttons of the screen or overlay that is up, in the game's order. Level rows are levelRows.
    std::vector<FlowButton> buttons() const;
    // The game's muted line under a screen's content (Level select's folder, the load error hint).
    std::string footnote() const;

    // Level select.
    std::span<const FlowLevelRow> levelRows() const { return rows; }
    const std::filesystem::path& levelFolder() const { return folderFor(currentMode); }
    const std::filesystem::path& levelFolderFor(GameMode mode) const { return folderFor(mode); }
    // The row to scroll to after an import, once.
    std::optional<int> takeScrollToRow();

    // Gameplay: the header's title, and the notice about the level's files changing on disk (empty when none).
    std::string gameTitle() const;
    std::string_view diskNotice() const;
    LevelDiskState diskState() const { return watcher.diskState(); }

    // Settings opened from the Main menu (a page) can run the machine benchmark; from Pause it cannot end the game.
    bool canRunMachineBenchmark() const { return currentScreen == FlowScreen::Settings; }
    bool benchmarkSettled() const { return benchmarkDone; }

    void showToast(std::string text, bool isError = false);
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
