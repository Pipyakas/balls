#include "ui/ScreenFlow.h"

#include "level/LevelRepository.h"
#include "ui/Theme.h"

#include <algorithm>
#include <utility>

namespace {

constexpr std::string_view importElsewhereMessage = "Open Level select to import levels";

std::string modeTitle(GameMode mode) { return mode == GameMode::Normal ? "Normal Mode" : "Performance Mode"; }

}

std::string_view flowScreenName(FlowScreen screen) {
    switch (screen) {
    case FlowScreen::MainMenu:
        return "menu";
    case FlowScreen::LevelSelect:
        return "levels";

    case FlowScreen::Gameplay:
    case FlowScreen::LoadError:
        return "gameplay";
    case FlowScreen::MapEditor:
        return "editor";
    case FlowScreen::Settings:
        return "settings";
    case FlowScreen::MachineBenchmark:
        return "benchmark";
    }
    return "menu";
}

ScreenFlow::ScreenFlow(std::filesystem::path normalLevels, std::filesystem::path performanceLevels)
    : folders{std::move(normalLevels), std::move(performanceLevels)} {}

const std::filesystem::path& ScreenFlow::folderFor(GameMode mode) const {
    return folders[mode == GameMode::Normal ? 0 : 1];
}

void ScreenFlow::request(FlowRequestKind kind, std::optional<LevelFiles> files, std::filesystem::path folder) {
    requests.push_back(FlowRequest{kind, currentMode, std::move(files), std::move(folder)});
}

std::optional<FlowRequest> ScreenFlow::takeRequest() {
    if (requests.empty()) {
        return std::nullopt;
    }
    FlowRequest next = std::move(requests.front());
    requests.erase(requests.begin());
    return next;
}

void ScreenFlow::enterScreen(FlowScreen screen) {
    if (screen != FlowScreen::Gameplay && screen != FlowScreen::LoadError) {
        leaveGame();
    }
    currentScreen = screen;
    currentOverlay = FlowOverlay::None;
    if (screen == FlowScreen::LevelSelect) {
        refreshLevels();
    }
}

void ScreenFlow::leaveGame() {
    if (gameRunning) {
        gameRunning = false;
        request(FlowRequestKind::EndGame);
    }
}

void ScreenFlow::showMainMenu() { enterScreen(FlowScreen::MainMenu); }

void ScreenFlow::showLevelSelect(GameMode mode) {
    currentMode = mode;
    enterScreen(FlowScreen::LevelSelect);
}

void ScreenFlow::startGame(GameMode mode, const LevelFiles& files) {
    leaveGame();
    currentMode = mode;
    currentLevel = files;
    lastLoadError.clear();
    currentScreen = FlowScreen::Gameplay;
    currentOverlay = FlowOverlay::None;
    watcher.watch(files);
    request(FlowRequestKind::StartGame, files);
}

void ScreenFlow::openEditor(GameMode mode, std::optional<LevelFiles> files) {
    currentMode = mode;
    enterScreen(FlowScreen::MapEditor);
    currentLevel = files;
    request(FlowRequestKind::OpenEditor, std::move(files));
}

void ScreenFlow::showSettings() { enterScreen(FlowScreen::Settings); }

void ScreenFlow::runMachineBenchmark() {
    enterScreen(FlowScreen::MachineBenchmark);
    benchmarkDone = false;
    benchmarkAdvanceLeft = 0.0f;
    request(FlowRequestKind::StartBenchmark);
}

void ScreenFlow::levelLoaded(std::string repairNotice) {
    gameRunning = true;
    lastLoadError.clear();
    watcher.markLoaded();
    if (currentScreen == FlowScreen::LoadError) {
        currentScreen = FlowScreen::Gameplay;
    }
    if (!repairNotice.empty()) {
        showToast(std::move(repairNotice), false, theme::repairToastSeconds);
    }
}

void ScreenFlow::levelFailed(std::string error) {
    gameRunning = false;
    lastLoadError = std::move(error);
    currentScreen = FlowScreen::LoadError;
    currentOverlay = FlowOverlay::None;
}

