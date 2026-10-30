#include "app/App.h"

#include "app/Input.h"
#include "app/Paths.h"
#include "app/PreferenceDirectory.h"
#include "editor/MapEditorScreen.h"
#include "game/SessionRunner.h"
#include "game/Settings.h"
#include "level/LevelRepository.h"
#include "render/Palette.h"
#include "screens/GameplayScreen.h"
#include "screens/LevelSelectScreen.h"
#include "screens/MachineBenchmarkScreen.h"
#include "screens/MainMenuScreen.h"
#include "screens/SettingsScreen.h"
#include "sim/BackendSelection.h"
#include "sim/BackendWorker.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

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
constexpr char importPathSeparator = '|';
constexpr float toastHeight = 48.0f;
constexpr float toastPadding = 20.0f;
constexpr float toastMargin = 24.0f;

// ImGui asserts inside AddFontFromFileTTF when the file cannot be opened, which aborts the app on
// devices where the asset extraction only produced an empty or unreadable font file.
ImFont* loadFontFromDisk(ImFontAtlas& atlas, const std::filesystem::path& path, float pixels) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return nullptr;
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes == 0) {
        return nullptr;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return nullptr;
    }
    std::vector<char> contents(static_cast<size_t>(bytes));
    if (!stream.read(contents.data(), static_cast<std::streamsize>(bytes))) {
        return nullptr;
    }
    void* owned = ImGui::MemAlloc(static_cast<size_t>(bytes));
    std::memcpy(owned, contents.data(), static_cast<size_t>(bytes));
    return atlas.AddFontFromMemoryTTF(owned, static_cast<int>(bytes), pixels);
}

// The renderer this build chose at configure time, used until a launch option or the saved settings override it.
std::string defaultRendererName() {
#if defined(BALLS_RENDERER_WEBGPU) || defined(BALLS_WITH_WEBGPU)
    return "webgpu";
#elif defined(BALLS_RENDERER_NVRHI) || defined(BALLS_WITH_NVRHI)
    return "nvrhi";
#else
    return "sdlgpu";
#endif
}

// The adapter list has to come from the API the renderer actually drives, so "auto" follows the renderer and platform.
GpuApi adapterApiFor(std::string_view renderer, std::string_view api) {
    if (api == "vulkan" || api == "webgpu-vulkan") {
        return GpuApi::Vulkan;
    }
    if (api == "d3d12" || api == "webgpu-d3d12") {
        return GpuApi::D3d12;
    }
#if defined(_WIN32)
    return renderer == "webgpu" ? GpuApi::D3d12 : GpuApi::Vulkan;
#else
    (void)renderer;
    return GpuApi::Vulkan;
#endif
}

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

App::App()
    : flowInstance(levelDirectory(GameMode::Normal), levelDirectory(GameMode::Performance)) {}

