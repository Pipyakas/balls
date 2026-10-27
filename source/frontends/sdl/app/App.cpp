#include "app/App.h"

#include "app/Input.h"
#include "app/PreferenceDirectory.h"
#include "editor/MapEditorScreen.h"
#include "game/Settings.h"
#include "level/LevelRepository.h"
#include "render/Palette.h"
#include "screens/GameplayScreen.h"
#include "screens/LevelSelectScreen.h"
#include "screens/MachineBenchmarkScreen.h"
#include "screens/MainMenuScreen.h"
#include "screens/SettingsScreen.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace {

constexpr int minimumWindowWidth = 480;
constexpr int minimumWindowHeight = 700;
constexpr int landscapeWindowWidth = 1440;
constexpr int landscapeWindowHeight = 900;
constexpr float portraitAspectWidth = 9.0f;
constexpr float portraitAspectHeight = 16.0f;
constexpr float portraitHeightShare = 0.9f;
constexpr float landscapeAspectWidth = 16.0f;
constexpr float landscapeAspectHeight = 10.0f;
constexpr float maximumFrameSeconds = 0.1f;
constexpr float fontLoadPixels = 32.0f;
constexpr float channelScale = 1.0f / 255.0f;
constexpr const char* windowTitle = "Balls";
constexpr const char* screenshotExtension = ".bmp";

double millisecondsBetween(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
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

std::string_view gameVersion() {
    const char* version = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING);
    return version ? version : "";
}

// The command line wins, then the Settings default (the benchmark winner or the player's pick), then the fastest CPU
// backend, skipping any that cannot load on this machine.
BackendKind startupBackend(std::optional<BackendKind> commandLine, std::string_view settingsDefault) {
    if (commandLine && isBackendAvailable(*commandLine)) {
        return *commandLine;
    }
    if (const auto saved = parseBackendKind(settingsDefault); saved && isBackendAvailable(*saved)) {
        return *saved;
    }
    return fastestAvailableCpuBackend();
}

std::string screenshotFileFor(const std::string& requested) {
    std::filesystem::path path(requested);
    path.replace_extension(screenshotExtension);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return path.string();
}

} // namespace

int currentRefreshRate() {
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    return mode && mode->refresh_rate > 0.0f ? static_cast<int>(mode->refresh_rate + 0.5f) : 0;
}