void ScreenFlow::sessionStateChanged(SessionState state) {

    if (state == SessionState::Won && currentMode == GameMode::Normal && currentScreen == FlowScreen::Gameplay) {
        currentOverlay = FlowOverlay::Win;
    }
}

void ScreenFlow::benchmarkFinished() {
    if (currentScreen == FlowScreen::MachineBenchmark && !benchmarkDone) {
        benchmarkDone = true;
        benchmarkAdvanceLeft = benchmarkAdvanceSeconds;
    }
}

void ScreenFlow::playFromEditor(GameMode mode, const LevelFiles& files) { startGame(mode, files); }

void ScreenFlow::importLevels(std::span<const std::filesystem::path> picked) {
    if (picked.empty()) {
        return;
    }
    if (currentScreen != FlowScreen::LevelSelect) {
        showToast(std::string(importElsewhereMessage));
        return;
    }
    const LevelImportReport report = ::importLevels(picked, levelFolder(), currentMode);
    showToast(report.summary(), report.anyFailed());
    if (const auto name = report.levelToHighlight()) {
        highlightedLevel = *name;
        highlightSecondsLeft = importHighlightSeconds;
    }
    refreshLevels();
    for (size_t index = 0; index < rows.size(); ++index) {
        if (rows[index].highlighted) {
            scrollToRow = static_cast<int>(index);
        }
    }
}

std::optional<int> ScreenFlow::takeScrollToRow() { return std::exchange(scrollToRow, std::nullopt); }

void ScreenFlow::refreshLevels() {
    levels = findLevels(levelFolder());
    rows.clear();
    rows.reserve(levels.size());
    for (const LevelFiles& files : levels) {
        rows.push_back({files.name, highlightSecondsLeft > 0.0f && files.name == highlightedLevel});
    }
    refreshSecondsLeft = levelListRefreshSeconds;
}

std::optional<LevelFiles> ScreenFlow::levelAt(int row) const {
    if (row < 0 || row >= static_cast<int>(levels.size())) {
        return std::nullopt;
    }
    return levels[static_cast<size_t>(row)];
}

void ScreenFlow::restart() {

    switch (watcher.diskState()) {
    case LevelDiskState::Removed:
        showLevelSelect(currentMode);
        return;
    case LevelDiskState::Changed:
        currentOverlay = FlowOverlay::None;
        request(FlowRequestKind::ReloadGame, currentLevel);
        return;
    case LevelDiskState::Unchanged:
        currentOverlay = FlowOverlay::None;
        request(FlowRequestKind::RestartGame, currentLevel);
        return;
    }
}

void ScreenFlow::nextLevel() {
    const std::vector<LevelFiles> available = findLevels(levelFolder());
    const auto current = std::find_if(available.begin(), available.end(), [&](const LevelFiles& files) {
        return currentLevel && files.name == currentLevel->name;
    });
    if (current != available.end() && std::next(current) != available.end()) {
        startGame(currentMode, *std::next(current));
        return;
    }

    showLevelSelect(currentMode);
}