App::~App() {
    controlServer.stop();
    currentScreen.reset();
    // A level left open is still finishing its physics frame; give it a moment so its backend is destroyed while the
    // plugins are loaded, but do not let a stuck step hold the window open.
    static_cast<void>(SessionRunner::waitForStoppedRunners(std::chrono::seconds(2)));
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
    const std::string wantedRenderer =
        options.renderer.empty() ? (activeRenderer.empty() ? defaultRendererName() : activeRenderer)
                                 : options.renderer;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | rendererWindowFlags(wantedRenderer, api);
    if (options.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (options.background) {
        flags |= SDL_WINDOW_NOT_FOCUSABLE;
    }
    window = SDL_CreateWindow(windowTitle, options.windowWidth, options.windowHeight, flags);
    if (!window && (flags & SDL_WINDOW_VULKAN) && api != "vulkan") {
        // A PC without a Vulkan loader can still run Direct3D 12; only the switch to Vulkan is lost.
        window = SDL_CreateWindow(windowTitle, options.windowWidth, options.windowHeight, flags & ~SDL_WINDOW_VULKAN);
    }
    if (!window) {
        error = std::format("SDL_CreateWindow: {}", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, minimumWindowWidth, minimumWindowHeight);
#if defined(__ANDROID__)
    // Fullscreen hides the status bar, which otherwise clips the top gadget in portrait.
    SDL_SetWindowFullscreen(window, true);
#endif
    renderer = createRenderer(window, wantedRenderer, api, options.gpu, error);
    if (!renderer) {
        return false;
    }
    activeRenderer = wantedRenderer;
    activeGraphicsApi = std::string(api);
    renderGpuPreference = options.gpu;
    renderAdapterList = enumerateGpuAdapters(adapterApiFor(activeRenderer, activeGraphicsApi));
    activeRenderAdapter = selectGpuAdapter(renderAdapterList, renderGpuPreference, GpuRole::Render);
    applyOrientationToWindow();
    return true;
}

void App::applyPendingRendererSwitch() {
    if (!pendingRendererSwitch) {
        return;
    }
    auto [wantedRenderer, wantedApi] = *pendingRendererSwitch;
    pendingRendererSwitch.reset();
    if (wantedRenderer.empty()) {
        wantedRenderer = activeRenderer;
    }
    if (wantedApi.empty()) {
        wantedApi = activeGraphicsApi;
    }
    if (wantedRenderer == activeRenderer && wantedApi == activeGraphicsApi && !preparingTarget) {
        return;
    }
    switchRenderer({wantedRenderer, wantedApi, renderGpuPreference});
}

void App::applyPendingRenderAdapter() {
    if (!pendingRenderAdapter) {
        return;
    }
    const int wanted = *pendingRenderAdapter;
    pendingRenderAdapter.reset();
    if ((wanted == activeRenderAdapter && !preparingTarget) || wanted < 0 ||
        wanted >= static_cast<int>(renderAdapterList.size())) {
        return;
    }
    switchRenderer({activeRenderer, activeGraphicsApi, std::to_string(wanted)});
}

// Opening a device and building its pipelines takes up to a few hundred milliseconds, so a renderer that can be
// prepared apart from the window is built on a worker thread while the current one keeps drawing; the frame only
// pays for the swap-chain hand-over in adoptPreparedRenderer. Other renderers still switch on the frame.
void App::switchRenderer(RendererTarget target) {
    if (!isRendererCompiledIn(target.renderer)) {
        // Tearing the working renderer down only to find the wanted one missing would cost a rebuild for nothing.
        SDL_Log("Renderer %s is not in this build", target.renderer.c_str());
        showToast(std::format("This build has no {} renderer", target.renderer), true);
        return;
    }
    if (preparing.valid()) {
        // The future of a std::async waits in its destructor, so an abandoned build finishes on its own thread.
        std::thread([abandoned = std::move(preparing)]() mutable { retireRenderer(abandoned.get().renderer); }).detach();
    }
    preparingTarget.reset();
    if (!renderer || !renderer->supportsDeferredAttach()) {
        switchRendererNow(target);
        return;
    }
    preparingTarget = target;
    SDL_Window* targetWindow = window;
    preparing = std::async(std::launch::async, [targetWindow, target] {
        PreparedRenderer prepared;
        prepared.renderer =
            prepareRenderer(targetWindow, target.renderer, target.api, target.gpuPreference, prepared.error);
        if (prepared.renderer) {
            prepared.adapters = enumerateGpuAdapters(adapterApiFor(target.renderer, target.api));
        }
        return prepared;
    });
}

void App::adoptPreparedRenderer() {
    if (!preparing.valid() || preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    PreparedRenderer prepared = preparing.get();
    const RendererTarget target = *preparingTarget;
    preparingTarget.reset();
    if (!prepared.renderer) {
        // This renderer cannot be prepared apart from the window, or preparing it failed: the frame-blocking path
        // either builds it or reports why not.
        if (prepared.error.empty()) {
            switchRendererNow(target);
        } else {
            SDL_Log("Could not switch renderer to %s (%s) on GPU %s: %s", target.renderer.c_str(), target.api.c_str(),
                    target.gpuPreference.c_str(), prepared.error.c_str());
            showToast("Could not switch the renderer: " + prepared.error, true);
        }
        return;
    }
    const bool vsync = isVsyncEnabled();
    balls.reset();
    renderer->detachFromWindow();
    std::string error;
    if (!prepared.renderer->attachToWindow(error)) {
        SDL_Log("Could not attach renderer %s (%s): %s", target.renderer.c_str(), target.api.c_str(), error.c_str());
        retireRenderer(std::move(prepared.renderer));
        std::string reattachError;
        if (!renderer->attachToWindow(reattachError)) {
            running = false;
            return;
        }
        showToast("Could not switch the renderer: " + error, true);
    } else {
        retireRenderer(std::exchange(renderer, std::move(prepared.renderer)));
        useRenderer(target, std::move(prepared.adapters));
    }
    balls = std::make_unique<BallView>(*renderer);
    setVsync(vsync);
}

void App::useRenderer(const RendererTarget& target, std::vector<GpuAdapterInfo> adapters) {
    activeRenderer = target.renderer;
    activeGraphicsApi = target.api;
    renderGpuPreference = target.gpuPreference;
    renderAdapterList = std::move(adapters);
    activeRenderAdapter = selectGpuAdapter(renderAdapterList, renderGpuPreference, GpuRole::Render);
    SDL_Log("Switched renderer to %s (%s) on %s", activeRenderer.c_str(), activeGraphicsApi.c_str(),
            renderer->deviceDescription().c_str());
}

void App::switchRendererNow(const RendererTarget& target) {
    const bool vsync = isVsyncEnabled();
    const RendererTarget previous{activeRenderer, activeGraphicsApi, renderGpuPreference};
    balls.reset();
    renderer.reset();
    std::string error;
    renderer = createRenderer(window, target.renderer, target.api, target.gpuPreference, error);
    if (renderer) {
        useRenderer(target, enumerateGpuAdapters(adapterApiFor(target.renderer, target.api)));
    } else {
        SDL_Log("Could not switch renderer to %s (%s): %s", target.renderer.c_str(), target.api.c_str(), error.c_str());
        std::string fallbackError;
        renderer = createRenderer(window, previous.renderer, previous.api, previous.gpuPreference, fallbackError);
    }
    if (!renderer) {
        running = false;
        return;
    }
    balls = std::make_unique<BallView>(*renderer);
    setVsync(vsync);
}

// Destroying a device waits for its GPU work and frees its memory, so a retired renderer goes on its own thread.
void App::retireRenderer(std::unique_ptr<IRenderer> retired) {
    if (retired) {
        std::thread([doomed = std::move(retired)]() mutable { doomed.reset(); }).detach();
    }
}

void App::applyOrientationToWindow() {
    applyOrientationHint(windowLayout(currentSettings));
    const LayoutOrientation orientation = windowLayout(currentSettings);
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
    fonts.regular = loadFontFromDisk(*io.Fonts, fontPath(palette::regularFontFile), fontLoadPixels);
    if (!fonts.regular) {
        SDL_Log("Fonts: %s unreadable, using the built-in font", palette::regularFontFile);
        fonts.regular = io.Fonts->AddFontDefault();
    }
    fonts.strong = loadFontFromDisk(*io.Fonts, fontPath(palette::strongFontFile), fontLoadPixels);
    if (!fonts.strong) {
        fonts.strong = fonts.regular;
    }
    fonts.strongMinimumSize = palette::strongTextMinimumPixels;

    // The saved settings decide the renderer unless a launch option names one outright. Without a file the build's
    // own default stays in charge, since a fresh Settings carries the WebGPU name whatever this binary links.
    std::error_code settingsError;
    const bool savedSettings = std::filesystem::is_regular_file(settingsFilePath(), settingsError);
    currentSettings = loadSettings(settingsFilePath());
    LaunchOptions effective = options;
    if (effective.renderer.empty() && savedSettings) {
        effective.renderer = normalizeRendererName(currentSettings.renderer);
    }
    if (effective.graphicsApi.empty() && savedSettings) {
        effective.graphicsApi = normalizeGraphicsApiName(currentSettings.graphicsApi);
    }

    if (!openWindowAndRenderer(effective, effective.graphicsApi, error)) {
        const std::string_view fallback = fallbackGraphicsApi(effective.graphicsApi);
        std::string fallbackError;
        bool initialized = false;
        if (!fallback.empty() && openWindowAndRenderer(effective, fallback, fallbackError)) {
            initialized = true;
        } else if (isNvrhiCompiledIn() && activeRenderer != "nvrhi") {
            // WebGPU can fail on an old driver; the desktop build links NVRHI too, so try it rather than quit.
            LaunchOptions nvrhiOptions = effective;
            nvrhiOptions.renderer = "nvrhi";
            nvrhiOptions.graphicsApi = "auto";
            std::string nvrhiError;
            if (openWindowAndRenderer(nvrhiOptions, nvrhiOptions.graphicsApi, nvrhiError)) {
                SDL_Log("WebGPU failed (%s); recovered with the NVRHI renderer", error.c_str());
                initialized = true;
            } else {
                const std::string_view nvrhiFallback = fallbackGraphicsApi(nvrhiOptions.graphicsApi);
                if (!nvrhiFallback.empty() && openWindowAndRenderer(nvrhiOptions, nvrhiFallback, fallbackError)) {
                    SDL_Log("WebGPU failed; recovered with the NVRHI renderer (%s)",
                            std::string(nvrhiFallback).c_str());
                    initialized = true;
                }
            }
        }
        if (!initialized) {
            error =
                fallback.empty() ? error : std::format("{}; {} fallback: {}", error, fallback, fallbackError);
            return false;
        }
    }
    balls = std::make_unique<BallView>(*renderer);
    profile = MachineProfile(preferenceDirectory());
    profile.load();
    setComputeGpuPreference(options.computeGpu);
    computeAdapterList = enumerateGpuAdapters(GpuApi::Vulkan);
    applyOrientationHint(windowLayout(currentSettings));
    setVsync(effective.vsync || currentSettings.vsync);
    backendChoice = chooseStartupBackend(options.backend, currentSettings.defaultBackend, assetRoot() / targetBenchmarkFileName);
    if (options.controlPort > 0) {
        controlServer.start(options.controlPort);
    }
    const auto startLevel = options.playMode ? findLevel(*options.playMode, options.playLevel) : std::nullopt;
    if (options.editor) {
        openEditor(options.playMode.value_or(GameMode::Normal), startLevel);
    } else if (startLevel) {
        startGame(*options.playMode, *startLevel);
    } else {
        showMainMenu();
    }
    processFlowRequests();
    applyFlowScreen();
    previousUpdateStart = Clock::now();
    return true;
}

void App::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    input::handleEvent(event);
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        running = false;
    } else if (event.type == SDL_EVENT_DROP_BEGIN) {
        droppedPaths.clear();
    } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
        droppedPaths.push_back(pathFromUtf8(event.drop.data));
    } else if (event.type == SDL_EVENT_DROP_COMPLETE && !droppedPaths.empty()) {
        deliverImport(std::move(droppedPaths));
        droppedPaths.clear();
    }
}

