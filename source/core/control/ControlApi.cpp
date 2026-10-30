#include "control/ControlApi.h"

#include "common/BallColor.h"
#include "level/LevelRepository.h"
#include "sim/BackendSelection.h"
#include "sim/BackendWorker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace {

constexpr std::string_view knownPaths[] = {
    "/state",  "/screenshot", "/play",  "/levels", "/editor", "/menu",      "/quit",           "/settings",
    "/vsync",  "/benchmark",  "/benchmark/skip", "/benchmark/continue", "/launch", "/launcher", "/start",
    "/rate",   "/fill",       "/limit", "/multiplier", "/backend", "/pause",   "/resume", "/restart",
};

std::string_view candidateStatusName(CandidateStatus status) {
    switch (status) {
    case CandidateStatus::Pending:
        return "pending";
    case CandidateStatus::Running:
        return "running";
    case CandidateStatus::Done:
        return "done";
    case CandidateStatus::Failed:
        return "failed";
    case CandidateStatus::Unavailable:
        return "unavailable";
    }
    return "pending";
}

bool equalsIgnoringCase(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

// "1"/"0" as SDL takes them, and "true"/"false" as Unreal did.
std::optional<bool> flag(const ControlRequest& request, std::string_view key) {
    const std::optional<std::string> value = request.text(key);
    if (!value) {
        return std::nullopt;
    }
    if (equalsIgnoringCase(*value, "true") || equalsIgnoringCase(*value, "on")) {
        return true;
    }
    if (equalsIgnoringCase(*value, "false") || equalsIgnoringCase(*value, "off")) {
        return false;
    }
    return request.integer(key).value_or(0) != 0;
}

std::optional<LevelFiles> findLevelNamed(const ControlHost& host, GameMode mode, std::string_view name) {
    for (const LevelFiles& files : findLevels(host.levelDirectory(mode))) {
        if (files.name == name) {
            return files;
        }
    }
    return std::nullopt;
}


std::string stateJson(ControlHost& host) {
    const ControlFrontEndState front = host.frontEndState();
    static const FrameTimings noFrames;
    const FrameTimings& frames = front.frames ? *front.frames : noFrames;
    const FrameSample& sample = frames.average();
    ControlGame* game = host.game();
    std::shared_ptr<const SessionView> view = game ? game->sessionView() : nullptr;
    const BackendKind active = view ? view->backend : front.preferredBackend;
    const std::string details = game ? controlGameJson(*game)
                                     : front.screenDetails.empty() ? std::string("{}") : front.screenDetails;
    return std::format(
        "{{\"ok\":true,\"frontend\":\"{}\",\"renderer\":\"{}\",\"screen\":\"{}\",\"window\":[{},{}],"
        "\"fps\":{:.1f},\"minFps\":{:.1f},\"workFps\":{:.1f},\"minWorkFps\":{:.1f},\"shownFps\":{:.1f},"
        "\"frameMs\":{:.3f},\"cpuMs\":{:.3f},\"gpuMs\":{:.3f},\"renderMs\":{:.3f},\"presentMs\":{:.3f},"
        "\"preferredBackend\":\"{}\",\"backend\":\"{}\",\"vsync\":{},\"toast\":\"{}\",{}{}\"details\":{}}}",
        front.frontEnd, jsonEscape(front.renderer), front.screen, front.windowWidth, front.windowHeight,
        frames.framesPerSecond(), frames.worstFpsLastSecond(), frames.workFramesPerSecond(),
        frames.worstWorkFpsLastSecond(), frames.shownFramesPerSecond(), sample.frameMs, sample.cpuMs, sample.gpuMs,
        sample.renderMs, sample.presentMs, backendKindName(front.preferredBackend), backendKindName(active),
        front.vsync, jsonEscape(front.toast), front.extraFields, front.extraFields.empty() ? "" : ",", details);
}

std::optional<ControlResponse> handleSettings(ControlHost& host, const ControlRequest& request) {
    Settings next = host.settings();
    if (const auto collisions = flag(request, "collisions")) {
        next.ballCollisions = *collisions;
    } else if (const auto legacy = flag(request, "ballCollisions")) {
        next.ballCollisions = *legacy;
    }
    if (const auto layout = request.text("layout")) {
        const auto orientation = parseOrientation(*layout);
        if (!orientation) {
            return ControlResponse::error(400, "layout must be auto, landscape or portrait");
        }
        next.layout = *orientation;
    }
    if (const auto vsync = flag(request, "vsync")) {
        next.vsync = *vsync;
    }
    if (const auto advanced = flag(request, "advanced")) {
        next.advancedSettings = *advanced;
    }
    if (const auto renderer = request.text("renderer")) {
        next.renderer = normalizeRendererName(*renderer);
    }
    if (const auto api = request.text("graphicsApi")) {
        next.graphicsApi = normalizeGraphicsApiName(*api);
    } else if (const auto shortApi = request.text("api")) {
        next.graphicsApi = normalizeGraphicsApiName(*shortApi);
    }
    std::optional<BackendKind> backend;
    if (const auto name = request.text("backend")) {
        backend = parseBackendKind(*name);
        if (!backend || !isBackendAvailable(*backend)) {
            return ControlResponse::error(400, std::format("backend {} is not available", *name));
        }
    }
    host.applySettings(next, backend);
    if (flag(request, "open").value_or(false)) {
        host.openSettings();
    }
    return ControlResponse::ok(controlSettingsJson(host.settings(), host.frontEndState().preferredBackend));
}

std::optional<ControlResponse> handleGameRoute(ControlGame& game, const ControlRequest& request) {
    const std::string& path = request.path;
    const ControlResponse done = ControlResponse::ok();
    if (path == "/pause" || path == "/resume") {
        game.setPaused(path == "/pause");
        return done;
    }
    if (path == "/restart") {
        game.restart();
        return done;
    }
    const std::shared_ptr<const SessionView> view = game.sessionView();
    if (!view) {
        return ControlResponse::error(409, game.levelLoadError().empty() ? "the level is still loading" : "the level did not load");
    }
    if (path == "/launch" || path == "/launcher") {
        const int column = request.integer("column").value_or(0);
        const int row = request.integer("row").value_or(0);
        if (!view->launchers.visible(column, row) || view->state == SessionState::Won) {
            return ControlResponse::error(409, "no launcher there or game not running");
        }
        game.post([column, row](GameSession& session) { session.chooseLauncher(column, row); });
        return done;
    }
    if (path == "/start") {
        game.post([](GameSession& session) { session.startAutoRelease(); });
        return done;
    }
    if (path == "/rate") {
        const int index = request.integer("index").value_or(view->spawnRateIndex);
        game.post([index](GameSession& session) { session.selectSpawnRate(index); });
        return done;
    }
    if (path == "/fill") {
        // Without a count, /fill is Auto: up to the backend's capacity, the most balls this machine holds.
        if (!request.text("count")) {
            game.post([](GameSession& session) { session.fillToBallLimit(); });
            return done;
        }
        const std::optional<uint64_t> count = request.count("count");
        if (!count) {
            return ControlResponse::error(400, "count must be a whole number of balls");
        }
        game.post([count = static_cast<BallCount>(*count)](GameSession& session) { session.fillWithBalls(count); });
        return done;
    }
    if (path == "/limit") {
        const int index = request.integer("index").value_or(view->ballLimitIndex);
        game.post([index](GameSession& session) {
            session.cancelFill();
            session.selectBallLimit(index);
        });
        return done;
    }
    if (path == "/multiplier") {
        const int index = request.integer("index").value_or(view->ballMultiplierIndex);
        game.post([index](GameSession& session) { session.selectBallMultiplier(index); });
        return done;
    }
    return std::nullopt;
}

} // namespace