App::~App() {
    controlServer.stop();
    currentScreen.reset();
    pendingScreen.reset();
    balls.reset();
    renderer.reset();
    if (imguiCreated) {
        ImGui::DestroyContext();
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
}

bool App::openWindowAndRenderer(const LaunchOptions& options, std::string_view api, std::string& error) {
    renderer.reset();
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | rendererWindowFlags(api);
    if (options.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (options.background) {
        flags |= SDL_WINDOW_NOT_FOCUSABLE;
    }
    window = SDL_CreateWindow(windowTitle, options.windowWidth, options.windowHeight, flags);
    if (!window) {
        error = std::format("SDL_CreateWindow: {}", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, minimumWindowWidth, minimumWindowHeight);
    renderer = createRenderer(window, api, error);
    if (renderer != nullptr) {
        applyOrientationToWindow();
    }
    return renderer != nullptr;
}

void App::applyOrientationToWindow() {
    applyOrientationHint(currentSettings.layout);
    const LayoutOrientation orientation = currentSettings.layout;
    if (orientation == LayoutOrientation::Auto || !window) {
        return;
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(display, &usable)) {
        return;
    }
    if (orientation == LayoutOrientation::Portrait) {
        const int height = std::max(
            minimumWindowHeight,
            std::min(static_cast<int>(static_cast<float>(usable.h) * portraitHeightShare), usable.h));
        const int width =
            std::max(minimumWindowWidth, static_cast<int>(static_cast<float>(height) * portraitAspectWidth /
                                                          portraitAspectHeight));
        SDL_SetWindowSize(window, width, height);
    } else {
        SDL_SetWindowSize(window, std::min(landscapeWindowWidth, usable.w),
                          std::min(landscapeWindowHeight, usable.h));
    }
}

void App::applyOrientationHint(LayoutOrientation orientation) {
    const char* hint = "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown";
    if (orientation == LayoutOrientation::Landscape) {
        hint = "LandscapeLeft LandscapeRight";
    } else if (orientation == LayoutOrientation::Portrait) {
        hint = "Portrait PortraitUpsideDown";
    }
    SDL_SetHint(SDL_HINT_ORIENTATIONS, hint);
}

bool App::initialize(const LaunchOptions& options, std::string& error) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    fonts.regular =
        io.Fonts->AddFontFromFileTTF(fontPath(palette::regularFontFile).string().c_str(), fontLoadPixels);
    if (!fonts.regular) {
        fonts.regular = io.Fonts->AddFontDefault();
    }
    fonts.strong =
        io.Fonts->AddFontFromFileTTF(fontPath(palette::strongFontFile).string().c_str(), fontLoadPixels);
    fonts.strongMinimumSize = palette::strongTextMinimumPixels;

    if (!openWindowAndRenderer(options, options.graphicsApi, error)) {
        const std::string_view fallback = fallbackGraphicsApi(options.graphicsApi);
        std::string fallbackError;
        if (fallback.empty() || !openWindowAndRenderer(options, fallback, fallbackError)) {
            error =
                fallback.empty() ? error : std::format("{}; {} fallback: {}", error, fallback, fallbackError);
            return false;
        }
    }
    balls = std::make_unique<BallView>(*renderer);
    profile.load();
    currentSettings = loadSettings(settingsFilePath());
    applyOrientationHint(currentSettings.layout);
    setVsync(options.vsync || currentSettings.vsync);
    backendChoice = startupBackend(options.backend, currentSettings.defaultBackend);
    if (options.controlPort > 0) {
        controlServer.start(options.controlPort);
    }
    const auto startLevel = options.playMode ? findLevel(*options.playMode, options.playLevel) : std::nullopt;
    const bool backendChosenOnCommandLine = options.playMode || options.backend;
    const std::optional<BenchmarkRecord>& stored = profile.benchmark();
    if (startLevel) {
        startGame(*options.playMode, *startLevel);
    } else if (!options.skipBenchmark && !backendChosenOnCommandLine &&
               benchmarkNeeded(stored, currentHardwareFingerprint(gameVersion()))) {
        showMachineBenchmark();
    } else {
        showMainMenu();
    }
    applyPendingScreen();
    previousUpdateStart = Clock::now();
    return true;
}

void App::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    input::handleEvent(event);
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        running = false;
    }
}

void App::setVsync(bool enabled) {
    if (renderer) {
        renderer->setVsync(enabled);
    }
}

void App::updateTextInput() {
    const bool wanted = input::wantsTextInput();
    if (wanted == textInputActive) {
        return;
    }
    textInputActive = wanted;
    if (wanted) {
        SDL_StartTextInput(window);
    } else {
        SDL_StopTextInput(window);
    }
}

bool App::frame() {
    if (!running) {
        return false;
    }
    const Clock::time_point updateStart = Clock::now();
    frameSeconds = static_cast<float>(millisecondsBetween(previousUpdateStart, updateStart) / 1000.0);
    previousUpdateStart = updateStart;
    const float deltaTime = std::min(frameSeconds, maximumFrameSeconds);
    renderer->beginFrame();
    const Clock::time_point workStart = Clock::now();
    gfx::beginFrame(renderer->viewWidth(), renderer->viewHeight(), fonts);
    currentScreen->update(deltaTime);
    currentScreen->draw();
    if (!currentScreen->drawsFrameTimings()) {
        drawFrameTimingsOverlay();
    }
    controlServer.pump([this](const ControlRequest& request) { return handleControl(request); });
    renderer->drawRects(gfx::sceneRects());
    updateTextInput();
    const Clock::time_point presentStart = Clock::now();
    const Color background = palette::background;
    renderer->endFrame(
        {background.r * channelScale, background.g * channelScale, background.b * channelScale});
    const Clock::time_point frameEnd = Clock::now();
    if (screenshotPending) {
        screenshotPending = false;
        const bool saved = renderer->lastScreenshotSucceeded();
        controlServer.completeDeferred([&](const ControlRequest&) {
            return saved ? ControlResponse::ok(
                               std::format("{{\"ok\":true,\"path\":\"{}\"}}", jsonEscape(screenshotPath)))
                         : ControlResponse::error(500, "could not write screenshot");
        });
    }
    input::clearTransientState();
    const double synchronizationMs = balls->takeSynchronizationMs();
    const double gpuWaitMs = renderer->lastGpuWaitMs();
    timings.record({millisecondsBetween(updateStart, frameEnd),
                    millisecondsBetween(workStart, presentStart) - synchronizationMs,
                    renderer->lastGpuBusyMs(),
                    millisecondsBetween(presentStart, frameEnd) + synchronizationMs + gpuWaitMs});
    applyPendingScreen();
    return running;
}