void App::showToast(std::string text, bool isError) {
    flowInstance.showToast(std::move(text), isError);
}

void App::deliverImport(std::vector<std::filesystem::path> paths) {
    flowInstance.importLevels(paths);
}

// SDL may call this from its own dialog thread, so the result is only queued here and handed to the screen on
// the next frame.
void App::receiveImportDialogResult(void* userdata, const char* const* fileList, int) {
    App& app = *static_cast<App*>(userdata);
    std::lock_guard lock(app.dialogMutex);
    if (!fileList) {
        app.dialogErrors.push_back(SDL_GetError());
        return;
    }
    std::vector<std::filesystem::path> picked;
    for (const char* const* file = fileList; *file; ++file) {
        picked.push_back(pathFromUtf8(*file));
    }
    if (!picked.empty()) {
        app.finishedDialogs.push_back(std::move(picked));
    }
}

void App::openLevelImportDialog(const std::filesystem::path& startFolder) {
    static const SDL_DialogFileFilter levelFilter{"Level files (*_Bricks.txt, *_SpawnBall.txt)", "txt"};
    const std::string location = utf8Of(startFolder);
    SDL_ShowOpenFileDialog(&App::receiveImportDialogResult, this, window, &levelFilter, 1, location.c_str(), true);
}

void App::deliverFinishedDialogs() {
    std::vector<std::vector<std::filesystem::path>> picked;
    std::vector<std::string> errors;
    {
        std::lock_guard lock(dialogMutex);
        picked.swap(finishedDialogs);
        errors.swap(dialogErrors);
    }
    for (const std::string& error : errors) {
        showToast("Import failed: " + error, true);
    }
    for (auto& paths : picked) {
        deliverImport(std::move(paths));
    }
}

