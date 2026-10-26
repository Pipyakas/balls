#include "app/App.h"

#include "editor/MapEditorScreen.h"
#include "level/LevelRepository.h"
#include "render/Palette.h"
#include "screens/GameplayScreen.h"
#include "screens/LevelSelectScreen.h"
#include "screens/MainMenuScreen.h"
#include "ui/Widgets.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>

namespace {

constexpr int minimumWindowWidth = 1024;
constexpr int minimumWindowHeight = 700;
constexpr float maximumFrameSeconds = 0.1f;

using Clock = std::chrono::steady_clock;

double millisecondsBetween(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::optional<LevelFiles> findLevel(GameMode mode, std::string_view name) {
    for (const LevelFiles& files : findLevels(levelDirectory(mode))) {
        if (files.name == name) {
            return files;
        }
    }
    return std::nullopt;
}

ControlResponse saveScreenshot(const std::string& path) {
    if (path.empty()) {
        return ControlResponse::error(400, "missing path");
    }
    Image image = LoadImageFromScreen();
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    const bool saved = ExportImage(image, path.c_str());
    UnloadImage(image);
    return saved ? ControlResponse::ok(std::format("{{\"ok\":true,\"path\":\"{}\"}}", jsonEscape(path)))
                 : ControlResponse::error(500, "could not write screenshot");
}

}

void App::initializeWindow(const LaunchOptions& options) {
    unsigned int flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
    if (options.background) {
        flags |= FLAG_WINDOW_UNFOCUSED;
    }
    if (options.hidden) {
        flags |= FLAG_WINDOW_HIDDEN;
    }
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(options.windowWidth, options.windowHeight, "Balls");
    SetWindowMinSize(minimumWindowWidth, minimumWindowHeight);
    SetExitKey(KEY_NULL);
    SetTargetFPS(0);
    ui::loadFonts();
    balls.load();
    gpuTimer.load();
    setVsync(options.vsync);
    backendChoice = options.backend && isBackendAvailable(*options.backend) ? *options.backend
                                                                            : fastestAvailableCpuBackend();
}

void App::run(const LaunchOptions& options) {
    initializeWindow(options);
    if (options.controlPort > 0) {
        controlServer.start(options.controlPort);
    }
    const auto startLevel = options.playMode ? findLevel(*options.playMode, options.playLevel) : std::nullopt;
    if (startLevel) {
        startGame(*options.playMode, *startLevel);
    } else {
        showMainMenu();
    }
    applyPendingScreen();

    Clock::time_point frameStart = Clock::now();
    while (running && !WindowShouldClose()) {
        const float deltaTime = std::min(GetFrameTime(), maximumFrameSeconds);
        gpuTimer.beginFrame();
        currentScreen->update(deltaTime);
        BeginDrawing();
        ClearBackground(palette::background);
        currentScreen->draw();
        if (!currentScreen->drawsFrameTimings()) {
            drawFrameTimingsOverlay();
        }
        rlDrawRenderBatchActive();
        controlServer.pump([this](const ControlRequest& request) { return handleControl(request); });
        gpuTimer.endFrame();
        const Clock::time_point presentStart = Clock::now();
        EndDrawing();
        const Clock::time_point frameEnd = Clock::now();
        timings.record({millisecondsBetween(frameStart, frameEnd), millisecondsBetween(frameStart, presentStart),
                        gpuTimer.lastFrameMs(), millisecondsBetween(presentStart, frameEnd)});
        frameStart = frameEnd;
        applyPendingScreen();
    }
    controlServer.stop();
    currentScreen.reset();
    pendingScreen.reset();
    balls.unload();
    ui::unloadFonts();
    CloseWindow();
}

void App::setVsync(bool enabled) {
    vsyncEnabled = enabled;
    setSwapInterval(enabled ? 1 : 0);
}

void App::drawFrameTimingsOverlay() const {
    const FrameSample& sample = timings.average();
    const std::string text = std::format("{:.0f} fps   cpu {:.2f}   gpu {:.2f}   present {:.2f} ms",
                                         timings.framesPerSecond(), sample.cpuMs, sample.gpuMs, sample.presentMs);
    const Vector2 extent = ui::measureText(text, 18.0f);
    const float x = 26.0f;
    const float y = static_cast<float>(GetScreenHeight()) - extent.y - 10.0f;
    DrawRectangleRounded({x - 10.0f, y - 4.0f, extent.x + 20.0f, extent.y + 8.0f}, 0.4f, 6,
                         Fade(palette::backgroundDeep, 0.85f));
    ui::drawText(text, {x, y}, 18.0f, palette::textMuted);
}

void App::applyPendingScreen() {
    if (pendingScreen) {
        currentScreen = std::move(pendingScreen);
    }
}

std::string App::stateJson() const {
    const FrameSample& sample = timings.average();
    return std::format("{{\"ok\":true,\"screen\":\"{}\",\"window\":[{},{}],\"fps\":{:.1f},\"minFps\":{:.1f},"
                       "\"frameMs\":{:.3f},\"cpuMs\":{:.3f},\"gpuMs\":{:.3f},\"presentMs\":{:.3f},"
                       "\"preferredBackend\":\"{}\",\"vsync\":{},\"details\":{}}}",
                       currentScreen->name(), GetScreenWidth(), GetScreenHeight(), timings.framesPerSecond(),
                       timings.worstFpsLastSecond(), sample.frameMs, sample.cpuMs, sample.gpuMs, sample.presentMs,
                       backendKindName(backendChoice), vsyncEnabled, currentScreen->stateJson());
}

ControlResponse App::handleControl(const ControlRequest& request) {
    const std::string& path = request.path;
    if (path == "/state") {
        return ControlResponse::ok(stateJson());
    }
    if (path == "/screenshot") {
        return saveScreenshot(request.text("path").value_or(""));
    }
    if (path == "/quit") {
        quit();
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (path == "/menu") {
        showMainMenu();
        return ControlResponse::ok("{\"ok\":true}");
    }
    const std::optional<GameMode> mode = parseGameMode(request.text("mode").value_or("normal"));
    if (path == "/levels" || path == "/play" || path == "/editor") {
        if (!mode) {
            return ControlResponse::error(400, "mode must be normal or performance");
        }
        const std::string levelName = request.text("level").value_or("");
        const std::optional<LevelFiles> level = levelName.empty() ? std::nullopt : findLevel(*mode, levelName);
        if (path == "/levels") {
            showLevelSelect(*mode);
        } else if (path == "/editor") {
            openEditor(*mode, level);
        } else if (!level) {
            return ControlResponse::error(404, std::format("no level '{}' in {} mode", levelName,
                                                           *mode == GameMode::Normal ? "normal" : "performance"));
        } else {
            startGame(*mode, *level);
        }
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (path == "/vsync") {
        setVsync(request.integer("on").value_or(vsyncEnabled ? 0 : 1) != 0);
        return ControlResponse::ok(std::format("{{\"ok\":true,\"vsync\":{}}}", vsyncEnabled));
    }
    if (path == "/window") {
        const int width = request.integer("width").value_or(GetScreenWidth());
        const int height = request.integer("height").value_or(GetScreenHeight());
        SetWindowSize(std::max(width, minimumWindowWidth), std::max(height, minimumWindowHeight));
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (auto response = currentScreen->handleControl(request)) {
        return *response;
    }
    return ControlResponse::error(404, std::format("unknown command {} on screen {}", path, currentScreen->name()));
}

void App::showMainMenu() { pendingScreen = std::make_unique<MainMenuScreen>(*this); }

void App::showLevelSelect(GameMode mode) { pendingScreen = std::make_unique<LevelSelectScreen>(*this, mode); }

void App::startGame(GameMode mode, const LevelFiles& files) {
    pendingScreen = std::make_unique<GameplayScreen>(*this, mode, files);
}

void App::openEditor(GameMode mode, std::optional<LevelFiles> files) {
    pendingScreen = std::make_unique<MapEditorScreen>(*this, mode, std::move(files));
}
