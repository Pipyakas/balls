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
constexpr float toastSeconds = 3.0f;
constexpr char importPathSeparator = '|';
constexpr float toastTextSize = 24.0f;
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

constexpr const char* targetBenchmarkFileName = "target-benchmark.txt";

// The scoring machine's machine-benchmark record, measured with balls_bench --machine-benchmark and
// shipped with the assets, so the game starts on the backend that is fastest where it is judged.
std::optional<BenchmarkCandidate> targetMachineWinner() {
    std::ifstream file(assetRoot() / targetBenchmarkFileName, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const std::optional<BenchmarkRecord> record = parseBenchmarkRecord(text.str());
    return record ? record->winner : std::nullopt;
}

// The command line wins, then the Settings default (this machine's own benchmark winner or the player's pick), then
// the scoring machine's winner, then the fastest CPU backend, skipping any that cannot load on this machine.
BackendKind startupBackend(std::optional<BackendKind> commandLine, std::string_view settingsDefault) {
    if (commandLine && isBackendAvailable(*commandLine)) {
        return *commandLine;
    }
    if (const auto saved = parseBackendKind(settingsDefault); saved && isBackendAvailable(*saved)) {
        return *saved;
    }
    if (const auto target = targetMachineWinner(); target && isBackendAvailable(target->kind)) {
        return target->kind;
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
    profile.load();
    setComputeGpuPreference(options.computeGpu);
    computeAdapterList = enumerateGpuAdapters(GpuApi::Vulkan);
    applyOrientationHint(currentSettings.layout);
    setVsync(effective.vsync || currentSettings.vsync);
    backendChoice = startupBackend(options.backend, currentSettings.defaultBackend);
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
    } else if (event.type == SDL_EVENT_DROP_BEGIN) {
        droppedPaths.clear();
    } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
        droppedPaths.push_back(pathFromUtf8(event.drop.data));
    } else if (event.type == SDL_EVENT_DROP_COMPLETE && !droppedPaths.empty()) {
        deliverImport(std::move(droppedPaths));
        droppedPaths.clear();
    }
}

void App::showToast(std::string text, bool isError) { toast = {std::move(text), toastSeconds, isError}; }

void App::deliverImport(std::vector<std::filesystem::path> paths) {
    if (!currentScreen || !currentScreen->importLevels(paths)) {
        showToast("Open Level select to import levels", false);
    }
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
    toast.secondsLeft = std::max(0.0f, toast.secondsLeft - frameSeconds);
    if (toast.secondsLeft <= 0.0f) {
        return;
    }
    const float alpha = std::min(1.0f, toast.secondsLeft);
    const float maximumWidth = static_cast<float>(gfx::screenWidth()) - 2.0f * toastMargin;
    const float size = std::min(toastTextSize, toastTextSize * maximumWidth / std::max(ui::measureText(toast.text, toastTextSize).x, 1.0f));
    const Vec2 extent = ui::measureText(toast.text, size);
    const Rect rect{(static_cast<float>(gfx::screenWidth()) - extent.x) * 0.5f - toastPadding, currentScreen->toastTop(),
                    extent.x + 2.0f * toastPadding, toastHeight};
    ui::drawPanel(rect, gfx::fade(toast.isError ? palette::danger : palette::panelLight, alpha), 0.4f);
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
    timings.record({millisecondsBetween(updateStart, frameEnd),
                    millisecondsBetween(workStart, presentStart),
                    renderer->lastGpuBusyMs(),
                    millisecondsBetween(presentStart, frameEnd) + gpuWaitMs,
                    renderer->lastFramePresented() ? 1.0 : 0.0, renderer->lastPresentWaitMs()});
    applyPendingRenderAdapter();
    applyPendingRendererSwitch();
    adoptPreparedRenderer();
    applyPendingScreen();
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
                  gfx::fade(palette::backgroundDeep, 0.85f));
    ui::drawText(text, {x, y}, 18.0f, palette::textMuted);
}