void App::openFolder(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::string url = "file:///";
    const std::u8string genericPath = folder.lexically_normal().generic_u8string();
    for (const char8_t unit : genericPath) {
        const char character = static_cast<char>(unit);
        url += character == ' ' ? std::string("%20") : std::string(1, character);
    }
    if (!SDL_OpenURL(url.c_str())) {
        showToast(std::format("Could not open {}: {}", utf8Of(folder), SDL_GetError()), true);
    }
}

void App::drawToast() {
    const FlowToast& toast = flowInstance.toast();
    if (toast.secondsLeft <= 0.0f) {
        return;
    }
    const float alpha = std::min(1.0f, toast.secondsLeft);
    const float maximumWidth = static_cast<float>(gfx::screenWidth()) - 2.0f * toastMargin;
    const float size = std::min(theme::toastTextSize, theme::toastTextSize * maximumWidth / std::max(ui::measureText(toast.text, theme::toastTextSize).x, 1.0f));
    const Vec2 extent = ui::measureText(toast.text, size);
    const float top = currentScreen ? currentScreen->toastTop() : 0.0f;
    const Rect rect{(static_cast<float>(gfx::screenWidth()) - extent.x) * 0.5f - toastPadding, top,
                    extent.x + 2.0f * toastPadding, toastHeight};
    ui::drawPanel(rect, gfx::fade(toast.isError ? palette::danger : palette::control, alpha), 0.4f);
    ui::drawTextInRect(toast.text, rect, size, gfx::fade(palette::text, alpha));
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
    longestFrameIntervalMs = std::max(longestFrameIntervalMs, millisecondsBetween(previousUpdateStart, updateStart));
    previousUpdateStart = updateStart;
    const float deltaTime = std::min(frameSeconds, maximumFrameSeconds);

    if (input::keyPressed(SDL_SCANCODE_ESCAPE) && flowInstance.screen() != FlowScreen::MapEditor) {
        flowInstance.escape();
    }
    flowInstance.update(deltaTime);
    processFlowRequests();
    applyFlowScreen();

    if (!currentScreen) {
        return running;
    }

    renderer->beginFrame();
    const Clock::time_point workStart = Clock::now();
    gfx::beginFrame(renderer->viewWidth(), renderer->viewHeight(), fonts);
    deliverFinishedDialogs();
    currentScreen->update(deltaTime);
    currentScreen->draw();
    if (!currentScreen->drawsFrameTimings()) {
        drawFrameTimingsOverlay();
    }
    drawToast();
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
    const double gpuWaitMs = renderer->lastGpuWaitMs();
    const double frameMs = millisecondsBetween(updateStart, frameEnd);
    const double presentWaitMs = renderer->lastPresentWaitMs();
    timings.record({.frameMs = frameMs,
                    .workMs = std::max(0.0, frameMs - presentWaitMs),
                    .cpuMs = millisecondsBetween(workStart, presentStart),
                    .gpuMs = renderer->lastGpuBusyMs(),
                    .presentMs = millisecondsBetween(presentStart, frameEnd) + gpuWaitMs,
                    .presented = renderer->lastFramePresented() ? 1.0 : 0.0,
                    .waitMs = presentWaitMs});
    applyPendingRenderAdapter();
    applyPendingRendererSwitch();
    adoptPreparedRenderer();
    processFlowRequests();
    applyFlowScreen();
    return running;
}