std::span<const std::string_view> controlApiPaths() { return knownPaths; }

std::string controlSettingsJson(const Settings& settings, BackendKind preferredBackend) {
    return std::format(
        "{{\"ok\":true,\"ballCollisions\":{},\"layout\":\"{}\",\"vsync\":{},\"defaultBackend\":\"{}\",\"backend\":\"{}\","
        "\"renderer\":\"{}\",\"graphicsApi\":\"{}\",\"advancedSettings\":{}}}",
        settings.ballCollisions, orientationName(settings.layout), settings.vsync, jsonEscape(settings.defaultBackend),
        backendKindName(preferredBackend), normalizeRendererName(settings.renderer),
        normalizeGraphicsApiName(settings.graphicsApi), settings.advancedSettings);
}

std::string controlBenchmarkJson(const ControlBenchmarkState& benchmark) {
    std::string candidates;
    for (const CandidateResult& result : benchmark.results) {
        candidates += std::format(
            "{}{{\"backend\":\"{}\",\"picks\":\"{}\",\"label\":\"{}\",\"computeGpu\":\"{}\",\"status\":\"{}\","
            "\"ballStepsPerSecond\":{:.0f},\"estimatedBalls\":{:.0f},\"ballCapacity\":{:.0f},\"progress\":{:.2f},"
            "\"failure\":\"{}\"}}",
            candidates.empty() ? "" : ",", backendKindName(result.candidate.kind), jsonEscape(result.candidate.picks),
            jsonEscape(result.candidate.label), jsonEscape(result.candidate.computeGpu),
            candidateStatusName(result.status), result.ballStepsPerSecond, result.estimatedBalls, result.ballCapacity,
            result.progress, jsonEscape(result.failure));
    }
    const std::string winner =
        benchmark.winner ? std::format("\"{}\"", jsonEscape(benchmark.winner->label)) : std::string("null");
    return std::format("{{\"ok\":true,\"state\":\"{}\",\"winner\":{},\"results\":[{}]}}", benchmark.state, winner,
                       candidates);
}