bool ScreenFlow::activate(FlowAction action, int row) {
    switch (currentScreen) {
    case FlowScreen::MainMenu:
        switch (action) {
        case FlowAction::NormalMode:
            showLevelSelect(GameMode::Normal);
            return true;
        case FlowAction::PerformanceMode:
            showLevelSelect(GameMode::Performance);
            return true;
        case FlowAction::MapEditor:
            openEditor(GameMode::Normal, std::nullopt);
            return true;
        case FlowAction::Settings:
            showSettings();
            return true;
        case FlowAction::Quit:
            request(FlowRequestKind::Quit);
            return true;
        default:
            return false;
        }
    case FlowScreen::LevelSelect:
        switch (action) {
        case FlowAction::PlayLevel:
            if (const auto files = levelAt(row)) {
                startGame(currentMode, *files);
                return true;
            }
            return false;
        case FlowAction::EditLevel:
            if (const auto files = levelAt(row)) {
                openEditor(currentMode, *files);
                return true;
            }
            return false;
        case FlowAction::NewLevel:
            openEditor(currentMode, std::nullopt);
            return true;
        case FlowAction::Import:
            request(FlowRequestKind::PickLevelFiles, std::nullopt, levelFolder());
            return true;
        case FlowAction::OpenFolder:
            request(FlowRequestKind::OpenFolder, std::nullopt, levelFolder());
            return true;
        case FlowAction::Back:
            showMainMenu();
            return true;
        default:
            return false;
        }
    case FlowScreen::Gameplay:
        return activateInGame(action);
    case FlowScreen::LoadError:
        if (action == FlowAction::Back) {
            showLevelSelect(currentMode);
            return true;
        }
        return false;
    case FlowScreen::MapEditor:

        if (action == FlowAction::Back) {
            showMainMenu();
            return true;
        }
        return false;
    case FlowScreen::Settings:
        if (action == FlowAction::Back) {
            showMainMenu();
            return true;
        }
        if (action == FlowAction::RunMachineBenchmark) {
            runMachineBenchmark();
            return true;
        }
        return false;
    case FlowScreen::MachineBenchmark:
        if (action == FlowAction::SkipBenchmark && !benchmarkDone) {
            request(FlowRequestKind::SkipBenchmark);
            showMainMenu();
            return true;
        }
        if (action == FlowAction::ContinueFromBenchmark && benchmarkDone) {
            showMainMenu();
            return true;
        }
        return false;
    }
    return false;
}

bool ScreenFlow::activateInGame(FlowAction action) {
    switch (currentOverlay) {
    case FlowOverlay::None:
        if (action == FlowAction::TogglePause) {
            currentOverlay = FlowOverlay::Pause;
            return true;
        }
        return false;
    case FlowOverlay::Pause:
        switch (action) {
        case FlowAction::TogglePause:
        case FlowAction::Resume:
            currentOverlay = FlowOverlay::None;
            return true;
        case FlowAction::Restart:
            restart();
            return true;
        case FlowAction::PauseSettings:
            currentOverlay = FlowOverlay::Settings;
            return true;
        case FlowAction::MainMenu:
            showMainMenu();
            return true;
        default:
            return false;
        }
    case FlowOverlay::Win:
        switch (action) {
        case FlowAction::NextLevel:
            nextLevel();
            return true;
        case FlowAction::Restart:
            restart();
            return true;
        case FlowAction::MainMenu:
            showMainMenu();
            return true;
        default:
            return false;
        }
    case FlowOverlay::Settings:

        if (action == FlowAction::Back) {
            currentOverlay = FlowOverlay::Pause;
            return true;
        }
        return false;
    }
    return false;
}

bool ScreenFlow::escape() {
    switch (currentScreen) {
    case FlowScreen::MainMenu:
        return false;
    case FlowScreen::Gameplay:

        return currentOverlay == FlowOverlay::Settings && activate(FlowAction::Back);
    case FlowScreen::MachineBenchmark:
        return activate(benchmarkDone ? FlowAction::ContinueFromBenchmark : FlowAction::SkipBenchmark);
    case FlowScreen::LevelSelect:
    case FlowScreen::LoadError:
    case FlowScreen::MapEditor:
    case FlowScreen::Settings:
        return activate(FlowAction::Back);
    }
    return false;
}

void ScreenFlow::update(float deltaSeconds, std::chrono::steady_clock::time_point now) {
    currentToast.secondsLeft = std::max(0.0f, currentToast.secondsLeft - deltaSeconds);
    if (highlightSecondsLeft > 0.0f) {
        highlightSecondsLeft = std::max(0.0f, highlightSecondsLeft - deltaSeconds);
        if (highlightSecondsLeft == 0.0f) {
            for (FlowLevelRow& row : rows) {
                row.highlighted = false;
            }
        }
    }
    if (currentScreen == FlowScreen::LevelSelect) {
        refreshSecondsLeft -= deltaSeconds;
        if (refreshSecondsLeft <= 0.0f) {
            refreshLevels();
        }
    }
    if (currentScreen == FlowScreen::MachineBenchmark && benchmarkDone) {
        benchmarkAdvanceLeft -= deltaSeconds;
        if (benchmarkAdvanceLeft <= 0.0f) {
            showMainMenu();
        }
    }
    if ((currentScreen == FlowScreen::Gameplay || currentScreen == FlowScreen::LoadError) && currentLevel) {
        const bool changed = watcher.pollChanged(now);

        if (changed && currentScreen == FlowScreen::LoadError &&
            watcher.diskState() != LevelDiskState::Removed) {
            request(FlowRequestKind::StartGame, currentLevel);
        }
    }
}

