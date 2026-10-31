#include "TestFramework.h"

#include "level/LevelRepository.h"
#include "ui/ScreenFlow.h"

#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

using namespace std::chrono_literals;

constexpr std::string_view sampleLevelPrefix = "DA_";

// Copies of the samples' two Normal levels and one Performance level in a scratch folder per test, so the
// tests don't change when levels are added.
struct FlowFolders {
    std::filesystem::path root;
    std::filesystem::path normal;
    std::filesystem::path performance;

    explicit FlowFolders(const char* name)
        : root(std::filesystem::temp_directory_path() / name), normal(root / "normal"),
          performance(root / "performance") {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(normal);
        std::filesystem::create_directories(performance);
        const std::filesystem::path assets = std::filesystem::path(BALLS_ASSET_DIR) / "levels";
        for (const auto& entry : std::filesystem::directory_iterator(assets / "normal")) {
            if (entry.path().filename().string().starts_with(sampleLevelPrefix)) {
                std::filesystem::copy_file(entry.path(), normal / entry.path().filename());
            }
        }
        const LevelFiles firstPerformance = findLevels(assets / "performance").front();
        std::filesystem::copy_file(firstPerformance.bricksPath, performance / firstPerformance.bricksPath.filename());
        std::filesystem::copy_file(firstPerformance.launchersPath,
                                   performance / firstPerformance.launchersPath.filename());
    }
    ~FlowFolders() { std::filesystem::remove_all(root); }
};

std::vector<FlowRequestKind> drainRequests(ScreenFlow& flow) {
    std::vector<FlowRequestKind> kinds;
    while (const auto request = flow.takeRequest()) {
        kinds.push_back(request->kind);
    }
    return kinds;
}

bool hasButton(const ScreenFlow& flow, FlowAction action, std::string_view label) {
    for (const FlowButton& button : flow.buttons()) {
        if (button.action == action && button.label == label) {
            return true;
        }
    }
    return false;
}

// Starts the first Normal level and reports it loaded, as a front end would.
void playFirstNormalLevel(ScreenFlow& flow) {
    flow.activate(FlowAction::NormalMode);
    flow.activate(FlowAction::PlayLevel, 0);
    drainRequests(flow);
    flow.levelLoaded();
}

} // namespace

TEST_CASE("the main menu leads to each mode's level select and Back returns") {
    FlowFolders folders("balls_flow_menu");
    ScreenFlow flow(folders.normal, folders.performance);
    EXPECT(flow.screen() == FlowScreen::MainMenu);
    EXPECT(flow.title() == "BALLS");
    EXPECT(flow.buttons().size() == 5);
    EXPECT(!flow.escape());

    EXPECT(flow.activate(FlowAction::PerformanceMode));
    EXPECT(flow.screen() == FlowScreen::LevelSelect);
    EXPECT(flow.title() == "Performance Mode");
    EXPECT(flow.levelRows().size() == 1);
    EXPECT(flow.footnote() == "Levels folder: " + folders.performance.string());
    EXPECT(flow.escape());
    EXPECT(flow.screen() == FlowScreen::MainMenu);

    EXPECT(flow.activate(FlowAction::NormalMode));
    EXPECT(flow.levelRows().size() == 2);
    EXPECT(!flow.activate(FlowAction::PlayLevel, 5));
    EXPECT(flow.activate(FlowAction::Back));
    EXPECT(flow.screen() == FlowScreen::MainMenu);
    EXPECT(flow.activate(FlowAction::Quit));
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::Quit});
}

TEST_CASE("a level that fails to load shows the load error, whose Back returns to level select") {
    FlowFolders folders("balls_flow_load_error");
    ScreenFlow flow(folders.normal, folders.performance);
    flow.activate(FlowAction::NormalMode);
    EXPECT(flow.activate(FlowAction::PlayLevel, 0));
    EXPECT(flow.screen() == FlowScreen::Gameplay);
    const auto start = flow.takeRequest();
    EXPECT(start && start->kind == FlowRequestKind::StartGame && start->files);
    EXPECT(start->files->name == flow.level()->name);

    flow.levelFailed("bad brick");
    EXPECT(flow.screen() == FlowScreen::LoadError);
    EXPECT(flow.loadError() == "bad brick");
    EXPECT(hasButton(flow, FlowAction::Back, "Back"));
    EXPECT(flow.escape());
    EXPECT(flow.screen() == FlowScreen::LevelSelect);
    EXPECT(flow.mode() == GameMode::Normal);
    // The game never ran, so there is nothing to end.
    EXPECT(drainRequests(flow).empty());
}