std::string controlGameJson(ControlGame& game) {
    if (!game.levelLoadError().empty()) {
        return std::format("{{\"loadError\":\"{}\"}}", jsonEscape(game.levelLoadError()));
    }
    const std::shared_ptr<const SessionView> view = game.sessionView();
    if (!view) {
        return "{\"loading\":true}";
    }
    const SimulationStats& stats = view->stats;
    std::string ballsByColor;
    for (int color = 0; color < ballColorCount; ++color) {
        ballsByColor += std::format("{}\"{}\":{}", color == 0 ? "" : ",", ballColorName(ballColorFromIndex(color)),
                                    stats.ballsByColor[color]);
    }
    std::string launcherColumns;
    const LauncherQueue& launchers = view->launchers;
    for (int column = 0; column < launchers.columnCount(); ++column) {
        launcherColumns += column == 0 ? "[" : ",[";
        for (int row = 0; row < launchers.visibleRows(); ++row) {
            const QueuedLauncher* queued = launchers.visible(column, row);
            if (!queued) {
                break;
            }
            launcherColumns += std::format("{}{{\"color\":\"{}\",\"balls\":{}}}", row == 0 ? "" : ",",
                                           ballColorName(queued->launcher.color), queued->launcher.ballCount);
        }
        launcherColumns += "]";
    }
    constexpr std::array<const char*, 3> stateNames{"ready", "running", "won"};
    return std::format(
        "{{\"mode\":\"{}\",\"level\":\"{}\",\"diskNotice\":\"{}\",\"state\":\"{}\",\"paused\":{},\"backend\":\"{}\","
        "\"pendingBackend\":\"{}\",\"refilling\":{},\"backendFault\":\"{}\",\"collisions\":{},"
        "\"balls\":{},\"bricks\":{},\"launchersLeft\":{},\"pendingSpawn\":{},\"spawned\":{},\"hits\":{},"
        "\"physicsMs\":{:.3f},\"kernelMs\":{:.3f},\"ticks\":{},\"spawnRate\":{},\"multiplier\":{},"
        "\"ballLimit\":{},\"elapsed\":{:.2f},\"physicsFps\":{:.1f},\"physicsFrameMs\":{:.3f},"
        "\"physicsLongestFrameMs\":{:.3f},\"physicsSlowFrames\":{},"
        "\"ballsByColor\":{{{}}},\"launchers\":[{}]}}",
        gameModeName(game.gameMode()), jsonEscape(game.levelName()), levelDiskNotice(game.diskState()),
        stateNames[static_cast<int>(view->state)], game.isPaused(), backendKindName(view->backend),
        view->pendingBackend ? backendKindName(*view->pendingBackend) : std::string_view{}, view->refilling,
        jsonEscape(view->backendFault), view->collisionsActive, stats.ballCount, view->board ? view->board->aliveBrickCount() : 0,
        launchers.launcherCount(), view->pendingSpawn, stats.totalBallsSpawned, stats.totalHits,
        stats.lastFramePhysicsMs, view->deviceStepMs, stats.ticksLastFrame, spawnRateOptions[view->spawnRateIndex],
        ballMultiplierOptions[view->ballMultiplierIndex], view->ballLimit, view->elapsedSeconds,
        view->physicsFramesPerSecond, view->physicsFrameMs, view->physicsLongestFrameMs, view->physicsSlowFrames,
        ballsByColor, launcherColumns);
}