std::string ScreenFlow::title() const {
    switch (currentScreen) {
    case FlowScreen::MainMenu:
        return "BALLS";
    case FlowScreen::LevelSelect:
        return modeTitle(currentMode);
    case FlowScreen::LoadError:
        return "Level failed to load";
    case FlowScreen::Settings:
        return "Settings";
    case FlowScreen::MachineBenchmark:
        return "Setting up for this machine";
    case FlowScreen::MapEditor:
        return {};
    case FlowScreen::Gameplay:
        switch (currentOverlay) {
        case FlowOverlay::Pause:
            return "Paused";
        case FlowOverlay::Win:
            return "Level Complete!";
        case FlowOverlay::Settings:
            return "Settings";
        case FlowOverlay::None:
            return {};
        }
    }
    return {};
}

std::vector<FlowButton> ScreenFlow::buttons() const {
    switch (currentScreen) {
    case FlowScreen::MainMenu:
        return {{FlowAction::NormalMode, "Normal Mode"},
                {FlowAction::PerformanceMode, "Performance Mode"},
                {FlowAction::MapEditor, "Map Editor"},
                {FlowAction::Settings, "Settings"},
                {FlowAction::Quit, "Quit"}};
    case FlowScreen::LevelSelect:
        return {{FlowAction::Back, "Back"},
                {FlowAction::NewLevel, "New Level"},
                {FlowAction::Import, "Import"},
                {FlowAction::OpenFolder, "Open Folder"}};
    case FlowScreen::LoadError:
    case FlowScreen::Settings:
        return {{FlowAction::Back, "Back"}};
    case FlowScreen::MachineBenchmark:
        return {benchmarkDone ? FlowButton{FlowAction::ContinueFromBenchmark, "Continue"}
                              : FlowButton{FlowAction::SkipBenchmark, "Skip"}};
    case FlowScreen::MapEditor:
        return {};
    case FlowScreen::Gameplay:
        switch (currentOverlay) {
        case FlowOverlay::Pause:
            return {{FlowAction::Resume, "Resume"},
                    {FlowAction::Restart, "Restart"},
                    {FlowAction::PauseSettings, "Settings"},
                    {FlowAction::MainMenu, "Main Menu"}};
        case FlowOverlay::Win:
            return {{FlowAction::NextLevel, "Next Level"},
                    {FlowAction::Restart, "Restart"},
                    {FlowAction::MainMenu, "Main Menu"}};
        case FlowOverlay::Settings:
            return {{FlowAction::Back, "Back"}};
        case FlowOverlay::None:
            return {};
        }
    }
    return {};
}

std::string ScreenFlow::footnote() const {
    switch (currentScreen) {
    case FlowScreen::LevelSelect:
        return "Levels folder: " + levelFolder().string();
    case FlowScreen::LoadError:
        return "Fix the file and save - it reloads automatically";
    case FlowScreen::MachineBenchmark:
        return "Measuring each physics backend on this machine.";
    default:
        return {};
    }
}

std::string ScreenFlow::gameTitle() const {
    const char* mode = currentMode == GameMode::Normal ? "Normal" : "Performance";
    return std::string(mode) + " - " + (currentLevel ? currentLevel->name : std::string{});
}

std::string_view ScreenFlow::diskNotice() const { return levelDiskNotice(watcher.diskState()); }

void ScreenFlow::showToast(std::string text, bool isError, float seconds) {
    currentToast = {std::move(text), isError, seconds};
}