TEST_CASE("the header pauses, the pause overlay holds settings and Esc only closes that panel") {
    FlowFolders folders("balls_flow_pause");
    ScreenFlow flow(folders.normal, folders.performance);
    playFirstNormalLevel(flow);
    EXPECT(!flow.escape());
    EXPECT(!flow.isPaused());

    EXPECT(flow.activate(FlowAction::TogglePause));
    EXPECT(flow.overlay() == FlowOverlay::Pause);
    EXPECT(flow.title() == "Paused");
    EXPECT(flow.buttons().size() == 4);
    EXPECT(!flow.escape());

    EXPECT(flow.activate(FlowAction::PauseSettings));
    EXPECT(flow.overlay() == FlowOverlay::Settings);
    EXPECT(flow.isPaused());
    EXPECT(!flow.canRunMachineBenchmark());
    EXPECT(flow.escape());
    EXPECT(flow.overlay() == FlowOverlay::Pause);

    EXPECT(flow.activate(FlowAction::TogglePause));
    EXPECT(flow.overlay() == FlowOverlay::None);
    EXPECT(flow.activate(FlowAction::TogglePause));
    EXPECT(flow.activate(FlowAction::Restart));
    EXPECT(flow.overlay() == FlowOverlay::None);
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::RestartGame});

    flow.activate(FlowAction::TogglePause);
    EXPECT(flow.activate(FlowAction::MainMenu));
    EXPECT(flow.screen() == FlowScreen::MainMenu);
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::EndGame});
}

TEST_CASE("winning a Normal level offers the next level, and after the last one level select") {
    FlowFolders folders("balls_flow_win");
    ScreenFlow flow(folders.normal, folders.performance);
    playFirstNormalLevel(flow);
    const std::string first = flow.level()->name;
    flow.sessionStateChanged(SessionState::Won);
    EXPECT(flow.overlay() == FlowOverlay::Win);
    EXPECT(flow.title() == "Level Complete!");
    EXPECT(hasButton(flow, FlowAction::NextLevel, "Next Level"));
    EXPECT(!flow.activate(FlowAction::PauseSettings));

    EXPECT(flow.activate(FlowAction::NextLevel));
    EXPECT(flow.screen() == FlowScreen::Gameplay);
    EXPECT(flow.level()->name != first);
    EXPECT((drainRequests(flow) == std::vector{FlowRequestKind::EndGame, FlowRequestKind::StartGame}));
    flow.levelLoaded();

    flow.sessionStateChanged(SessionState::Won);
    EXPECT(flow.activate(FlowAction::NextLevel));
    EXPECT(flow.screen() == FlowScreen::LevelSelect);
}

TEST_CASE("Performance mode has no win overlay") {
    FlowFolders folders("balls_flow_performance_win");
    ScreenFlow flow(folders.normal, folders.performance);
    flow.activate(FlowAction::PerformanceMode);
    flow.activate(FlowAction::PlayLevel, 0);
    flow.levelLoaded();
    flow.sessionStateChanged(SessionState::Won);
    EXPECT(flow.overlay() == FlowOverlay::None);
    EXPECT(flow.gameTitle().rfind("Performance - ", 0) == 0);
}

TEST_CASE("Restart reloads changed files and leaves for level select once they are removed") {
    FlowFolders folders("balls_flow_restart");
    ScreenFlow flow(folders.normal, folders.performance);
    playFirstNormalLevel(flow);
    const LevelFiles files = *flow.level();
    auto now = std::chrono::steady_clock::now();
    const auto settle = [&] {
        for (int step = 0; step < 4; ++step) {
            now += 300ms;
            flow.update(0.3f, now);
        }
    };
    settle();
    EXPECT(flow.diskNotice().empty());

    std::filesystem::last_write_time(files.bricksPath, std::filesystem::last_write_time(files.bricksPath) + 5s);
    settle();
    EXPECT(flow.diskState() == LevelDiskState::Changed);
    EXPECT(!flow.diskNotice().empty());
    flow.activate(FlowAction::TogglePause);
    EXPECT(flow.activate(FlowAction::Restart));
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::ReloadGame});
    flow.levelLoaded();
    EXPECT(flow.diskState() == LevelDiskState::Unchanged);

    std::filesystem::remove(files.launchersPath);
    settle();
    EXPECT(flow.diskState() == LevelDiskState::Removed);
    flow.activate(FlowAction::TogglePause);
    EXPECT(flow.activate(FlowAction::Restart));
    EXPECT(flow.screen() == FlowScreen::LevelSelect);
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::EndGame});
}

TEST_CASE("the load error retries when the files change again") {
    FlowFolders folders("balls_flow_retry");
    ScreenFlow flow(folders.normal, folders.performance);
    flow.activate(FlowAction::NormalMode);
    flow.activate(FlowAction::PlayLevel, 0);
    drainRequests(flow);
    flow.levelFailed("broken");
    const LevelFiles files = *flow.level();
    auto now = std::chrono::steady_clock::now();
    std::filesystem::last_write_time(files.bricksPath, std::filesystem::last_write_time(files.bricksPath) + 5s);
    for (int step = 0; step < 4; ++step) {
        now += 300ms;
        flow.update(0.3f, now);
    }
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::StartGame});
    flow.levelLoaded();
    EXPECT(flow.screen() == FlowScreen::Gameplay);
}