void App::drawFrameTimingsOverlay() const {
    const FrameSample& sample = timings.average();
    const std::string text =
        std::format("{:.0f} fps   cpu {:.2f}   gpu {:.2f}   present {:.2f} ms", timings.framesPerSecond(),
                    sample.cpuMs, sample.gpuMs, sample.presentMs);
    const Vec2 extent = ui::measureText(text, 18.0f);
    const float x = 26.0f;
    const float y = static_cast<float>(gfx::screenHeight()) - extent.y - 10.0f;
    ui::drawPanel({x - 10.0f, y - 4.0f, extent.x + 20.0f, extent.y + 8.0f},
                  gfx::fade(palette::backgroundDeep, 0.85f));
    ui::drawText(text, {x, y}, 18.0f, palette::textMuted);
}

void App::applyPendingScreen() {
    if (pendingScreen) {
        currentScreen = std::move(pendingScreen);
    }
}

std::string App::stateJson() const {
    const FrameSample& sample = timings.average();
    return std::format(
        "{{\"ok\":true,\"frontend\":\"sdl\",\"renderer\":\"{}\",\"screen\":\"{}\",\"window\":[{},{}],"
        "\"fps\":{:.1f},\"minFps\":{:.1f},\"frameMs\":{:.3f},\"cpuMs\":{:.3f},\"gpuMs\":{:.3f},"
        "\"presentMs\":{:.3f},\"preferredBackend\":\"{}\",\"vsync\":{},\"details\":{}}}",
        jsonEscape(renderer->deviceDescription()), currentScreen->name(), gfx::screenWidth(),
        gfx::screenHeight(), timings.framesPerSecond(), timings.worstFpsLastSecond(), sample.frameMs,
        sample.cpuMs, sample.gpuMs, sample.presentMs, backendKindName(backendChoice), isVsyncEnabled(),
        currentScreen->stateJson());
}

