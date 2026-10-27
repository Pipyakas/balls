#include "app/App.h"

#include "app/Input.h"
#include "editor/MapEditorScreen.h"
#include "level/LevelRepository.h"
#include "render/Palette.h"
#include "screens/GameplayScreen.h"
#include "screens/LevelSelectScreen.h"
#include "screens/MainMenuScreen.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace {

constexpr int minimumWindowWidth = 1024;
constexpr int minimumWindowHeight = 700;
constexpr float maximumFrameSeconds = 0.1f;
constexpr float fontLoadPixels = 32.0f;
constexpr float channelScale = 1.0f / 255.0f;
constexpr const char* windowTitle = "Balls";
constexpr const char* screenshotExtension = ".bmp";

double millisecondsBetween(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
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

std::string screenshotFileFor(const std::string& requested) {
    std::filesystem::path path(requested);
    path.replace_extension(screenshotExtension);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return path.string();
}

}

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
    renderer = createRenderer(window, api, options.gpu, error);
    return renderer != nullptr;
}

bool App::initialize(const LaunchOptions& options, std::string& error) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    const std::string fontFile = fontPath().string();
    font = io.Fonts->AddFontFromFileTTF(fontFile.c_str(), fontLoadPixels);
    if (!font) {
        font = io.Fonts->AddFontDefault();
    }

    if (!openWindowAndRenderer(options, options.graphicsApi, error)) {
        const std::string_view fallback = fallbackGraphicsApi(options.graphicsApi);
        std::string fallbackError;
        if (fallback.empty() || !openWindowAndRenderer(options, fallback, fallbackError)) {
            error = fallback.empty() ? error : std::format("{}; {} fallback: {}", error, fallback, fallbackError);
            return false;
        }
    }
    balls = std::make_unique<BallView>(*renderer);
    profile.load();
    setComputeGpuPreference(options.computeGpu);
    setVsync(options.vsync);
    backendChoice = options.backend && isBackendAvailable(*options.backend) ? *options.backend
                                                                            : fastestAvailableCpuBackend();
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
    gfx::beginFrame(renderer->viewWidth(), renderer->viewHeight(), font);
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
    renderer->endFrame({background.r * channelScale, background.g * channelScale, background.b * channelScale});
    const Clock::time_point frameEnd = Clock::now();
    if (screenshotPending) {
        screenshotPending = false;
        const bool saved = renderer->lastScreenshotSucceeded();
        controlServer.completeDeferred([&](const ControlRequest&) {
            return saved ? ControlResponse::ok(std::format("{{\"ok\":true,\"path\":\"{}\"}}", jsonEscape(screenshotPath)))
                         : ControlResponse::error(500, "could not write screenshot");
        });
    }
    input::clearTransientState();
    const double synchronizationMs = balls->takeSynchronizationMs();
    const double gpuWaitMs = renderer->lastGpuWaitMs();
    timings.record({millisecondsBetween(updateStart, frameEnd),
                    millisecondsBetween(workStart, presentStart) - synchronizationMs, renderer->lastGpuBusyMs(),
                    millisecondsBetween(presentStart, frameEnd) + synchronizationMs + gpuWaitMs});
    applyPendingScreen();
    return running;
}

void App::drawFrameTimingsOverlay() const {
    const FrameSample& sample = timings.average();
    const std::string text = std::format("{:.0f} fps   cpu {:.2f}   gpu {:.2f}   present {:.2f} ms",
                                         timings.framesPerSecond(), sample.cpuMs, sample.gpuMs, sample.presentMs);
    const Vec2 extent = ui::measureText(text, 18.0f);
    const float x = 26.0f;
    const float y = static_cast<float>(gfx::screenHeight()) - extent.y - 10.0f;
    gfx::fillRounded({x - 10.0f, y - 4.0f, extent.x + 20.0f, extent.y + 8.0f}, 0.4f,
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
    return std::format("{{\"ok\":true,\"frontend\":\"sdl\",\"renderer\":\"{}\",\"screen\":\"{}\",\"window\":[{},{}],"
                       "\"fps\":{:.1f},\"minFps\":{:.1f},\"frameMs\":{:.3f},\"cpuMs\":{:.3f},\"gpuMs\":{:.3f},"
                       "\"presentMs\":{:.3f},\"preferredBackend\":\"{}\",\"computeGpu\":\"{}\",\"vsync\":{},\"details\":{}}}",
                       jsonEscape(renderer->deviceDescription()), currentScreen->name(), gfx::screenWidth(),
                       gfx::screenHeight(), timings.framesPerSecond(), timings.worstFpsLastSecond(), sample.frameMs,
                       sample.cpuMs, sample.gpuMs, sample.presentMs, backendKindName(backendChoice),
                       jsonEscape(backendDetail(BackendKind::Vulkan)), isVsyncEnabled(), currentScreen->stateJson());
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
