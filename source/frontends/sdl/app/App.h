#pragma once

#include "app/FrameTimings.h"
#include "app/LaunchOptions.h"
#include "app/MachineProfile.h"
#include "app/Paths.h"
#include "app/Screen.h"
#include "gpu/GpuAdapters.h"
#include "ui/Painter.h"
#include "control/ControlServer.h"
#include "game/Settings.h"
#include "level/LevelData.h"
#include "render/BallView.h"
#include "render/IRenderer.h"
#include "sim/BackendFactory.h"
#include "ui/Painter.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;
union SDL_Event;
struct ImFont;

int currentRefreshRate();

class App {
public:
    ~App();

    bool initialize(const LaunchOptions& options, std::string& error);
    bool frame();
    void handleEvent(const SDL_Event& event);

    void showMainMenu();
    void showMachineBenchmark();
    void showLevelSelect(GameMode mode);
    void startGame(GameMode mode, const LevelFiles& files);
    void openEditor(GameMode mode, std::optional<LevelFiles> files);
    void showSettings();
    void quit() { running = false; }

    const Settings& settings() const { return currentSettings; }
    void applySettings(const Settings& settings, std::optional<BackendKind> backend = std::nullopt);
    std::string settingsJson() const;
    // Whether this build links a WebGPU renderer, so the Settings page can offer it.
    bool isWebGpuSupported() const;
    void notifySettingsConsumers();
    void applyOrientationToWindow();
    void applyOrientationHint(LayoutOrientation orientation);

    BallView& ballRenderer() { return *balls; }
    const FrameTimings& frameTimings() const { return timings; }
    float lastFrameSeconds() const { return frameSeconds; }
    BackendKind preferredBackend() const { return backendChoice; }
    void setPreferredBackend(BackendKind kind) { backendChoice = kind; }
    void adoptBenchmarkWinner(const BenchmarkCandidate& winner);
    std::span<const CandidateResult> benchmarkScores() const {
        return profile.benchmark() ? std::span<const CandidateResult>(profile.benchmark()->results)
                                   : std::span<const CandidateResult>{};
    }
    bool isVsyncEnabled() const { return renderer && renderer->isVsyncEnabled(); }
    MachineProfile& machineProfile() { return profile; }
    void setVsync(bool enabled);
    void showToast(std::string text, bool isError);
    void openLevelImportDialog(const std::filesystem::path& startFolder);
    void openFolder(const std::filesystem::path& folder);

    const std::vector<GpuAdapterInfo>& renderAdapters() const { return renderAdapterList; }
    const std::vector<GpuAdapterInfo>& computeAdapters() const { return computeAdapterList; }
    int renderAdapterIndex() const { return activeRenderAdapter; }
    // Swaps the renderer onto another adapter at the end of the frame; the window and the game stay.
    void requestRenderAdapter(int adapterIndex) { pendingRenderAdapter = adapterIndex; }
    const std::string& rendererName() const { return activeRenderer; }
    const std::string& graphicsApiName() const { return activeGraphicsApi; }
    std::string rendererDescription() const { return renderer ? renderer->deviceDescription() : ""; }
    void requestRenderer(std::string rendererChoice, std::string apiChoice) {
        pendingRendererSwitch = std::make_pair(std::move(rendererChoice), std::move(apiChoice));
    }

private:
    bool openWindowAndRenderer(const LaunchOptions& options, std::string_view api, std::string& error);
    void applyPendingRenderAdapter();
    void applyPendingRendererSwitch();
    using Clock = std::chrono::steady_clock;

    struct Toast {
        std::string text;
        float secondsLeft = 0.0f;
        bool isError = false;
    };

    static void receiveImportDialogResult(void* userdata, const char* const* fileList, int filter);
    void deliverImport(std::vector<std::filesystem::path> paths);
    void deliverFinishedDialogs();
    void drawToast();
    void applyPendingScreen();
    void drawFrameTimingsOverlay() const;
    void updateTextInput();
    std::optional<ControlResponse> handleControl(const ControlRequest& request);
    std::string stateJson() const;
    std::string benchmarkStateJson() const;

    SDL_Window* window = nullptr;
    std::unique_ptr<IRenderer> renderer;
    std::unique_ptr<BallView> balls;
    gfx::FontSet fonts;
    bool imguiCreated = false;
    std::unique_ptr<Screen> currentScreen;
    std::unique_ptr<Screen> pendingScreen;
    FrameTimings timings;
    ControlServer controlServer;
    MachineProfile profile;
    Settings currentSettings;
    BackendKind backendChoice = BackendKind::Avx2;
    bool running = true;
    bool textInputActive = false;
    bool screenshotPending = false;
    std::string screenshotPath;
    float frameSeconds = 0.0f;
    Toast toast;
    std::vector<std::filesystem::path> droppedPaths;
    std::mutex dialogMutex;
    std::vector<std::vector<std::filesystem::path>> finishedDialogs;
    std::vector<std::string> dialogErrors;
    // Empty until the first renderer opens, so the build's own default renderer fills it in.
    std::string activeRenderer;
    std::string activeGraphicsApi;
    std::string renderGpuPreference;
    std::vector<GpuAdapterInfo> renderAdapterList;
    std::vector<GpuAdapterInfo> computeAdapterList;
    int activeRenderAdapter = -1;
    std::optional<int> pendingRenderAdapter;
    std::optional<std::pair<std::string, std::string>> pendingRendererSwitch;
    Clock::time_point previousUpdateStart = Clock::now();
};