#if defined(BALLS_FIXED_TARGET)
TEST_CASE("the scoring build never offers the machine benchmark") {
    FlowFolders folders("balls_flow_benchmark");
    ScreenFlow flow(folders.normal, folders.performance);
    flow.activate(FlowAction::Settings);
    EXPECT(!flow.canRunMachineBenchmark());
    EXPECT(!flow.activate(FlowAction::RunMachineBenchmark));
    EXPECT(flow.screen() == FlowScreen::Settings);
}
#else
TEST_CASE("the machine benchmark offers Skip, then Continue, and moves on by itself") {
    FlowFolders folders("balls_flow_benchmark");
    ScreenFlow flow(folders.normal, folders.performance);
    flow.activate(FlowAction::Settings);
    EXPECT(flow.canRunMachineBenchmark());
    EXPECT(flow.activate(FlowAction::RunMachineBenchmark));
    EXPECT(flow.screen() == FlowScreen::MachineBenchmark);
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::StartBenchmark});
    EXPECT(hasButton(flow, FlowAction::SkipBenchmark, "Skip"));
    EXPECT(!flow.activate(FlowAction::ContinueFromBenchmark));

    flow.benchmarkFinished();
    EXPECT(hasButton(flow, FlowAction::ContinueFromBenchmark, "Continue"));
    flow.update(1.0f);
    EXPECT(flow.screen() == FlowScreen::MachineBenchmark);
    flow.update(0.6f);
    EXPECT(flow.screen() == FlowScreen::MainMenu);

    flow.runMachineBenchmark();
    drainRequests(flow);
    EXPECT(flow.escape());
    EXPECT(flow.screen() == FlowScreen::MainMenu);
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::SkipBenchmark});
}
#endif

TEST_CASE("imports land in the open level select and highlight the new level for two seconds") {
    FlowFolders folders("balls_flow_import");
    ScreenFlow flow(folders.normal, folders.performance);
    const std::filesystem::path source = folders.root / "picked";
    std::filesystem::create_directories(source);
    const LevelFiles existing = findLevels(folders.normal).front();
    std::filesystem::copy_file(existing.bricksPath, source / "Imported_Bricks.txt");
    std::filesystem::copy_file(existing.launchersPath, source / "Imported_SpawnBall.txt");
    const std::vector<std::filesystem::path> picked{source / "Imported_Bricks.txt"};

    flow.importLevels(picked);
    EXPECT(flow.toast().text == "Open Level select to import levels");
    EXPECT(flow.toast().secondsLeft > 0.0f);

    flow.activate(FlowAction::NormalMode);
    flow.importLevels(picked);
    EXPECT(!flow.toast().isError);
    EXPECT(flow.levelRows().size() == 3);
    const auto row = flow.takeScrollToRow();
    EXPECT(row.has_value());
    EXPECT(flow.levelRows()[static_cast<size_t>(*row)].name == "Imported");
    EXPECT(flow.levelRows()[static_cast<size_t>(*row)].highlighted);
    EXPECT(!flow.takeScrollToRow());
    flow.update(2.1f);
    EXPECT(!flow.levelRows()[static_cast<size_t>(*row)].highlighted);
}

TEST_CASE("the editor opens on a level or a new one and leaves for the main menu") {
    FlowFolders folders("balls_flow_editor");
    ScreenFlow flow(folders.normal, folders.performance);
    flow.activate(FlowAction::PerformanceMode);
    EXPECT(flow.activate(FlowAction::EditLevel, 0));
    EXPECT(flow.screen() == FlowScreen::MapEditor);
    const auto open = flow.takeRequest();
    EXPECT(open && open->kind == FlowRequestKind::OpenEditor && open->files && open->mode == GameMode::Performance);

    flow.playFromEditor(GameMode::Performance, *open->files);
    EXPECT(flow.screen() == FlowScreen::Gameplay);
    EXPECT(drainRequests(flow) == std::vector{FlowRequestKind::StartGame});

    flow.showMainMenu();
    EXPECT(flow.activate(FlowAction::MapEditor));
    const auto fresh = flow.takeRequest();
    EXPECT(fresh && fresh->kind == FlowRequestKind::OpenEditor && !fresh->files && fresh->mode == GameMode::Normal);
    EXPECT(flow.escape());
    EXPECT(flow.screen() == FlowScreen::MainMenu);
}

TEST_CASE("a level loaded after a repair says so in a warning toast that stays longer") {
    FlowFolders folders("balls_flow_repaired");
    ScreenFlow flow(folders.normal, folders.performance);
    playFirstNormalLevel(flow);
    flow.levelLoaded("Loose does not follow the map format and was repaired: no PlayableHeight; used 9");
    EXPECT(flow.toast().text == "Loose does not follow the map format and was repaired: no PlayableHeight; used 9");
    EXPECT(!flow.toast().isError);
    EXPECT(flow.toast().secondsLeft == theme::repairToastSeconds);
}