void App::drawFrameTimingsOverlay() const {
    const FrameSample& sample = timings.average();
    const std::string text =
        std::format("{:.0f} fps ({:.0f} shown)   cpu {:.2f}   gpu {:.2f}   present {:.2f} ms", timings.framesPerSecond(),
                    timings.shownFramesPerSecond(), sample.cpuMs, sample.gpuMs, sample.presentMs);
    const Vec2 extent = ui::measureText(text, 18.0f);
    const float x = 26.0f;
    const float y = static_cast<float>(gfx::screenHeight()) - extent.y - 10.0f;
    ui::drawPanel({x - 10.0f, y - 4.0f, extent.x + 20.0f, extent.y + 8.0f},
                  gfx::fade(palette::boardBackdrop, 0.85f));
    ui::drawText(text, {x, y}, 18.0f, palette::textMuted);
}

void App::processFlowRequests() {
    while (const auto request = flowInstance.takeRequest()) {
        switch (request->kind) {
        case FlowRequestKind::StartGame:
        case FlowRequestKind::ReloadGame:
            if (request->files) {
                currentScreen = std::make_unique<GameplayScreen>(*this, request->mode, *request->files);
            }
            break;
        case FlowRequestKind::RestartGame:
            if (auto* gameplay = dynamic_cast<GameplayScreen*>(currentScreen.get())) {
                gameplay->restartSession();
            }
            break;
        case FlowRequestKind::EndGame:
            if (dynamic_cast<GameplayScreen*>(currentScreen.get())) {
                currentScreen.reset();
            }
            break;
        case FlowRequestKind::OpenEditor:
            currentScreen = std::make_unique<MapEditorScreen>(*this, request->mode, request->files);
            break;
        case FlowRequestKind::StartBenchmark:
            currentScreen = std::make_unique<MachineBenchmarkScreen>(*this, currentHardwareFingerprint(gameVersion()));
            break;
        case FlowRequestKind::SkipBenchmark:
            if (auto* benchmark = dynamic_cast<MachineBenchmarkScreen*>(currentScreen.get())) {
                benchmark->skip();
            }
            break;
        case FlowRequestKind::PickLevelFiles:
            openLevelImportDialog(request->folder);
            break;
        case FlowRequestKind::OpenFolder:
            openFolder(request->folder);
            break;
        case FlowRequestKind::Quit:
            quit();
            break;
        }
    }
}