std::optional<ControlResponse> handleControlRequest(ControlHost& host, const ControlRequest& request) {
    const std::string& path = request.path;
    if (path == "/state") {
        return ControlResponse::ok(stateJson(host));
    }
    if (path == "/screenshot") {
        const std::string target = request.text("path").value_or("");
        if (target.empty()) {
            return ControlResponse::error(400, "missing path");
        }
        return host.takeScreenshot(target);
    }
    if (path == "/menu") {
        host.showMainMenu();
        return ControlResponse::ok();
    }
    if (path == "/quit") {
        host.quit();
        return ControlResponse::ok();
    }
    if (path == "/settings") {
        return handleSettings(host, request);
    }
    if (path == "/vsync") {
        host.setVsync(flag(request, "on").value_or(!host.frontEndState().vsync));
        return ControlResponse::ok(std::format("{{\"ok\":true,\"vsync\":{}}}", host.frontEndState().vsync));
    }
    if (path == "/benchmark") {
        if (flag(request, "run").value_or(false)) {
            host.runBenchmark();
        }
        return ControlResponse::ok(controlBenchmarkJson(host.benchmarkState()));
    }
    if (path == "/benchmark/skip" || path == "/benchmark/continue") {
        const bool applied = path == "/benchmark/skip" ? host.skipBenchmark() : host.continueFromBenchmark();
        return applied ? ControlResponse::ok() : ControlResponse::error(409, "the benchmark is not at that step");
    }
    if (path == "/play" || path == "/levels" || path == "/editor") {
        const std::optional<GameMode> mode = parseGameMode(request.text("mode").value_or("normal"));
        if (!mode) {
            return ControlResponse::error(400, "mode must be normal or performance");
        }
        const std::string levelName = request.text("level").value_or("");
        const std::optional<LevelFiles> level = levelName.empty() ? std::nullopt : findLevelNamed(host, *mode, levelName);
        if (levelName.empty()) {
            if (path == "/play") {
                return ControlResponse::error(400, "missing level");
            }
            if (path == "/levels") {
                return host.showLevelSelect(*mode) ? ControlResponse::ok() : ControlResponse::error(404, "no level select screen");
            }
            return host.openEditor(*mode, std::nullopt) ? ControlResponse::ok() : ControlResponse::error(404, "no editor screen");
        }
        if (!level) {
            return ControlResponse::error(404, std::format("level {} not found", levelName));
        }
        if (path == "/play") {
            host.play(*mode, *level);
            return ControlResponse::ok();
        }
        if (path == "/editor") {
            return host.openEditor(*mode, level) ? ControlResponse::ok() : ControlResponse::error(404, "no editor screen");
        }
    }
    if (ControlGame* game = host.game()) {
        if (const auto handled = handleGameRoute(*game, request)) {
            return handled;
        }
    }
    if (const auto own = host.handleOwnRoute(request)) {
        return own;
    }
    return ControlResponse::error(404, std::format("unknown route {}", path));
}
