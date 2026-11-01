#include "TestFramework.h"

#include "control/ControlApi.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

#include <filesystem>

namespace {

class FakeGame final : public ControlGame {
public:
    explicit FakeGame(GameMode mode) : session(mode, loadLevelNamed(mode), BackendKind::Scalar) {}

    GameMode gameMode() const override { return session.mode(); }
    std::string levelName() const override { return "DA_FirstLevel"; }
    std::string levelLoadError() const override { return {}; }
    LevelDiskState diskState() const override { return LevelDiskState::Unchanged; }
    std::shared_ptr<const SessionView> sessionView() override {
        auto view = std::make_shared<SessionView>();
        describeSession(session, *view);
        return view;
    }
    void post(std::function<void(GameSession&)> command) override { command(session); }
    bool isPaused() const override { return paused; }
    void setPaused(bool pause) override { paused = pause; }
    void restart() override { session.restart(); }
    GameSession session;
    bool paused = false;

private:
    static LevelData loadLevelNamed(GameMode mode) {
        const char* folder = mode == GameMode::Normal ? "normal" : "performance";
        const auto files = findLevels(std::filesystem::path(BALLS_ASSET_DIR) / "levels" / folder);
        auto level = loadLevel(files.front());
        if (!level) {
            throw TestFailure{level.error()};
        }
        return *level;
    }
};

class FakeHost final : public ControlHost {
public:
    ControlFrontEndState frontEndState() const override {
        ControlFrontEndState state;
        state.frontEnd = "test";
        state.screen = gamePlayed ? "gameplay" : "main-menu";
        state.frames = &frames;
        return state;
    }
    ControlGame* game() override { return gamePlayed.get(); }
    std::filesystem::path levelDirectory(GameMode mode) const override {
        return std::filesystem::path(BALLS_ASSET_DIR) / "levels" / (mode == GameMode::Normal ? "normal" : "performance");
    }
    std::optional<ControlResponse> takeScreenshot(const std::string& path) override {
        screenshot = path;
        return std::nullopt;
    }
    void play(GameMode mode, const LevelFiles& level) override {
        played = level.name;
        gamePlayed = std::make_unique<FakeGame>(mode);
    }
    void showMainMenu() override { gamePlayed.reset(); }
    bool showLevelSelect(GameMode) override { return false; }
    bool openEditor(GameMode, const std::optional<LevelFiles>&) override { return false; }
    void quit() override { quitRequested = true; }
    const Settings& settings() const override { return current; }
    void applySettings(const Settings& settings, std::optional<BackendKind>) override { current = settings; }
    void openSettings() override {}
    void setVsync(bool) override {}
    ControlBenchmarkState benchmarkState() const override { return {}; }
    void runBenchmark() override {}
    bool skipBenchmark() override { return false; }
    bool continueFromBenchmark() override { return false; }

    FrameTimings frames;
    Settings current;
    std::unique_ptr<FakeGame> gamePlayed;
    std::string played;
    std::string screenshot;
    bool quitRequested = false;
};

ControlResponse answer(FakeHost& host, std::string_view target) {
    const std::optional<ControlResponse> response = handleControlRequest(host, parseControlTarget(target));
    return response.value_or(ControlResponse{0, ""});
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

}

TEST_CASE("control targets are URL-decoded into a path and parameters") {
    const ControlRequest request = parseControlTarget("/play?mode=normal&level=My%20Level&x=a+b&flag");
    EXPECT(request.path == "/play");
    EXPECT(request.text("level") == "My Level");
    EXPECT(request.text("x") == "a b");
    EXPECT(request.text("flag") == "");
    EXPECT(!request.integer("mode").has_value());
    EXPECT(parseControlTarget("/fill?count=7100000000").count("count") == 7'100'000'000u);
    EXPECT(!parseControlTarget("/fill?count=-1").count("count").has_value());
}

TEST_CASE("the control API plays a level and drives the session") {
    FakeHost host;
    EXPECT(answer(host, "/play?mode=normal&level=nope").status == 404);
    const std::string first = findLevels(host.levelDirectory(GameMode::Normal)).front().name;
    EXPECT(answer(host, "/play?mode=normal&level=" + first).status == 200);
    EXPECT(host.played == first);
    EXPECT(answer(host, "/launch?column=0&row=0").status == 200);
    EXPECT(host.gamePlayed->session.spawner().pendingBalls() > 0);
    EXPECT(answer(host, "/pause").status == 200);
    EXPECT(host.gamePlayed->paused);
    const ControlResponse state = answer(host, "/state");
    EXPECT(state.status == 200);
    EXPECT(contains(state.json, "\"frontend\":\"test\""));
    EXPECT(contains(state.json, "\"paused\":true"));
    EXPECT(contains(state.json, "\"launchers\":[["));
}

TEST_CASE("the control API checks parameters and leaves unknown routes to the front end") {
    FakeHost host;
    EXPECT(answer(host, "/settings?layout=sideways").status == 400);
    EXPECT(answer(host, "/settings?collisions=1").status == 200);
    EXPECT(host.current.ballCollisions);
    EXPECT(answer(host, "/settings?ballCollisions=false").status == 200);
    EXPECT(!host.current.ballCollisions);
    EXPECT(answer(host, "/levels").status == 404);
    EXPECT(answer(host, "/nope").status == 404);
    EXPECT(answer(host, "/screenshot").status == 400);
    EXPECT(answer(host, "/screenshot?path=shot.png").status == 0);
    EXPECT(host.screenshot == "shot.png");
    EXPECT(answer(host, "/quit").status == 200);
    EXPECT(host.quitRequested);
}