void App::applyFlowScreen() {
    switch (flowInstance.screen()) {
    case FlowScreen::MainMenu:
        if (!dynamic_cast<MainMenuScreen*>(currentScreen.get())) {
            currentScreen = std::make_unique<MainMenuScreen>(*this);
        }
        break;
    case FlowScreen::LevelSelect:
        if (!dynamic_cast<LevelSelectScreen*>(currentScreen.get()) || lastLevelSelectMode != flowInstance.mode()) {
            lastLevelSelectMode = flowInstance.mode();
            currentScreen = std::make_unique<LevelSelectScreen>(*this, flowInstance.mode());
        }
        break;
    case FlowScreen::Settings:
        if (!dynamic_cast<SettingsScreen*>(currentScreen.get())) {
            currentScreen = std::make_unique<SettingsScreen>(*this);
        }
        break;
    case FlowScreen::MachineBenchmark:
        if (!dynamic_cast<MachineBenchmarkScreen*>(currentScreen.get())) {
            currentScreen = std::make_unique<MachineBenchmarkScreen>(*this, currentHardwareFingerprint(gameVersion()));
        }
        break;
    case FlowScreen::MapEditor:
        if (!dynamic_cast<MapEditorScreen*>(currentScreen.get())) {
            currentScreen = std::make_unique<MapEditorScreen>(*this, flowInstance.mode(), flowInstance.level());
        }
        break;
    case FlowScreen::Gameplay:
    case FlowScreen::LoadError:
        if (!dynamic_cast<GameplayScreen*>(currentScreen.get()) && flowInstance.level()) {
            currentScreen = std::make_unique<GameplayScreen>(*this, flowInstance.mode(), *flowInstance.level());
        }
        break;
    }
}

namespace {

std::string adapterNamesJson(const std::vector<GpuAdapterInfo>& adapters) {
    std::string json = "[";
    for (const GpuAdapterInfo& adapter : adapters) {
        json += std::format("{}\"{}\"", json.size() > 1 ? "," : "", jsonEscape(adapter.name));
    }
    return json + "]";
}

} // namespace

// What the core's control API may do to SDL. It runs on the main thread, from ControlServer::pump.
struct App::ControlBridge final : ControlHost {
    App& app;

    explicit ControlBridge(App& owner) : app(owner) {}

