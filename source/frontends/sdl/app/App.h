#pragma once

#include "app/LaunchOptions.h"
#include "app/Paths.h"
#include "app/Screen.h"
#include "control/ControlServer.h"
#include "game/FrameTimings.h"
#include "game/MachineProfile.h"
#include "game/Settings.h"
#include "gpu/GpuAdapters.h"
#include "level/LevelData.h"
#include "render/BallView.h"
#include "render/IRenderer.h"
#include "sim/BackendFactory.h"
#include "ui/Painter.h"
#include "ui/ScreenFlow.h"

#include <chrono>
#include <filesystem>
#include <future>
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
    App();
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

    ScreenFlow& flow() { return flowInstance; }
    const ScreenFlow& flow() const { return flowInstance; }

    const Settings& settings() const { return currentSettings; }
    void applySettings(const Settings& settings, std::optional<BackendKind> backend = std::nullopt);
    void setAdvancedSettingsOpen(bool open);

    bool isWebGpuSupported() const;
    void notifySettingsConsumers();
    void applyOrientationToWindow();
    void applyOrientationHint(LayoutOrientation orientation);

    BallView& ballRenderer() { return *balls; }
    IRenderer& sceneRenderer() { return *renderer; }
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
    void showToast(std::string text, bool isError = false);
    void openLevelImportDialog(const std::filesystem::path& startFolder);
    void openFolder(const std::filesystem::path& folder);

    const std::vector<GpuAdapterInfo>& renderAdapters() const { return renderAdapterList; }
    const std::vector<GpuAdapterInfo>& computeAdapters() const { return computeAdapterList; }
    int renderAdapterIndex() const { return activeRenderAdapter; }

    void requestRenderAdapter(int adapterIndex) { pendingRenderAdapter = adapterIndex; }
    const std::string& rendererName() const { return activeRenderer; }
    const std::string& graphicsApiName() const { return activeGraphicsApi; }
    std::string rendererDescription() const { return renderer ? renderer->deviceDescription() : ""; }
    void requestRenderer(std::string rendererChoice, std::string apiChoice) {
        pendingRendererSwitch = std::make_pair(std::move(rendererChoice), std::move(apiChoice));
    }

private:

    struct RendererTarget {
        std::string renderer;
        std::string api;
        std::string gpuPreference;
    };
    struct PreparedRenderer {
        std::unique_ptr<IRenderer> renderer;
        std::string error;
        std::vector<GpuAdapterInfo> adapters;
    };

    bool openWindowAndRenderer(const LaunchOptions& options, std::string_view api, std::string& error);
    void applyPendingRenderAdapter();
    void applyPendingRendererSwitch();
    void switchRenderer(RendererTarget target);
    void switchRendererNow(const RendererTarget& target);
    void adoptPreparedRenderer();
    void useRenderer(const RendererTarget& target, std::vector<GpuAdapterInfo> adapters);
    static void retireRenderer(std::unique_ptr<IRenderer> retired);
    using Clock = std::chrono::steady_clock;

    static void receiveImportDialogResult(void* userdata, const char* const* fileList, int filter);
    void deliverImport(std::vector<std::filesystem::path> paths);
    void deliverFinishedDialogs();
    void drawToast();
    void processFlowRequests();
    void applyFlowScreen();
    void drawFrameTimingsOverlay() const;
    void updateTextInput();

    struct ControlBridge;
    std::optional<ControlResponse> handleControl(const ControlRequest& request);
    std::optional<ControlResponse> handleOwnControl(const ControlRequest& request);

    ScreenFlow flowInstance;
    SDL_Window* window = nullptr;
    std::unique_ptr<IRenderer> renderer;
    std::unique_ptr<BallView> balls;
    gfx::FontSet fonts;
    bool imguiCreated = false;
    std::unique_ptr<Screen> currentScreen;
    std::optional<GameMode> lastLevelSelectMode;
    FrameTimings timings;
    ControlServer controlServer;

    std::shared_ptr<ControlBridge> controlBridge;
    MachineProfile profile;
    Settings currentSettings;
    BackendKind backendChoice = BackendKind::Avx2;
    bool running = true;
    bool textInputActive = false;
    bool screenshotPending = false;
    std::string screenshotPath;
    float frameSeconds = 0.0f;

    double longestFrameIntervalMs = 0.0;
    std::vector<std::filesystem::path> droppedPaths;
    std::mutex dialogMutex;
    std::vector<std::vector<std::filesystem::path>> finishedDialogs;
    std::vector<std::string> dialogErrors;

    std::string activeRenderer;
    std::string activeGraphicsApi;
    std::string renderGpuPreference;
    std::vector<GpuAdapterInfo> renderAdapterList;
    std::vector<GpuAdapterInfo> computeAdapterList;
    int activeRenderAdapter = -1;
    std::optional<int> pendingRenderAdapter;
    std::optional<std::pair<std::string, std::string>> pendingRendererSwitch;

    std::optional<RendererTarget> preparingTarget;
    std::future<PreparedRenderer> preparing;
    Clock::time_point previousUpdateStart = Clock::now();
};