std::optional<ControlResponse> App::handleControl(const ControlRequest& request) {
    const std::string& path = request.path;
    if (path == "/state") {
        return ControlResponse::ok(stateJson());
    }
    if (path == "/screenshot") {
        const std::string requested = request.text("path").value_or("");
        if (requested.empty()) {
            return ControlResponse::error(400, "missing path");
        }
        screenshotPath = screenshotFileFor(requested);
        renderer->requestScreenshot(screenshotPath);
        screenshotPending = true;
        return std::nullopt;
    }
    if (path == "/quit") {
        quit();
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (path == "/benchmark") {
        if (request.integer("run").value_or(0) != 0) {
            showMachineBenchmark();
            return ControlResponse::ok("{\"ok\":true}");
        }
        return ControlResponse::ok(benchmarkStateJson());
    }
    if (path == "/menu") {
        showMainMenu();
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (path == "/settings") {
        Settings next = currentSettings;
        if (const auto collisions = request.integer("collisions")) {
            next.ballCollisions = *collisions != 0;
        }
        if (const auto layout = request.text("layout")) {
            if (const auto orientation = parseOrientation(*layout)) {
                next.layout = *orientation;
            } else {
                return ControlResponse::error(400, "layout must be auto, landscape or portrait");
            }
        }
        if (const auto vsync = request.integer("vsync")) {
            next.vsync = *vsync != 0;
        }
        std::optional<BackendKind> backend;
        if (const auto name = request.text("backend")) {
            backend = parseBackendKind(*name);
            if (!backend || !isBackendAvailable(*backend)) {
                return ControlResponse::error(400, std::format("backend {} is not available", *name));
            }
        }
        const bool open = request.integer("open").value_or(0) != 0;
        applySettings(next, backend);
        if (open && currentScreen) {
            applyPendingScreen();
            if (auto* gameplay = dynamic_cast<GameplayScreen*>(currentScreen.get())) {
                gameplay->openSettingsOverlay();
            } else if (dynamic_cast<SettingsScreen*>(currentScreen.get()) == nullptr) {
                showSettings();
            }
        }
        return ControlResponse::ok(settingsJson());
    }
    const std::optional<GameMode> mode = parseGameMode(request.text("mode").value_or("normal"));
    if (path == "/levels" || path == "/play" || path == "/editor") {
        if (!mode) {
            return ControlResponse::error(400, "mode must be normal or performance");
        }
        const std::string levelName = request.text("level").value_or("");
        const std::optional<LevelFiles> level =
            levelName.empty() ? std::nullopt : findLevel(*mode, levelName);
        if (path == "/levels") {
            showLevelSelect(*mode);
        } else if (path == "/editor") {
            openEditor(*mode, level);
        } else if (!level) {
            return ControlResponse::error(404,
                                          std::format("no level '{}' in {} mode", levelName,
                                                      *mode == GameMode::Normal ? "normal" : "performance"));
        } else {
            startGame(*mode, *level);
        }
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (path == "/vsync") {
        setVsync(request.integer("on").value_or(isVsyncEnabled() ? 0 : 1) != 0);
        return ControlResponse::ok(std::format("{{\"ok\":true,\"vsync\":{}}}", isVsyncEnabled()));
    }
    if (path == "/window") {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        width = request.integer("width").value_or(width);
        height = request.integer("height").value_or(height);
        SDL_SetWindowSize(window, std::max(width, minimumWindowWidth), std::max(height, minimumWindowHeight));
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (auto response = currentScreen->handleControl(request)) {
        return *response;
    }
    return ControlResponse::error(
        404, std::format("unknown command {} on screen {}", path, currentScreen->name()));
}

void App::showMainMenu() {
    pendingScreen = std::make_unique<MainMenuScreen>(*this);
}

void App::showMachineBenchmark() {
    pendingScreen = std::make_unique<MachineBenchmarkScreen>(*this, currentHardwareFingerprint(gameVersion()));
}

void App::adoptBenchmarkWinner(const BenchmarkCandidate& winner) {
    if (!isBackendAvailable(winner.kind)) {
        return;
    }
    backendChoice = winner.kind;
    currentSettings.defaultBackend = std::string(backendKindName(winner.kind));
    static_cast<void>(saveSettings(settingsFilePath(), currentSettings));
}

std::string App::benchmarkStateJson() const {
    if (currentScreen && currentScreen->name() == "benchmark") {
        return currentScreen->stateJson();
    }
    const std::optional<BenchmarkRecord>& stored = profile.benchmark();
    if (!stored) {
        return benchmarkJson("none", {}, std::nullopt);
    }
    return benchmarkJson(stored->complete ? "done" : "skipped", stored->results, stored->winner);
}

void App::showSettings() {
    pendingScreen = std::make_unique<SettingsScreen>(*this);
}

void App::applySettings(const Settings& settings, std::optional<BackendKind> backend) {
    currentSettings = settings;
    if (backend) {
        backendChoice = *backend;
        currentSettings.defaultBackend = std::string(backendKindName(*backend));
    }
    static_cast<void>(saveSettings(settingsFilePath(), currentSettings));
    setVsync(settings.vsync);
    applyOrientationToWindow();
    notifySettingsConsumers();
}

std::string App::settingsJson() const {
    return std::format("{{\"ballCollisions\":{},\"layout\":\"{}\",\"vsync\":{},\"defaultBackend\":\"{}\"}}",
                       currentSettings.ballCollisions, orientationName(currentSettings.layout),
                       currentSettings.vsync, jsonEscape(currentSettings.defaultBackend));
}

void App::notifySettingsConsumers() {
    applyPendingScreen();
    if (auto* gameplay = dynamic_cast<GameplayScreen*>(currentScreen.get())) {
        gameplay->refreshSettings();
    }
}

void App::showLevelSelect(GameMode mode) {
    pendingScreen = std::make_unique<LevelSelectScreen>(*this, mode);
}

void App::startGame(GameMode mode, const LevelFiles& files) {
    pendingScreen = std::make_unique<GameplayScreen>(*this, mode, files);
}

void App::openEditor(GameMode mode, std::optional<LevelFiles> files) {
    pendingScreen = std::make_unique<MapEditorScreen>(*this, mode, std::move(files));
}