    ControlFrontEndState frontEndState() const override {
        ControlFrontEndState state;
        state.frontEnd = "sdl";
        state.renderer = app.renderer ? app.renderer->deviceDescription() : std::string{};
        state.screen = flowScreenName(app.flow().screen());
        state.windowWidth = gfx::screenWidth();
        state.windowHeight = gfx::screenHeight();
        state.frames = &app.timings;
        state.preferredBackend = app.backendChoice;
        state.vsync = app.isVsyncEnabled();
        state.toast = app.flow().toast().secondsLeft > 0.0f ? app.flow().toast().text : std::string{};
        state.extraFields = std::format("\"computeGpu\":\"{}\",\"renderAdapters\":{},\"computeAdapters\":{}",
                                        jsonEscape(backendStatus(BackendKind::Vulkan).detail),
                                        adapterNamesJson(app.renderAdapterList), adapterNamesJson(app.computeAdapterList));
        state.screenDetails = app.currentScreen ? app.currentScreen->stateJson() : std::string{};
        return state;
    }
    ControlGame* game() override { return app.currentScreen ? app.currentScreen->controlGame() : nullptr; }
    std::filesystem::path levelDirectory(GameMode mode) const override { return ::levelDirectory(mode); }

    std::optional<ControlResponse> takeScreenshot(const std::string& path) override {
        app.screenshotPath = screenshotFileFor(path);
        app.renderer->requestScreenshot(app.screenshotPath);
        app.screenshotPending = true;
        return std::nullopt;
    }
    void play(GameMode mode, const LevelFiles& level) override {
        app.flow().startGame(mode, level);
    }
    void showMainMenu() override {
        app.flow().showMainMenu();
    }
    bool showLevelSelect(GameMode mode) override {
        app.flow().showLevelSelect(mode);
        return true;
    }
    bool openEditor(GameMode mode, const std::optional<LevelFiles>& level) override {
        app.flow().openEditor(mode, level);
        return true;
    }
    void quit() override { app.quit(); }

    const Settings& settings() const override { return app.currentSettings; }
    void applySettings(const Settings& settings, std::optional<BackendKind> defaultBackend) override {
        app.applySettings(settings, defaultBackend);
    }
    void openSettings() override {
        if (app.flow().screen() == FlowScreen::Gameplay) {
            if (app.flow().overlay() == FlowOverlay::None) {
                app.flow().activate(FlowAction::TogglePause);
            }
            app.flow().activate(FlowAction::PauseSettings);
        } else {
            app.flow().showSettings();
        }
    }
    void setVsync(bool enabled) override { app.setVsync(enabled); }

    ControlBenchmarkState benchmarkState() const override {
        if (const auto* screen = dynamic_cast<const MachineBenchmarkScreen*>(app.currentScreen.get())) {
            return screen->controlState();
        }
        const std::optional<BenchmarkRecord>& stored = app.profile.benchmark();
        if (!stored) {
            return {};
        }
        return {stored->complete ? "done" : "skipped", stored->results, stored->winner};
    }
    void runBenchmark() override {
        app.flow().runMachineBenchmark();
    }
    bool skipBenchmark() override {
        const bool applied = app.flow().activate(FlowAction::SkipBenchmark);
        return applied;
    }
    bool continueFromBenchmark() override {
        const bool applied = app.flow().activate(FlowAction::ContinueFromBenchmark);
        return applied;
    }

    std::optional<ControlResponse> handleOwnRoute(const ControlRequest& request) override {
        return app.handleOwnControl(request);
    }
};

std::optional<ControlResponse> App::handleControl(const ControlRequest& request) {
    if (!controlBridge) {
        controlBridge = std::make_shared<ControlBridge>(*this);
    }
    return handleControlRequest(*controlBridge, request);
}