void App::applyPendingScreen() {
    if (pendingScreen) {
        currentScreen = std::move(pendingScreen);
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

std::string App::stateJson() const {
    const FrameSample& sample = timings.average();
    return std::format(
        "{{\"ok\":true,\"frontend\":\"sdl\",\"renderer\":\"{}\",\"screen\":\"{}\",\"window\":[{},{}],"
        "\"fps\":{:.1f},\"minFps\":{:.1f},\"workFps\":{:.1f},\"minWorkFps\":{:.1f},\"shownFps\":{:.1f},\"frameMs\":{:.3f},\"cpuMs\":{:.3f},\"gpuMs\":{:.3f},"
        "\"presentMs\":{:.3f},\"preferredBackend\":\"{}\",\"computeGpu\":\"{}\",\"vsync\":{},\"toast\":\"{}\","
        "\"renderAdapters\":{},\"computeAdapters\":{},\"details\":{}}}",
        jsonEscape(renderer->deviceDescription()), currentScreen->name(), gfx::screenWidth(),
        gfx::screenHeight(), timings.framesPerSecond(), timings.worstFpsLastSecond(),
        timings.workFramesPerSecond(), timings.worstWorkFpsLastSecond(), timings.shownFramesPerSecond(),
        sample.frameMs,
        sample.cpuMs, sample.gpuMs, sample.presentMs, backendKindName(backendChoice),
        jsonEscape(backendStatus(BackendKind::Vulkan).detail), isVsyncEnabled(),
        jsonEscape(toast.secondsLeft > 0.0f ? toast.text : std::string{}), adapterNamesJson(renderAdapterList),
        adapterNamesJson(computeAdapterList), currentScreen->stateJson());
}

std::optional<ControlResponse> App::handleControl(const ControlRequest& request) {
    const std::string& path = request.path;
    if (path == "/state") {
        return ControlResponse::ok(stateJson());
    }
    if (path == "/hitch") {
        const double longest = longestFrameIntervalMs;
        longestFrameIntervalMs = 0.0;
        return ControlResponse::ok(std::format("{{\"ok\":true,\"longestFrameMs\":{:.2f}}}", longest));
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
        deliverImport(std::move(paths));
        return ControlResponse::ok(std::format("{{\"ok\":true,\"toast\":\"{}\"}}", jsonEscape(toast.text)));
    }
    if (path == "/renderer") {
        const std::string wantedRenderer = request.text("renderer").value_or(activeRenderer);
        const std::string wantedApi = request.text("api").value_or(activeGraphicsApi);
        requestRenderer(wantedRenderer, wantedApi);
        return ControlResponse::ok("{\"ok\":true}");
    }
    if (path == "/render-gpu") {
        const auto index = request.integer("index");
        if (!index || *index < 0 || *index >= static_cast<long long>(renderAdapterList.size())) {
            return ControlResponse::error(400, "index must name an adapter from /state renderAdapters");
        }
        requestRenderAdapter(static_cast<int>(*index));
        return ControlResponse::ok("{\"ok\":true}");
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
        if (const auto advanced = request.integer("advanced")) {
            next.advancedSettings = *advanced != 0;
        }
        if (const auto renderer = request.text("renderer")) {
            next.renderer = normalizeRendererName(*renderer);
        }
        if (const auto api = request.text("graphicsApi")) {
            next.graphicsApi = normalizeGraphicsApiName(*api);
        } else if (const auto api = request.text("api")) {
            next.graphicsApi = normalizeGraphicsApiName(*api);
        }
        std::optional<BackendKind> backend;
        if (const auto name = request.text("backend")) {
            backend = parseBackendKind(*name);
            if (!backend || !backendStatus(*backend).available) {
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

bool App::isWebGpuSupported() const { return isWebGpuCompiledIn(); }

void App::applySettings(const Settings& settings, std::optional<BackendKind> backend) {
    const bool rendererChanged = normalizeRendererName(settings.renderer) != activeRenderer ||
                                 normalizeGraphicsApiName(settings.graphicsApi) != activeGraphicsApi;
    currentSettings = settings;
    if (backend) {
        backendChoice = *backend;
        currentSettings.defaultBackend = std::string(backendKindName(*backend));
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

std::string App::settingsJson() const {
    return std::format(
        "{{\"ballCollisions\":{},\"layout\":\"{}\",\"vsync\":{},\"defaultBackend\":\"{}\",\"renderer\":\"{}\","
        "\"graphicsApi\":\"{}\",\"advancedSettings\":{}}}",
        currentSettings.ballCollisions, orientationName(currentSettings.layout), currentSettings.vsync,
        jsonEscape(currentSettings.defaultBackend), normalizeRendererName(currentSettings.renderer),
        normalizeGraphicsApiName(currentSettings.graphicsApi), currentSettings.advancedSettings);
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