// The routes only SDL has: renderer and window controls, level import and the frame hitch probe.
std::optional<ControlResponse> App::handleOwnControl(const ControlRequest& request) {
    const std::string& path = request.path;
    if (path == "/hitch") {
        const double longest = longestFrameIntervalMs;
        longestFrameIntervalMs = 0.0;
        return ControlResponse::ok(std::format("{{\"ok\":true,\"longestFrameMs\":{:.2f}}}", longest));
    }
    if (path == "/import") {
        std::vector<std::filesystem::path> paths;
        const std::string list = request.text("path").value_or("");
        for (size_t start = 0; start < list.size();) {
            const size_t end = std::min(list.find(importPathSeparator, start), list.size());
            if (end > start) {
                paths.push_back(pathFromUtf8(list.substr(start, end - start).c_str()));
            }
            start = end + 1;
        }
        if (paths.empty()) {
            return ControlResponse::error(400, "missing path");
        }
        flowInstance.importLevels(paths);
        return ControlResponse::ok(std::format("{{\"ok\":true,\"toast\":\"{}\"}}", jsonEscape(flowInstance.toast().text)));
    }
    if (path == "/renderer") {
        const std::string wantedRenderer = request.text("renderer").value_or(activeRenderer);
        const std::string wantedApi = request.text("api").value_or(activeGraphicsApi);
        requestRenderer(wantedRenderer, wantedApi);
        return ControlResponse::ok();
    }
    if (path == "/render-gpu") {
        const auto index = request.integer("index");
        if (!index || *index < 0 || *index >= static_cast<long long>(renderAdapterList.size())) {
            return ControlResponse::error(400, "index must name an adapter from /state renderAdapters");
        }
        requestRenderAdapter(static_cast<int>(*index));
        return ControlResponse::ok();
    }
    if (path == "/window") {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        width = request.integer("width").value_or(width);
        height = request.integer("height").value_or(height);
        SDL_SetWindowSize(window, std::max(width, minimumWindowWidth), std::max(height, minimumWindowHeight));
        return ControlResponse::ok();
    }
    return currentScreen ? currentScreen->handleControl(request) : std::nullopt;
}

void App::showMainMenu() {
    flowInstance.showMainMenu();
}

void App::showMachineBenchmark() {
    flowInstance.runMachineBenchmark();
}

void App::adoptBenchmarkWinner(const BenchmarkCandidate& winner) {
    const std::vector<BackendPick> previousPicks = backendPicks();
    setBackendPicks(winner.picks.empty() ? std::vector<BackendPick>{}
                                         : parseBackendPicks(winner.picks).value_or(std::vector<BackendPick>{}));
    if (!isBackendAvailable(winner.kind)) {
        setBackendPicks(previousPicks);
        return;
    }
    refreshBackendStatuses();
    backendChoice = winner.kind;
    currentSettings.defaultBackend = storedBackendName(winner.kind);
    static_cast<void>(saveSettings(settingsFilePath(), currentSettings));
}

void App::showSettings() {
    flowInstance.showSettings();
}

bool App::isWebGpuSupported() const { return isWebGpuCompiledIn(); }

void App::applySettings(const Settings& settings, std::optional<BackendKind> backend) {
    const bool rendererChanged = normalizeRendererName(settings.renderer) != activeRenderer ||
                                 normalizeGraphicsApiName(settings.graphicsApi) != activeGraphicsApi;
    currentSettings = settings;
    if (backend) {
        backendChoice = *backend;
        currentSettings.defaultBackend = storedBackendName(*backend);
    }
    static_cast<void>(saveSettings(settingsFilePath(), currentSettings));
    setVsync(settings.vsync);
    applyOrientationToWindow();
    notifySettingsConsumers();
    if (rendererChanged) {
        requestRenderer(normalizeRendererName(settings.renderer),
                        normalizeGraphicsApiName(settings.graphicsApi));
    }
}

void App::setAdvancedSettingsOpen(bool open) {
    currentSettings.advancedSettings = open;
    static_cast<void>(saveSettings(settingsFilePath(), currentSettings));
}

void App::notifySettingsConsumers() {
    if (auto* gameplay = dynamic_cast<GameplayScreen*>(currentScreen.get())) {
        gameplay->refreshSettings();
    }
}

void App::showLevelSelect(GameMode mode) {
    flowInstance.showLevelSelect(mode);
}

void App::startGame(GameMode mode, const LevelFiles& files) {
    flowInstance.startGame(mode, files);
}

void App::openEditor(GameMode mode, std::optional<LevelFiles> files) {
    flowInstance.openEditor(mode, std::move(files));
}
