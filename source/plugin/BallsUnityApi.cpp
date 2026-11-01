#include "BallsUnityApi.h"

#include "common/BallColor.h"
#include "editor/EditorSession.h"
#include "editor/LevelEditor.h"
#include "control/ControlApi.h"
#include "game/FrameTimings.h"
#include "game/GameSession.h"
#include "game/PhysicsRate.h"
#include "game/SessionRunner.h"
#include "game/MachineProfile.h"
#include "game/Settings.h"
#include "level/LevelImporter.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "level/LevelWatcher.h"
#include "level/LevelWriter.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"
#include "sim/BackendSelection.h"
#include "sim/BackendWorker.h"
#include "sim/DeviceInventory.h"
#include "sim/Simulation.h"
#include "ui/GameplayLayout.h"
#include "ui/MenuModel.h"
#include "ui/ScreenFlow.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

thread_local std::string lastError;

void setError(std::string message) {
    lastError = std::move(message);
}

template <typename T>
int32_t writeText(const T& text, char* buffer, int32_t size) {
    if (buffer == nullptr || size <= 0) {
        return 0;
    }
    const size_t length = std::min(text.size(), static_cast<size_t>(size - 1));
    std::memcpy(buffer, text.data(), length);
    buffer[length] = '\0';
    return static_cast<int32_t>(length);
}

void copyName(const std::string& name, char* destination, size_t size) {
    const size_t length = std::min(name.size(), size - 1);
    std::memcpy(destination, name.data(), length);
    destination[length] = '\0';
}

void copyPath(const std::filesystem::path& path, char* destination, size_t size) {
    copyName(path.string(), destination, size);
}

BackendKind toBackendKind(int32_t kind) {
    switch (kind) {
    case BALLS_BACKEND_AVX2:
        return BackendKind::Avx2;
    case BALLS_BACKEND_CUDA:
        return BackendKind::Cuda;
    case BALLS_BACKEND_HYBRID:
        return BackendKind::Hybrid;
    case BALLS_BACKEND_VULKAN:
        return BackendKind::Vulkan;
    default:
        return BackendKind::Scalar;
    }
}

int32_t fromBackendKind(BackendKind kind) {
    switch (kind) {
    case BackendKind::Avx2:
        return BALLS_BACKEND_AVX2;
    case BackendKind::Cuda:
        return BALLS_BACKEND_CUDA;
    case BackendKind::Hybrid:
        return BALLS_BACKEND_HYBRID;
    case BackendKind::Vulkan:
        return BALLS_BACKEND_VULKAN;
    case BackendKind::Scalar:
    default:
        return BALLS_BACKEND_SCALAR;
    }
}

void fromCoreSettings(const Settings& from, BallsSettings& to) {
    to.ballCollisions = from.ballCollisions ? 1 : 0;
    to.vsync = from.vsync ? 1 : 0;
    to.advancedSettings = from.advancedSettings ? 1 : 0;
    to.orientation = from.layout == LayoutOrientation::Landscape  ? BALLS_ORIENTATION_LANDSCAPE
                     : from.layout == LayoutOrientation::Portrait ? BALLS_ORIENTATION_PORTRAIT
                                                                  : BALLS_ORIENTATION_AUTO;
    copyName(from.defaultBackend, to.defaultBackend, sizeof(to.defaultBackend));
}

Settings toCoreSettings(const BallsSettings& from) {
    Settings to;
    to.ballCollisions = from.ballCollisions != 0;
    to.vsync = from.vsync != 0;
    to.advancedSettings = from.advancedSettings != 0;
    to.layout = from.orientation == BALLS_ORIENTATION_LANDSCAPE  ? LayoutOrientation::Landscape
                : from.orientation == BALLS_ORIENTATION_PORTRAIT ? LayoutOrientation::Portrait
                                                                : LayoutOrientation::Auto;
    to.defaultBackend = std::string(from.defaultBackend, strnlen(from.defaultBackend, sizeof(from.defaultBackend)));
    return to;
}

std::optional<LevelData> loadLevelOrReport(const char* directory, const char* name) {
    if (directory == nullptr || name == nullptr) {
        setError("level directory and name are required");
        return std::nullopt;
    }
    const LevelFiles files = levelFilesFor(std::filesystem::path(directory), name);
    auto loaded = loadLevel(files);
    if (!loaded) {
        setError(loaded.error());
        return std::nullopt;
    }
    return std::move(*loaded);
}

}

struct BallsSession {

    std::unique_ptr<GameSession> game;
    std::unique_ptr<SessionRunner> runner;
    std::shared_ptr<const SessionView> view;
    PhysicsFrameInput frameInput;

    PhysicsRate rate;
    int visibilityWidth = 0;
    std::vector<int32_t> collisionChanges;
    std::vector<SessionEvent> switchEvents;
    GameMode mode = GameMode::Normal;
    int playableHeight = 0;
    std::string levelDirectory;
    std::string levelName;

    bool threaded() const { return runner != nullptr; }

    std::shared_ptr<const SessionView> latestView() const {
        if (runner) {
            return view;
        }
        auto described = std::make_shared<SessionView>();
        describeSession(*game, *described);
        described->physicsFrameMs = rate.lastFrameMs();
        described->physicsFramesPerSecond = rate.framesPerSecond();
        described->physicsTicksPerSecond = rate.ticksPerSecond();
        return described;
    }

    void apply(SessionRunner::Command command) {
        if (runner) {
            runner->post(std::move(command));
        } else {
            command(*game);
        }
    }
    const BrickBoard* board() const {
        if (runner) {
            return view && view->board ? view->board.get() : nullptr;
        }
        return &game->simulation().board();
    }
    const LauncherQueue* launchers() const {
        if (runner) {
            return view ? &view->launchers : nullptr;
        }
        return &game->launchers();
    }

    std::vector<int32_t> brickHitsLeft;
    std::vector<float> brickRiseOffset;
    std::vector<float> brickHitFlash;

    void refreshBoardScratch() {
        const std::vector<Brick>& bricks = board()->bricks();
        brickHitsLeft.resize(bricks.size());
        brickRiseOffset.resize(bricks.size());
        brickHitFlash.resize(bricks.size());
        for (size_t index = 0; index < bricks.size(); ++index) {
            brickHitsLeft[index] = bricks[index].hitsLeft;
            brickRiseOffset[index] = bricks[index].riseOffset;
            brickHitFlash[index] = bricks[index].hitFlash;
        }
    }
};

struct BallsBenchmark {
    std::unique_ptr<BackendBenchmark> run;
    std::vector<CandidateResult> results;

    void refresh() { results = run != nullptr ? run->results() : std::vector<CandidateResult>{}; }
};

struct BallsLevelWatcher {
    LevelWatcher watcher;
};

int32_t fromDiskState(LevelDiskState state) {
    switch (state) {
    case LevelDiskState::Changed:
        return BALLS_DISK_CHANGED;
    case LevelDiskState::Removed:
        return BALLS_DISK_REMOVED;
    case LevelDiskState::Unchanged:
        break;
    }
    return BALLS_DISK_UNCHANGED;
}

struct BallsFlow {
    ScreenFlow flow;
};

GameMode toGameMode(int32_t mode) { return mode == BALLS_MODE_PERFORMANCE ? GameMode::Performance : GameMode::Normal; }

int32_t fromGameMode(GameMode mode) { return mode == GameMode::Performance ? BALLS_MODE_PERFORMANCE : BALLS_MODE_NORMAL; }

void fillLevelInfo(const LevelFiles& files, BallsLevelInfo& info) {
    copyName(files.name, info.name, sizeof info.name);
    copyPath(files.bricksPath, info.bricksPath, sizeof info.bricksPath);
    copyPath(files.launchersPath, info.launchersPath, sizeof info.launchersPath);
}

bool hasText(const char* text) { return text != nullptr && text[0] != 0; }

struct BallsEditorSession {
    EditorSession session;
};

struct BallsFrameTimings {
    FrameTimings timings;
};

struct BallsProfile {
    MachineProfile profile;
};

namespace {

std::unique_ptr<BallsSession> newSession(int32_t mode, const char* levelDirectory, const char* levelName,
                                         const LevelData& level) {
    auto session = std::make_unique<BallsSession>();
    session->levelDirectory = levelDirectory;
    session->levelName = levelName;
    session->mode = mode == BALLS_MODE_PERFORMANCE ? GameMode::Performance : GameMode::Normal;
    session->playableHeight = level.bricks.playableHeight;
    return session;
}

int32_t fromCollisionsChange(BallCollisionsChange change) {
    switch (change) {
    case BallCollisionsChange::SwitchingToScalar:
        return BALLS_COLLISIONS_SWITCHING_TO_SCALAR;
    case BallCollisionsChange::ScalarNotOffered:
        return BALLS_COLLISIONS_SCALAR_NOT_OFFERED;
    case BallCollisionsChange::None:
        break;
    }
    return BALLS_COLLISIONS_UNCHANGED;
}

int32_t fromSessionState(SessionState state) {
    switch (state) {
    case SessionState::Running:
        return BALLS_STATE_RUNNING;
    case SessionState::Won:
        return BALLS_STATE_WON;
    case SessionState::Ready:
    default:
        return BALLS_STATE_READY;
    }
}

}

struct BallsControl final : ControlHost {
    struct Game final : ControlGame {
        BallsControl& owner;
        explicit Game(BallsControl& control) : owner(control) {}

        const BallsControlContext& context() const { return *owner.context; }
        GameMode gameMode() const override { return context().mode == BALLS_MODE_PERFORMANCE ? GameMode::Performance : GameMode::Normal; }
        std::string levelName() const override { return context().levelName ? context().levelName : ""; }
        std::string levelLoadError() const override { return context().loadError ? context().loadError : ""; }
        LevelDiskState diskState() const override {
            return context().diskState == BALLS_DISK_CHANGED   ? LevelDiskState::Changed
                   : context().diskState == BALLS_DISK_REMOVED ? LevelDiskState::Removed
                                                               : LevelDiskState::Unchanged;
        }
        std::shared_ptr<const SessionView> sessionView() override {
            BallsSession* session = context().session;
            return session != nullptr ? session->latestView() : nullptr;
        }
        void post(std::function<void(GameSession&)> command) override {
            if (context().session != nullptr) {
                context().session->apply(std::move(command));
            }
        }
        bool isPaused() const override { return context().paused != 0; }
        void setPaused(bool paused) override { owner.queue({BALLS_CONTROL_SET_PAUSED, 0, paused ? 1 : 0}); }
        void restart() override { owner.queue({BALLS_CONTROL_RESTART}); }

    };

    std::filesystem::path normalLevels;
    std::filesystem::path performanceLevels;
    const BallsControlContext* context = nullptr;
    Settings currentSettings;
    Game gameInPlay{*this};
    std::vector<BallsControlAction> actions;

    void queue(BallsControlAction action, std::string_view text = {}) {
        copyName(std::string(text), action.text, sizeof(action.text));
        actions.push_back(action);
    }
    static int32_t modeOf(GameMode mode) { return mode == GameMode::Performance ? BALLS_MODE_PERFORMANCE : BALLS_MODE_NORMAL; }
    static std::string textOf(const char* text) { return text != nullptr ? text : ""; }

    ControlFrontEndState frontEndState() const override {
        ControlFrontEndState state;
        state.frontEnd = "unity";
        state.renderer = textOf(context->renderer);
        state.screen = context->screen != nullptr ? context->screen : "main-menu";
        state.windowWidth = context->windowWidth;
        state.windowHeight = context->windowHeight;
        state.frames = context->frames != nullptr ? &context->frames->timings : nullptr;
        state.preferredBackend = toBackendKind(context->preferredBackend);
        state.vsync = context->vsync != 0;
        state.toast = textOf(context->toast);
        state.extraFields = textOf(context->extraFields);
        state.screenDetails = textOf(context->screenDetails);
        return state;
    }
    ControlGame* game() override {
        return context->session != nullptr || (context->loadError != nullptr && *context->loadError != 0) ? &gameInPlay : nullptr;
    }
    std::filesystem::path levelDirectory(GameMode mode) const override {
        return mode == GameMode::Performance ? performanceLevels : normalLevels;
    }

    std::optional<ControlResponse> takeScreenshot(const std::string& path) override {
        queue({BALLS_CONTROL_SCREENSHOT}, path);
        return std::nullopt;
    }
    void play(GameMode mode, const LevelFiles& level) override { queue({BALLS_CONTROL_PLAY, modeOf(mode)}, level.name); }
    void showMainMenu() override { queue({BALLS_CONTROL_MAIN_MENU}); }
    bool showLevelSelect(GameMode mode) override {
        queue({BALLS_CONTROL_LEVEL_SELECT, modeOf(mode)});
        return true;
    }
    bool openEditor(GameMode mode, const std::optional<LevelFiles>& level) override {
        queue({BALLS_CONTROL_EDITOR, modeOf(mode)}, level ? level->name : std::string());
        return true;
    }
    void quit() override { queue({BALLS_CONTROL_QUIT}); }

    const Settings& settings() const override { return currentSettings; }
    void applySettings(const Settings& next, std::optional<BackendKind> defaultBackend) override {
        currentSettings = next;
        BallsControlAction action{BALLS_CONTROL_APPLY_SETTINGS, 0, defaultBackend ? fromBackendKind(*defaultBackend) : -1};
        fromCoreSettings(next, action.settings);
        queue(action);
    }
    void openSettings() override { queue({BALLS_CONTROL_OPEN_SETTINGS}); }
    void setVsync(bool enabled) override { queue({BALLS_CONTROL_SET_VSYNC, 0, enabled ? 1 : 0}); }

    ControlBenchmarkState benchmarkState() const override {
        if (context->benchmark != nullptr && context->benchmark->run != nullptr) {
            context->benchmark->refresh();
            const bool finished = context->benchmark->run->finished();
            ControlBenchmarkState state{finished ? "done" : "running", context->benchmark->results, std::nullopt};
            if (finished) {
                state.winner = chooseDefaultBackend(state.results, [](BackendKind kind) { return isBackendAvailable(kind); });
            }
            return state;
        }
        if (context->profile == nullptr || !context->profile->profile.benchmark()) {
            return {};
        }
        const BenchmarkRecord& record = *context->profile->profile.benchmark();
        return {record.complete ? "done" : "skipped", record.results, record.winner};
    }
    void runBenchmark() override { queue({BALLS_CONTROL_RUN_BENCHMARK}); }
    bool skipBenchmark() override {
        queue({BALLS_CONTROL_SKIP_BENCHMARK});
        return true;
    }
    bool continueFromBenchmark() override {
        queue({BALLS_CONTROL_CONTINUE_BENCHMARK});
        return true;
    }
};

extern "C" {

int32_t balls_abi_version(void) {
    return BALLS_UNITY_ABI_VERSION;
}

void balls_set_plugin_directory(const char* directory) {
    if (directory == nullptr) {
        setPluginSearchDirectory({});
        return;
    }
    setPluginSearchDirectory(std::filesystem::path(directory));
}

int32_t balls_last_error(char* buffer, int32_t size) {
    return writeText(lastError, buffer, size);
}

float balls_render_pack_scale(float fieldExtent) {
    return renderPackScale(fieldExtent);
}

namespace {

uint32_t packThemeColor(ThemeColor color) {
    return (uint32_t{color.r} << 24) | (uint32_t{color.g} << 16) | (uint32_t{color.b} << 8) | uint32_t{color.a};
}

}

uint32_t balls_theme_color(int32_t id) {
    static constexpr ThemeColor colors[BALLS_THEME_COLOR_COUNT] = {
        theme::background, theme::boardBackdrop, theme::panel,     theme::control,   theme::controlHover,
        theme::controlPressed, theme::border,    theme::text,      theme::textStrong, theme::textMuted,
        theme::textDim,    theme::accent,        theme::danger,    theme::success,
    };
    return id >= 0 && id < BALLS_THEME_COLOR_COUNT ? packThemeColor(colors[id]) : 0;
}

uint32_t balls_theme_ball_color(int32_t colorIndex) { return packThemeColor(theme::ballColor(colorIndex)); }

uint32_t balls_theme_brick_color(int32_t colorIndex) { return packThemeColor(theme::brickColor(colorIndex)); }

float balls_theme_value(int32_t id) {
    static constexpr float values[BALLS_THEME_VALUE_COUNT] = {
        theme::brickShade,    theme::brickHitFlash, theme::brickHitFlashSeconds, theme::toastSeconds,
        theme::toastTextSize, theme::optionRowHeight, theme::optionRowGap,      theme::advancedHeaderHeight,
        theme::displaySize,   theme::titleSize,     theme::largeSize,           theme::headingSize,
        theme::bigSize,       theme::mediumSize,    theme::bodySize,            theme::headerSize,
        theme::labelSize,     theme::valueSize,     theme::smallSize,           theme::tinySize,
    };
    return id >= 0 && id < BALLS_THEME_VALUE_COUNT ? values[id] : 0.0f;
}

void balls_compute_layout(float frameWidth, float frameHeight, int32_t boardColumns, int32_t boardRows,
                          int32_t orientation, int32_t launcherRows, BallsLayout* layout) {
    if (layout == nullptr) {
        return;
    }
    const LayoutOrientation direction = orientation == BALLS_ORIENTATION_LANDSCAPE  ? LayoutOrientation::Landscape
                                        : orientation == BALLS_ORIENTATION_PORTRAIT ? LayoutOrientation::Portrait
                                                                                   : LayoutOrientation::Auto;
    const CoreGameplayLayout computed =
        computeCoreGameplayLayout(frameWidth, frameHeight, boardColumns, boardRows, direction, launcherRows);
    layout->header[0] = computed.header.x;
    layout->header[1] = computed.header.y;
    layout->header[2] = computed.header.width;
    layout->header[3] = computed.header.height;
    layout->main[0] = computed.main.x;
    layout->main[1] = computed.main.y;
    layout->main[2] = computed.main.width;
    layout->main[3] = computed.main.height;
    layout->interaction[0] = computed.interaction.x;
    layout->interaction[1] = computed.interaction.y;
    layout->interaction[2] = computed.interaction.width;
    layout->interaction[3] = computed.interaction.height;
    layout->debug[0] = computed.debug.x;
    layout->debug[1] = computed.debug.y;
    layout->debug[2] = computed.debug.width;
    layout->debug[3] = computed.debug.height;
    layout->boardOrigin[0] = computed.boardOrigin.x;
    layout->boardOrigin[1] = computed.boardOrigin.y;
    layout->boardRect[0] = computed.boardRect.x;
    layout->boardRect[1] = computed.boardRect.y;
    layout->boardRect[2] = computed.boardRect.width;
    layout->boardRect[3] = computed.boardRect.height;
    layout->cellSize = computed.cellSize;
    layout->portrait = computed.isPortrait ? 1 : 0;
}

int32_t balls_backend_available(int32_t kind) {
    return isBackendAvailable(toBackendKind(kind)) ? 1 : 0;
}

int32_t balls_backend_name(int32_t kind, char* buffer, int32_t size) {
    return writeText(backendKindName(toBackendKind(kind)), buffer, size);
}

int32_t balls_backend_reason(int32_t kind, char* buffer, int32_t size) {
    return writeText(backendUnavailableReason(toBackendKind(kind)), buffer, size);
}

int32_t balls_backend_detail(int32_t kind, char* buffer, int32_t size) {
    return writeText(backendDetail(toBackendKind(kind)), buffer, size);
}

int32_t balls_fastest_cpu_backend(void) {
    return fromBackendKind(fastestAvailableCpuBackend());
}

int32_t balls_devices_find(BallsDeviceInfo* devices, int32_t capacity) {

    const std::vector<PhysicalDevice>& found = physicalDevices();
    const int32_t count = static_cast<int32_t>(found.size());
    if (devices == nullptr) {
        return count;
    }
    const int32_t written = std::min(count, std::max(capacity, 0));
    for (int32_t index = 0; index < written; ++index) {
        const PhysicalDevice& device = found[static_cast<size_t>(index)];
        BallsDeviceInfo& out = devices[index];
        std::memset(&out, 0, sizeof out);
        copyName(device.id, out.id, sizeof out.id);
        copyName(device.name, out.name, sizeof out.name);
        out.type = static_cast<int32_t>(device.type);
        out.memoryBytes = device.memoryBytes;
        out.vulkanDevice = -1;
        out.cudaDevice = -1;
        if (const DeviceBackendOption* option = device.option(BackendKind::Vulkan)) {
            out.vulkanDevice = option->pluginDevice;
        }
        if (const DeviceBackendOption* option = device.option(BackendKind::Cuda)) {
            out.cudaDevice = option->pluginDevice;
        }
    }
    return written;
}

extern "C++" {
namespace {

std::vector<BackendPick> toPicks(const BallsPick* picks, int32_t count) {
    std::vector<BackendPick> found;
    if (picks == nullptr) {
        return found;
    }
    found.reserve(static_cast<size_t>(std::max(count, 0)));
    for (int32_t index = 0; index < count; ++index) {
        found.push_back(BackendPick{toBackendKind(picks[index].kind), picks[index].device});
    }
    return found;
}

int32_t writePicks(std::span<const BackendPick> picks, BallsPick* out, int32_t capacity) {
    const int32_t count = static_cast<int32_t>(picks.size());
    if (out == nullptr) {
        return count;
    }
    const int32_t written = std::min(count, std::max(capacity, 0));
    for (int32_t index = 0; index < written; ++index) {
        BallsPick& pick = out[index];
        std::memset(&pick, 0, sizeof pick);
        copyName(picks[static_cast<size_t>(index)].device, pick.device, sizeof pick.device);
        pick.kind = fromBackendKind(picks[static_cast<size_t>(index)].kind);
    }
    return written;
}

}
}

int32_t balls_picks_get(BallsPick* picks, int32_t capacity) {
    return writePicks(backendPicks(), picks, capacity);
}

int32_t balls_picks_display(int32_t runningKind, BallsPick* picks, int32_t capacity) {
    return writePicks(displayedPicks(toBackendKind(runningKind)), picks, capacity);
}

int32_t balls_picks_set(const BallsPick* picks, int32_t count, char* problem, int32_t problemSize) {
    const std::vector<BackendPick> wanted = toPicks(picks, count);
    const std::string why = backendPicksProblem(wanted);
    if (!why.empty()) {
        writeText(why, problem, problemSize);
        return 0;
    }
    setBackendPicks(wanted);
    writeText(std::string_view{}, problem, problemSize);
    return 1;
}

int32_t balls_pickable_kinds(const char* device, int32_t* kinds, int32_t capacity) {
    const PhysicalDevice* found = device == nullptr ? nullptr : findPhysicalDevice(device);
    if (found == nullptr) {
        return 0;
    }
    const std::vector<BackendKind> offered = pickableKinds(*found);
    const int32_t count = static_cast<int32_t>(offered.size());
    if (kinds == nullptr) {
        return count;
    }
    const int32_t written = std::min(count, std::max(capacity, 0));
    for (int32_t index = 0; index < written; ++index) {
        kinds[index] = fromBackendKind(offered[static_cast<size_t>(index)]);
    }
    return written;
}

int32_t balls_default_hybrid_picks(BallsPick* picks, int32_t capacity) {
    return writePicks(defaultHybridPicks(), picks, capacity);
}

int32_t balls_picks_label(const BallsPick* picks, int32_t count, char* buffer, int32_t size) {
    return writeText(backendPicksLabel(toPicks(picks, count)), buffer, size);
}

int32_t balls_kind_for_picks(const BallsPick* picks, int32_t count) {
    return fromBackendKind(kindForPicks(toPicks(picks, count)));
}

int32_t balls_backend_stored_name(int32_t kind, char* buffer, int32_t size) {
    return writeText(storedBackendName(toBackendKind(kind)), buffer, size);
}

int32_t balls_backend_parse_saved(const char* text) {
    if (text == nullptr) {
        return -1;
    }
    const std::optional<BackendKind> kind = parseSavedBackendKind(text);
    if (!kind) {
        return -1;
    }

    return fromBackendKind(offeredBackend(*kind));
}

int32_t balls_compute_gpu(char* buffer, int32_t size) {
    return writeText(computeGpuPreference(), buffer, size);
}

int32_t balls_set_compute_gpu(const char* preference) {
    if (preference == nullptr) {
        return 0;
    }

    const std::string wanted(preference);
    std::string lowered;
    lowered.reserve(wanted.size());
    for (char letter : wanted) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(letter))));
    }
    const bool automatic = lowered.empty() || lowered == "auto" || lowered == "integrated" || lowered == "discrete";
    const bool numeric = !lowered.empty() &&
                         std::all_of(lowered.begin(), lowered.end(), [](char letter) { return letter >= '0' && letter <= '9'; });
    if (!automatic && !numeric && findPhysicalDevice(wanted) == nullptr) {
        return 0;
    }
    setComputeGpuPreference(wanted);
    return 1;
}

int32_t balls_levels_find(const char* directory, BallsLevelInfo* levels, int32_t capacity) {
    if (directory == nullptr) {
        setError("level directory is required");
        return 0;
    }
    const std::vector<LevelFiles> found = findLevels(std::filesystem::path(directory));
    const int32_t count = static_cast<int32_t>(found.size());
    if (levels == nullptr) {
        return count;
    }
    const int32_t written = std::min(count, capacity);
    for (int32_t index = 0; index < written; ++index) {
        std::memset(&levels[index], 0, sizeof(BallsLevelInfo));
        copyName(found[static_cast<size_t>(index)].name, levels[index].name, sizeof(levels[index].name));
        copyPath(found[static_cast<size_t>(index)].bricksPath, levels[index].bricksPath,
                 sizeof(levels[index].bricksPath));
        copyPath(found[static_cast<size_t>(index)].launchersPath, levels[index].launchersPath,
                 sizeof(levels[index].launchersPath));
    }
    return count;
}

int32_t balls_levels_import(int32_t mode, const char* destination, const char* const* picked, int32_t pickedCount,
                            BallsImportResult* results, int32_t capacity, char* summary, int32_t summarySize) {
    if (destination == nullptr || picked == nullptr || pickedCount <= 0) {
        setError("a destination folder and at least one picked file are required");
        return 0;
    }
    std::vector<std::filesystem::path> paths;
    paths.reserve(static_cast<size_t>(pickedCount));
    for (int32_t index = 0; index < pickedCount; ++index) {
        if (picked[index] != nullptr) {
            paths.emplace_back(picked[index]);
        }
    }
    const GameMode gameMode = mode == BALLS_MODE_PERFORMANCE ? GameMode::Performance : GameMode::Normal;
    const LevelImportReport report = importLevels(paths, std::filesystem::path(destination), gameMode);
    if (results != nullptr) {
        const int32_t written = std::min(static_cast<int32_t>(report.results.size()), std::max(capacity, 0));
        for (int32_t index = 0; index < written; ++index) {
            const LevelImportResult& source = report.results[static_cast<size_t>(index)];
            BallsImportResult& out = results[index];
            std::memset(&out, 0, sizeof out);
            out.status = source.status == LevelImportResult::Status::Imported ? 0
                         : source.status == LevelImportResult::Status::Replaced ? 1
                                                                                : 2;
            copyName(source.name, out.name, sizeof out.name);
            copyName(source.message, out.message, sizeof out.message);
            out.hasWarning = source.hasWarning ? 1 : 0;
        }
    }
    writeText(report.summary(), summary, summarySize);
    return static_cast<int32_t>(report.results.size());
}

int32_t balls_settings_load(const char* path, BallsSettings* settings) {
    if (path == nullptr || settings == nullptr) {
        return -1;
    }
    fromCoreSettings(loadSettings(std::filesystem::path(path)), *settings);
    return 0;
}

int32_t balls_settings_save(const char* path, const BallsSettings* settings) {
    if (path == nullptr || settings == nullptr) {
        return -1;
    }
    const Result<void> saved = saveSettings(std::filesystem::path(path), toCoreSettings(*settings));
    if (!saved) {
        setError(saved.error());
        return -1;
    }
    return 0;
}

BallsSession* balls_session_create(int32_t mode, const char* levelDirectory, const char* levelName,
                                   int32_t backend) {
    const auto level = loadLevelOrReport(levelDirectory, levelName);
    if (!level) {
        return nullptr;
    }
    auto session = newSession(mode, levelDirectory, levelName, *level);
    session->game = std::make_unique<GameSession>(session->mode, *level, toBackendKind(backend));
    return session.release();
}

BallsSession* balls_session_create_threaded(int32_t mode, const char* levelDirectory, const char* levelName,
                                            int32_t backend) {
    const auto level = loadLevelOrReport(levelDirectory, levelName);
    if (!level) {
        return nullptr;
    }
    auto session = newSession(mode, levelDirectory, levelName, *level);
    session->runner = std::make_unique<SessionRunner>(session->mode, *level, toBackendKind(backend));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!(session->view = session->runner->view()) && std::chrono::steady_clock::now() < deadline) {
        session->runner->tick(session->frameInput);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!session->view) {
        setError("the physics thread did not start");
        return nullptr;
    }
    return session.release();
}

int32_t balls_session_threaded(const BallsSession* session) {
    return session != nullptr && session->threaded() ? 1 : 0;
}

int32_t balls_session_take_collisions_change(BallsSession* session) {
    if (session == nullptr || session->collisionChanges.empty()) {
        return BALLS_COLLISIONS_UNCHANGED;
    }
    const int32_t change = session->collisionChanges.front();
    session->collisionChanges.erase(session->collisionChanges.begin());
    return change;
}

double balls_session_physics_frame_ms(const BallsSession* session) {
    return session != nullptr && session->view ? session->view->physicsFrameMs : 0.0;
}

double balls_session_physics_fps(const BallsSession* session) {
    return session != nullptr && session->view ? session->view->physicsTicksPerSecond : 0.0;
}

void balls_session_destroy(BallsSession* session) {
    delete session;
}

int32_t balls_session_load_level(BallsSession* session, const char* levelDirectory, const char* levelName) {
    if (session == nullptr) {
        return -1;
    }
    const auto level = loadLevelOrReport(levelDirectory, levelName);
    if (!level) {
        return -1;
    }
    session->levelDirectory = levelDirectory;
    session->levelName = levelName;
    session->playableHeight = level->bricks.playableHeight;
    session->apply([level = *level](GameSession& game) { game.reload(level); });
    return 0;
}

int32_t balls_session_restart(BallsSession* session) {
    if (session == nullptr) {
        return -1;
    }
    session->apply([](GameSession& game) { game.restart(); });
    return 0;
}

void balls_session_update(BallsSession* session, float deltaSeconds) {
    if (session == nullptr) {
        return;
    }
    if (!session->threaded()) {

        session->game->pollBackgroundWork();
        const PhysicsRate::Clock::time_point workStart = PhysicsRate::Clock::now();
        session->game->update(deltaSeconds);
        session->rate.frame(workStart, PhysicsRate::Clock::now(), session->game->simulation().stats().ticksLastFrame);
        return;
    }

    if (session->view && session->visibilityWidth > 0) {
        session->frameInput.visibilitySlotCells = session->view->balls.fieldWidth / static_cast<float>(session->visibilityWidth);
    }
    session->runner->tick(session->frameInput);
    if (auto latest = session->runner->view()) {
        session->view = std::move(latest);
    }
    for (const SessionEvent& event : session->runner->takeEvents()) {
        if (event.kind == SessionEventKind::BallCollisionsChanged) {
            session->collisionChanges.push_back(fromCollisionsChange(event.collisionsChange));
        } else {
            session->switchEvents.push_back(event);
        }
    }
}

void balls_session_report_frame(BallsSession* session, double, double workSeconds) {
    if (session == nullptr) {
        return;
    }
    if (session->threaded()) {
        session->frameInput.uiWorkSeconds = workSeconds;
    } else {
        session->game->reportFrameWorkSeconds(workSeconds);
    }
}

int32_t balls_session_mode(const BallsSession* session) {
    return session != nullptr && session->mode == GameMode::Performance ? BALLS_MODE_PERFORMANCE : BALLS_MODE_NORMAL;
}

int32_t balls_session_state(const BallsSession* session) {
    if (session == nullptr) {
        return BALLS_STATE_READY;
    }
    return fromSessionState(session->threaded() ? session->view->state : session->game->state());
}

int32_t balls_session_backend(const BallsSession* session) {
    if (session == nullptr) {
        return BALLS_BACKEND_SCALAR;
    }
    return session->threaded() ? fromBackendKind(session->view->backend) : fromBackendKind(session->game->backend());
}

double balls_session_elapsed_seconds(const BallsSession* session) {
    if (session == nullptr) {
        return 0.0;
    }
    return session->threaded() ? session->view->elapsedSeconds : session->game->elapsedSeconds();
}

int32_t balls_session_choose_launcher(BallsSession* session, int32_t column, int32_t row) {
    if (session == nullptr) {
        return 0;
    }
    if (!session->threaded()) {
        return session->game->chooseLauncher(column, row) ? 1 : 0;
    }

    if (session->view->mode != GameMode::Normal || session->view->state != SessionState::Running ||
        session->view->launchers.visible(column, row) == nullptr) {
        return 0;
    }
    session->apply([column, row](GameSession& game) { game.chooseLauncher(column, row); });
    return 1;
}

void balls_session_start_auto_release(BallsSession* session) {
    if (session != nullptr) {
        session->apply([](GameSession& game) { game.startAutoRelease(); });
    }
}

void balls_session_select_spawn_rate(BallsSession* session, int32_t optionIndex) {
    if (session != nullptr) {
        session->apply([optionIndex](GameSession& game) { game.selectSpawnRate(optionIndex); });
    }
}

void balls_session_select_ball_limit(BallsSession* session, int32_t optionIndex) {
    if (session != nullptr) {
        session->apply([optionIndex](GameSession& game) { game.selectBallLimit(optionIndex); });
    }
}

int32_t balls_session_spawn_rate_index(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    return session->threaded() ? session->view->spawnRateIndex : session->game->spawnRateIndex();
}

int32_t balls_session_ball_limit_index(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    return session->threaded() ? session->view->ballLimitIndex : session->game->ballLimitIndex();
}

uint64_t balls_session_ball_limit(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    return session->threaded() ? session->view->ballLimit : session->game->ballLimit();
}

int32_t balls_session_auto_releasing(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    return session->threaded() ? (session->view->autoReleasing ? 1 : 0) : (session->game->isAutoReleasing() ? 1 : 0);
}

int32_t balls_session_switch_backend(BallsSession* session, int32_t kind) {
    if (session == nullptr) {
        return 0;
    }

    session->apply([backend = toBackendKind(kind)](GameSession& game) { game.requestBackend(backend); });
    return 1;
}

int32_t balls_session_pending_backend(const BallsSession* session) {
    if (session == nullptr) {
        return -1;
    }
    const std::optional<BackendKind> pending = session->threaded() ? session->view->pendingBackend : session->game->pendingBackend();
    return pending ? fromBackendKind(*pending) : -1;
}

int32_t balls_session_take_switch_outcome(BallsSession* session, int32_t* kind) {
    if (session == nullptr) {
        return 0;
    }
    std::optional<BackendSwitchOutcome> outcome;
    if (!session->threaded()) {
        outcome = session->game->takeBackendSwitchOutcome();
    } else if (!session->switchEvents.empty()) {
        const SessionEvent event = session->switchEvents.front();
        session->switchEvents.erase(session->switchEvents.begin());
        outcome = BackendSwitchOutcome{event.backend, event.kind == SessionEventKind::BackendSwitched, event.forBallCollisions};
    }
    if (!outcome) {
        return 0;
    }
    if (kind != nullptr) {
        *kind = fromBackendKind(outcome->kind);
    }
    return outcome->succeeded ? 1 : 2;
}

void balls_session_start_capacity_probe(BallsSession* session, double targetFps) {
    (void)targetFps;
    if (session != nullptr) {
        session->apply([](GameSession& game) { game.fillToBallLimit(); });
    }
}

void balls_session_cancel_capacity_probe(BallsSession* session) {
    if (session != nullptr) {
        session->apply([](GameSession& game) { game.cancelFill(); });
    }
}

void balls_session_probe(const BallsSession* session, BallsProbeInfo* probe) {
    (void)session;
    if (probe == nullptr) {
        return;
    }
    probe->active = 0;
    probe->phase = BALLS_PROBE_IDLE;
    probe->lastFps = 0.0;
    probe->targetFps = 0.0;
    probe->bestPassingCount = 0;
}

int32_t balls_session_has_machine_ball_limit(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    return session->threaded() ? (session->view->machineLimit.has_value() ? 1 : 0) : (session->game->machineBallLimit().has_value() ? 1 : 0);
}

uint64_t balls_session_machine_ball_limit(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    return session->threaded() ? session->view->machineLimit.value_or(0) : session->game->machineBallLimit().value_or(0);
}

void balls_session_use_machine_ball_limit(BallsSession* session, uint64_t limit) {
    if (session != nullptr) {
        session->apply([limit](GameSession& game) { game.useMachineBallLimit(limit); });
    }
}

void balls_session_fill_with_balls(BallsSession* session, uint64_t count) {
    if (session != nullptr) {
        session->apply([count](GameSession& game) { game.fillWithBalls(count); });
    }
}

void balls_session_set_visibility_grid(BallsSession* session, int32_t width, int32_t height) {
    if (session == nullptr) {
        return;
    }
    if (session->threaded()) {

        session->visibilityWidth = width;
        return;
    }
    session->game->simulation().backend().setVisibilityGrid(width, height);
}

void balls_set_compute_gpu_preference(const char* preference) {
    setComputeGpuPreference(preference != nullptr ? std::string_view(preference) : std::string_view{});
}

int32_t balls_compute_gpu_preference(char* buffer, int32_t size) {
    return writeText(computeGpuPreference(), buffer, size);
}

int32_t balls_session_set_ball_collisions(BallsSession* session, int32_t enabled) {
    if (session == nullptr) {
        return BALLS_COLLISIONS_UNCHANGED;
    }
    if (session->threaded()) {
        session->runner->setBallCollisions(enabled != 0);
        return BALLS_COLLISIONS_UNCHANGED;
    }
    return fromCollisionsChange(session->game->setBallCollisions(enabled != 0));
}

int32_t balls_ball_collisions_message(int32_t change, char* buffer, int32_t size) {
    const BallCollisionsChange kind = change == BALLS_COLLISIONS_SWITCHING_TO_SCALAR ? BallCollisionsChange::SwitchingToScalar
                                      : change == BALLS_COLLISIONS_SCALAR_NOT_OFFERED ? BallCollisionsChange::ScalarNotOffered
                                                                                      : BallCollisionsChange::None;
    return writeText(ballCollisionsMessage(kind), buffer, size);
}

int32_t balls_session_ball_collisions(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    return session->threaded() ? (session->view->collisionsRequested ? 1 : 0) : (session->game->simulation().ballCollisionsRequested() ? 1 : 0);
}

int32_t balls_session_launch_balls(BallsSession* session, int32_t color, uint64_t count) {

    if (session == nullptr || session->threaded() || color < 0 || color >= BALLS_COLOR_COUNT) {
        return 0;
    }
    session->game->simulation().launchBalls(ballColorFromIndex(color), count);
    return 1;
}

int32_t balls_session_spawn_ball(BallsSession* session, float x, float y, float vx, float vy, int32_t color) {

    if (session == nullptr || session->threaded() || color < 0 || color >= BALLS_COLOR_COUNT) {
        return 0;
    }
    session->game->simulation().spawnBalls({BallSpawn{x, y, vx, vy, color}});
    return 1;
}

void balls_session_tick(BallsSession* session, int32_t ticks) {
    if (session == nullptr || session->threaded() || ticks <= 0) {
        return;
    }
    session->game->simulation().tick(ticks);
}

int32_t balls_session_last_backend_fault(const BallsSession* session, char* buffer, int32_t size) {
    if (session == nullptr) {
        return 0;
    }
    return writeText(session->threaded() ? session->view->backendFault : session->game->lastBackendFault(), buffer, size);
}

void balls_session_board_info(const BallsSession* session, BallsBoardInfo* info) {
    if (session == nullptr || info == nullptr || session->board() == nullptr) {
        return;
    }
    const BrickBoard& board = *session->board();
    const LauncherQueue& launchers = *session->launchers();
    info->columns = board.columns();
    info->rows = board.rows();
    info->playableHeight = session->playableHeight;
    info->paddedWidth = board.paddedWidth();
    info->paddedCellCount = board.paddedCellCount();
    info->brickCount = static_cast<int32_t>(board.bricks().size());
    info->aliveBrickCount = board.aliveBrickCount();
    info->revision = board.revision();
    info->launcherColumns = launchers.columnCount();
    info->launcherVisibleRows = launchers.visibleRows();
    const ColorTally remaining = launchers.remainingBalls();
    uint64_t total = 0;
    for (const BallCount count : remaining) {
        total += count;
    }
    info->launcherRemainingBalls = static_cast<int32_t>(std::min<uint64_t>(total, 0x7FFFFFFF));
}

void balls_session_board_view(BallsSession* session, BallsBoardView* view) {
    if (session == nullptr || view == nullptr || session->board() == nullptr) {
        return;
    }
    session->refreshBoardScratch();
    const BrickBoard& board = *session->board();
    view->paddedCells = board.paddedCells().data();
    view->brickColors = board.brickColors().data();
    view->brickHitsLeft = session->brickHitsLeft.data();
    view->brickRiseOffset = session->brickRiseOffset.data();
    view->brickHitFlash = session->brickHitFlash.data();
    view->aliveBricks = board.aliveBrickIndices().data();
    view->changedCells = board.changedCells().data();
    view->changedCellCount = static_cast<int32_t>(board.changedCells().size());
    view->brickCount = static_cast<int32_t>(board.bricks().size());
    view->aliveBrickCount = board.aliveBrickCount();
}

void balls_session_clear_changed_cells(BallsSession* session) {

    if (session != nullptr && !session->threaded()) {
        session->game->simulation().board().clearChangedCells();
    }
}

void balls_session_stats(const BallsSession* session, BallsStats* stats) {
    if (session == nullptr || stats == nullptr) {
        return;
    }
    const SimulationStats& source = session->threaded() ? session->view->stats : session->game->simulation().stats();
    stats->ballCount = session->threaded() ? session->view->stats.ballCount : session->game->simulation().backend().ballCount();
    stats->liveBallCount = session->threaded() ? session->view->liveBalls : session->game->simulation().liveBallCount();
    for (int color = 0; color < BALLS_COLOR_COUNT; ++color) {
        stats->ballsByColor[color] = source.ballsByColor[static_cast<size_t>(color)];
    }
    stats->totalBallsSpawned = source.totalBallsSpawned;
    stats->totalHits = source.totalHits;
    stats->lastFramePhysicsMs = source.lastFramePhysicsMs;
    stats->ticksLastFrame = source.ticksLastFrame;
}

int32_t balls_session_snapshot(BallsSession* session, BallsSnapshot* snapshot) {
    if (session == nullptr || snapshot == nullptr) {
        return 0;
    }
    if (session->threaded()) {

        const BallFrame& balls = session->view->balls;
        snapshot->packed = balls.packed.empty() ? nullptr : balls.packed.data();
        snapshot->count = balls.packed.size();
        snapshot->fieldWidth = balls.fieldWidth;
        snapshot->fieldHeight = balls.fieldHeight;
        snapshot->residentInRenderBuffer = 0;
        snapshot->visibilityGrid = balls.visibilityGrid ? 1 : 0;
        return snapshot->packed != nullptr ? 1 : 0;
    }
    const BallSnapshot source = session->game->simulation().backend().snapshot();
    snapshot->packed = source.packed;
    snapshot->count = source.count;
    snapshot->fieldWidth = source.fieldWidth;
    snapshot->fieldHeight = source.fieldHeight;
    snapshot->residentInRenderBuffer = source.residentInRenderBuffer ? 1 : 0;
    snapshot->visibilityGrid = source.visibilityGrid ? 1 : 0;
    return source.packed != nullptr ? 1 : 0;
}

int32_t balls_session_launcher(const BallsSession* session, int32_t column, int32_t row, BallsLauncher* launcher) {
    if (session == nullptr || launcher == nullptr) {
        return 0;
    }
    const QueuedLauncher* visible = session->launchers() ? session->launchers()->visible(column, row) : nullptr;
    if (visible == nullptr) {
        launcher->present = 0;
        launcher->color = 0;
        launcher->ballCount = 0;
        launcher->riseOffset = 0.0f;
        return 0;
    }
    launcher->present = 1;
    launcher->color = toIndex(visible->launcher.color);
    launcher->ballCount = visible->launcher.ballCount;
    launcher->riseOffset = visible->riseOffset;
    return 1;
}

uint64_t balls_session_remaining_balls_by_color(const BallsSession* session, int32_t color) {
    if (session == nullptr || color < 0 || color >= BALLS_COLOR_COUNT) {
        return 0;
    }
    return session->launchers() ? session->launchers()->remainingBalls()[static_cast<size_t>(color)] : 0;
}

BallsBenchmark* balls_benchmark_create(const char* levelDirectory, const char* levelName) {
    const auto level = loadLevelOrReport(levelDirectory, levelName);
    if (!level) {
        return nullptr;
    }
    auto benchmark = std::make_unique<BallsBenchmark>();
    BenchmarkWorkload workload;
    workload.level = level->bricks;
    benchmark->run = std::make_unique<BackendBenchmark>(workload, benchmarkCandidates());
    return benchmark.release();
}

void balls_benchmark_destroy(BallsBenchmark* benchmark) {
    delete benchmark;
}

void balls_benchmark_start(BallsBenchmark* benchmark) {
    if (benchmark != nullptr && benchmark->run != nullptr) {
        benchmark->run->start();
    }
}

void balls_benchmark_cancel(BallsBenchmark* benchmark) {
    if (benchmark != nullptr && benchmark->run != nullptr) {
        benchmark->run->cancel();
    }
}

int32_t balls_benchmark_finished(const BallsBenchmark* benchmark) {
    return benchmark != nullptr && benchmark->run != nullptr && benchmark->run->finished() ? 1 : 0;
}

int32_t balls_benchmark_rows(BallsBenchmark* benchmark, BallsBenchmarkRow* rows, int32_t capacity) {
    if (benchmark == nullptr || benchmark->run == nullptr) {
        return 0;
    }
    benchmark->refresh();
    const int32_t count = static_cast<int32_t>(benchmark->results.size());
    if (rows == nullptr) {
        return count;
    }
    const int32_t written = std::min(count, capacity);
    for (int32_t index = 0; index < written; ++index) {
        const CandidateResult& result = benchmark->results[static_cast<size_t>(index)];
        std::memset(&rows[index], 0, sizeof(BallsBenchmarkRow));
        copyName(result.candidate.label, rows[index].label, sizeof(rows[index].label));
        rows[index].kind = fromBackendKind(result.candidate.kind);
        switch (result.status) {
        case CandidateStatus::Running:
            rows[index].status = 1;
            break;
        case CandidateStatus::Done:
            rows[index].status = 2;
            break;
        case CandidateStatus::Failed:
            rows[index].status = 3;
            break;
        case CandidateStatus::Unavailable:
            rows[index].status = 4;
            break;
        case CandidateStatus::Pending:
        default:
            rows[index].status = 0;
            break;
        }
        rows[index].ballStepsPerSecond = result.ballStepsPerSecond;
        rows[index].progress = result.progress;
        copyName(result.failure, rows[index].failure, sizeof(rows[index].failure));
        rows[index].estimatedBalls = result.estimatedBalls;
        copyName(candidateResultText(result), rows[index].result, sizeof(rows[index].result));
    }
    return count;
}

int32_t balls_benchmark_winner(BallsBenchmark* benchmark) {
    if (benchmark == nullptr || benchmark->run == nullptr) {
        return -1;
    }
    benchmark->refresh();
    const BenchmarkCandidate winner = chooseDefaultBackend(
        benchmark->results, [](BackendKind kind) { return isBackendAvailable(kind); });
    return fromBackendKind(winner.kind);
}

BallsControl* balls_control_create(const char* normalLevels, const char* performanceLevels) {
    auto control = std::make_unique<BallsControl>();
    control->normalLevels = std::filesystem::path(normalLevels != nullptr ? normalLevels : "");
    control->performanceLevels = std::filesystem::path(performanceLevels != nullptr ? performanceLevels : "");
    return control.release();
}

void balls_control_destroy(BallsControl* control) {
    delete control;
}

int32_t balls_control_handle(BallsControl* control, const BallsControlContext* context, const char* target, char* json,
                             int32_t size, int32_t* jsonLength) {
    if (control == nullptr || context == nullptr || target == nullptr) {
        return 400;
    }
    control->context = context;
    control->currentSettings = toCoreSettings(context->settings);
    const ControlRequest request = parseControlTarget(target);
    const bool known = std::ranges::find(controlApiPaths(), std::string_view(request.path)) != controlApiPaths().end();
    std::optional<ControlResponse> response;
    if (known) {
        response = handleControlRequest(*control, request);
    }
    control->context = nullptr;
    if (!known || !response) {

        writeText(std::string_view{}, json, size);
        if (jsonLength != nullptr) {
            *jsonLength = 0;
        }
        return known ? 0 : -1;
    }
    if (jsonLength != nullptr) {
        *jsonLength = static_cast<int32_t>(response->json.size());
    }
    writeText(response->json, json, size);
    return response->status;
}

int32_t balls_control_take_action(BallsControl* control, BallsControlAction* action) {
    if (control == nullptr || action == nullptr || control->actions.empty()) {
        return 0;
    }
    *action = control->actions.front();
    control->actions.erase(control->actions.begin());
    return action->kind;
}

BallsLevelWatcher* balls_level_watcher_create(const char* levelDirectory, const char* levelName) {
    if (levelDirectory == nullptr || levelName == nullptr) {
        setError("level directory and name are required");
        return nullptr;
    }
    auto watcher = std::make_unique<BallsLevelWatcher>();
    watcher->watcher.watch(levelFilesFor(std::filesystem::path(levelDirectory), levelName));
    return watcher.release();
}

void balls_level_watcher_destroy(BallsLevelWatcher* watcher) {
    delete watcher;
}

int32_t balls_level_watcher_poll(BallsLevelWatcher* watcher) {
    if (watcher == nullptr) {
        return BALLS_DISK_UNCHANGED;
    }
    watcher->watcher.pollChanged(std::chrono::steady_clock::now());
    return fromDiskState(watcher->watcher.diskState());
}

void balls_level_watcher_mark_loaded(BallsLevelWatcher* watcher) {
    if (watcher != nullptr) {
        watcher->watcher.markLoaded();
    }
}

int32_t balls_level_disk_notice(int32_t state, char* buffer, int32_t size) {
    const LevelDiskState kind = state == BALLS_DISK_CHANGED   ? LevelDiskState::Changed
                                : state == BALLS_DISK_REMOVED ? LevelDiskState::Removed
                                                              : LevelDiskState::Unchanged;
    return writeText(levelDiskNotice(kind), buffer, size);
}

BallsFrameTimings* balls_frame_timings_create(void) {
    return new BallsFrameTimings();
}

void balls_frame_timings_destroy(BallsFrameTimings* timings) {
    delete timings;
}

void balls_frame_timings_record(BallsFrameTimings* timings, double frameMs, double workMs, double gpuMs) {
    if (timings != nullptr) {
        timings->timings.record({.frameMs = frameMs, .workMs = workMs, .gpuMs = gpuMs});
    }
}

void balls_frame_timings_stats(const BallsFrameTimings* timings, BallsFrameStats* stats) {
    if (timings == nullptr || stats == nullptr) {
        return;
    }
    const FrameTimings& frames = timings->timings;
    *stats = {frames.framesPerSecond(),        frames.worstFpsLastSecond(), frames.workFramesPerSecond(),
              frames.worstWorkFpsLastSecond(), frames.average().frameMs,    frames.average().workMs,
              frames.average().gpuMs};
}

BallsProfile* balls_profile_open(const char* directory) {
    if (directory == nullptr) {
        setError("profile directory is required");
        return nullptr;
    }
    auto profile = std::make_unique<BallsProfile>(BallsProfile{MachineProfile(std::filesystem::path(directory))});
    profile->profile.load();
    return profile.release();
}

void balls_profile_close(BallsProfile* profile) {
    delete profile;
}

int32_t balls_profile_ball_limit(const BallsProfile* profile, int32_t kind, double* targetFps, uint64_t* balls) {
    (void)profile;
    (void)kind;
    (void)targetFps;
    (void)balls;
    return 0;
}

void balls_profile_remember_ball_limit(BallsProfile* profile, int32_t kind, double targetFps, uint64_t balls) {
    (void)profile;
    (void)kind;
    (void)targetFps;
    (void)balls;
}

int32_t balls_profile_benchmark_score(const BallsProfile* profile, int32_t kind, double* ballStepsPerSecond) {
    if (profile == nullptr || !profile->profile.benchmark()) {
        return 0;
    }
    const BackendKind wanted = toBackendKind(kind);
    double best = 0.0;
    for (const CandidateResult& result : profile->profile.benchmark()->results) {
        if (result.candidate.kind == wanted && result.status == CandidateStatus::Done) {
            best = std::max(best, result.ballStepsPerSecond);
        }
    }
    if (best <= 0.0) {
        return 0;
    }
    if (ballStepsPerSecond != nullptr) {
        *ballStepsPerSecond = best;
    }
    return 1;
}

void balls_profile_remember_benchmark(BallsProfile* profile, BallsBenchmark* benchmark, int32_t complete,
                                      const char* gameVersion) {
    if (profile == nullptr || benchmark == nullptr || benchmark->run == nullptr) {
        return;
    }
    benchmark->refresh();
    BenchmarkRecord record;
    record.fingerprint = currentHardwareFingerprint(gameVersion != nullptr ? gameVersion : "");
    record.results = benchmark->results;
    record.winner = chooseDefaultBackend(record.results, [](BackendKind kind) { return isBackendAvailable(kind); });
    record.complete = complete != 0;
    profile->profile.rememberBenchmark(std::move(record));
}

BallsFlow* balls_flow_create(const char* normalLevels, const char* performanceLevels) {
    if (normalLevels == nullptr || performanceLevels == nullptr) {
        setError("both level folders are required");
        return nullptr;
    }
    return new BallsFlow{ScreenFlow(std::filesystem::path(normalLevels), std::filesystem::path(performanceLevels))};
}

void balls_flow_destroy(BallsFlow* flow) { delete flow; }

void balls_flow_update(BallsFlow* flow, float deltaSeconds) {
    if (flow != nullptr) {
        flow->flow.update(deltaSeconds);
    }
}

int32_t balls_flow_activate(BallsFlow* flow, int32_t action, int32_t row) {
    if (flow == nullptr || action < BALLS_FLOW_NORMAL_MODE || action > BALLS_FLOW_BACK) {
        return 0;
    }
    return flow->flow.activate(static_cast<FlowAction>(action), row) ? 1 : 0;
}

int32_t balls_flow_escape(BallsFlow* flow) { return flow != nullptr && flow->flow.escape() ? 1 : 0; }

void balls_flow_show_main_menu(BallsFlow* flow) {
    if (flow != nullptr) {
        flow->flow.showMainMenu();
    }
}

void balls_flow_show_level_select(BallsFlow* flow, int32_t mode) {
    if (flow != nullptr) {
        flow->flow.showLevelSelect(toGameMode(mode));
    }
}

int32_t balls_flow_start_game(BallsFlow* flow, int32_t mode, const char* levelName) {
    if (flow == nullptr || !hasText(levelName)) {
        return 0;
    }
    const GameMode gameMode = toGameMode(mode);
    const LevelFiles files = levelFilesFor(flow->flow.levelFolderFor(gameMode), levelName);
    if (!std::filesystem::exists(files.bricksPath) || !std::filesystem::exists(files.launchersPath)) {
        setError("no level named " + std::string(levelName));
        return 0;
    }
    flow->flow.startGame(gameMode, files);
    return 1;
}

void balls_flow_open_editor(BallsFlow* flow, int32_t mode, const char* levelName) {
    if (flow == nullptr) {
        return;
    }
    const GameMode gameMode = toGameMode(mode);
    std::optional<LevelFiles> files;
    if (hasText(levelName)) {
        files = levelFilesFor(flow->flow.levelFolderFor(gameMode), levelName);
    }
    flow->flow.openEditor(gameMode, std::move(files));
}

void balls_flow_show_settings(BallsFlow* flow) {
    if (flow != nullptr) {
        flow->flow.showSettings();
    }
}

void balls_flow_run_machine_benchmark(BallsFlow* flow) {
    if (flow != nullptr) {
        flow->flow.runMachineBenchmark();
    }
}

void balls_flow_level_loaded(BallsFlow* flow) {
    if (flow != nullptr) {
        flow->flow.levelLoaded();
    }
}

void balls_flow_level_failed(BallsFlow* flow, const char* error) {
    if (flow != nullptr) {
        flow->flow.levelFailed(error != nullptr ? error : "");
    }
}

void balls_flow_session_state(BallsFlow* flow, int32_t state) {
    if (flow == nullptr) {
        return;
    }
    flow->flow.sessionStateChanged(state == BALLS_STATE_WON       ? SessionState::Won
                                   : state == BALLS_STATE_RUNNING ? SessionState::Running
                                                                  : SessionState::Ready);
}

void balls_flow_benchmark_finished(BallsFlow* flow) {
    if (flow != nullptr) {
        flow->flow.benchmarkFinished();
    }
}

void balls_flow_play_from_editor(BallsFlow* flow, int32_t mode, const char* levelName) {
    if (flow == nullptr || !hasText(levelName)) {
        return;
    }
    const GameMode gameMode = toGameMode(mode);
    flow->flow.playFromEditor(gameMode, levelFilesFor(flow->flow.levelFolderFor(gameMode), levelName));
}

void balls_flow_import(BallsFlow* flow, const char* const* picked, int32_t pickedCount) {
    if (flow == nullptr || picked == nullptr || pickedCount <= 0) {
        return;
    }
    std::vector<std::filesystem::path> paths;
    for (int32_t index = 0; index < pickedCount; ++index) {
        if (picked[index] != nullptr) {
            paths.emplace_back(picked[index]);
        }
    }
    flow->flow.importLevels(paths);
}

void balls_flow_show_toast(BallsFlow* flow, const char* text, int32_t isError) {
    if (flow != nullptr && text != nullptr) {
        flow->flow.showToast(text, isError != 0);
    }
}

int32_t balls_flow_take_request(BallsFlow* flow, BallsFlowRequest* request) {
    if (flow == nullptr || request == nullptr) {
        return 0;
    }
    const std::optional<FlowRequest> next = flow->flow.takeRequest();
    if (!next) {
        return 0;
    }
    std::memset(request, 0, sizeof *request);
    request->kind = static_cast<int32_t>(next->kind);
    request->mode = fromGameMode(next->mode);
    if (next->files) {
        request->hasLevel = 1;
        fillLevelInfo(*next->files, request->level);
    }
    copyPath(next->folder, request->folder, sizeof request->folder);
    return 1;
}

int32_t balls_flow_take_scroll_to_row(BallsFlow* flow) {
    if (flow == nullptr) {
        return -1;
    }
    const std::optional<int> row = flow->flow.takeScrollToRow();
    return row ? *row : -1;
}

void balls_flow_state(const BallsFlow* flow, BallsFlowState* state) {
    if (flow == nullptr || state == nullptr) {
        return;
    }
    const ScreenFlow& source = flow->flow;
    std::memset(state, 0, sizeof *state);
    state->screen = static_cast<int32_t>(source.screen());
    state->overlay = static_cast<int32_t>(source.overlay());
    state->mode = fromGameMode(source.mode());
    state->paused = source.isPaused() ? 1 : 0;
    state->diskState = fromDiskState(source.diskState());
    state->canRunMachineBenchmark = source.canRunMachineBenchmark() ? 1 : 0;
    state->benchmarkSettled = source.benchmarkSettled() ? 1 : 0;
    state->buttonCount = static_cast<int32_t>(source.buttons().size());
    state->levelCount = static_cast<int32_t>(source.levelRows().size());
    state->hasLevel = source.level() ? 1 : 0;
    state->toastSecondsLeft = source.toast().secondsLeft;
    state->toastIsError = source.toast().isError ? 1 : 0;
}

int32_t balls_flow_text(const BallsFlow* flow, int32_t which, char* buffer, int32_t size) {
    if (flow == nullptr) {
        return writeText(std::string_view{}, buffer, size);
    }
    const ScreenFlow& source = flow->flow;
    switch (which) {
    case BALLS_FLOW_TEXT_TITLE:
        return writeText(source.title(), buffer, size);
    case BALLS_FLOW_TEXT_FOOTNOTE:
        return writeText(source.footnote(), buffer, size);
    case BALLS_FLOW_TEXT_GAME_TITLE:
        return writeText(source.gameTitle(), buffer, size);
    case BALLS_FLOW_TEXT_DISK_NOTICE:
        return writeText(source.diskNotice(), buffer, size);
    case BALLS_FLOW_TEXT_LOAD_ERROR:
        return writeText(source.loadError(), buffer, size);
    case BALLS_FLOW_TEXT_TOAST:
        return writeText(source.toast().text, buffer, size);
    case BALLS_FLOW_TEXT_SCREEN_NAME:
        return writeText(flowScreenName(source.screen()), buffer, size);
    case BALLS_FLOW_TEXT_LEVEL_FOLDER:
        return writeText(source.levelFolder().string(), buffer, size);
    default:
        return writeText(std::string_view{}, buffer, size);
    }
}

int32_t balls_flow_button(const BallsFlow* flow, int32_t index, BallsFlowButton* button) {
    if (flow == nullptr || button == nullptr) {
        return 0;
    }
    const std::vector<FlowButton> buttons = flow->flow.buttons();
    if (index < 0 || index >= static_cast<int32_t>(buttons.size())) {
        return 0;
    }
    const FlowButton& source = buttons[static_cast<size_t>(index)];
    std::memset(button, 0, sizeof *button);
    button->action = static_cast<int32_t>(source.action);
    button->enabled = source.enabled ? 1 : 0;
    copyName(source.label, button->label, sizeof button->label);
    return 1;
}

int32_t balls_flow_level_row(const BallsFlow* flow, int32_t index, BallsFlowLevelRow* row) {
    if (flow == nullptr || row == nullptr) {
        return 0;
    }
    const std::span<const FlowLevelRow> rows = flow->flow.levelRows();
    if (index < 0 || index >= static_cast<int32_t>(rows.size())) {
        return 0;
    }
    std::memset(row, 0, sizeof *row);
    copyName(rows[static_cast<size_t>(index)].name, row->name, sizeof row->name);
    row->highlighted = rows[static_cast<size_t>(index)].highlighted ? 1 : 0;
    return 1;
}

extern "C++" {
namespace {

std::shared_ptr<const SessionView> latestView(BallsSession* session) {
    return session != nullptr ? session->latestView() : nullptr;
}

void writeOption(const UiOption& source, int32_t kind, BallsUiOption& out) {
    std::memset(&out, 0, sizeof out);
    copyName(source.label, out.label, sizeof out.label);
    out.enabled = source.enabled ? 1 : 0;
    out.selected = source.selected ? 1 : 0;
    copyName(source.detail, out.detail, sizeof out.detail);
    out.kind = kind;
}

int32_t writeOptions(const std::vector<UiOption>& source, BallsUiOption* options, int32_t capacity) {
    if (options != nullptr) {
        const int32_t written = std::min(static_cast<int32_t>(source.size()), std::max(capacity, 0));
        for (int32_t index = 0; index < written; ++index) {
            writeOption(source[static_cast<size_t>(index)], -1, options[index]);
        }
    }
    return static_cast<int32_t>(source.size());
}

int32_t writeBackendRows(const std::vector<BackendRow>& rows, BallsUiOption* options, int32_t capacity) {
    if (options != nullptr) {
        const int32_t written = std::min(static_cast<int32_t>(rows.size()), std::max(capacity, 0));
        for (int32_t index = 0; index < written; ++index) {
            const BackendRow& row = rows[static_cast<size_t>(index)];
            writeOption(row.option, fromBackendKind(row.kind), options[index]);
        }
    }
    return static_cast<int32_t>(rows.size());
}

}
}

int32_t balls_menu_options(int32_t list, int32_t value, BallsSession* session, BallsUiOption* options,
                           int32_t capacity) {
    switch (list) {
    case BALLS_MENU_ORIENTATION:
        return writeOptions(orientationOptions(static_cast<LayoutOrientation>(value)), options, capacity);
    case BALLS_MENU_OFF_ON:
        return writeOptions(offOnOptions(value != 0), options, capacity);
    case BALLS_MENU_BALL_COLLISIONS: {
        const BallCollisionsChoice choice = ballCollisionsChoice(value != 0);
        std::vector<UiOption> withHint = choice.options;
        withHint.front().detail = choice.hint;
        return writeOptions(withHint, options, capacity);
    }
    default:
        break;
    }
    const std::shared_ptr<const SessionView> view = latestView(session);
    if (!view) {
        return 0;
    }
    switch (list) {
    case BALLS_MENU_SPAWN_RATE:
        return writeOptions(spawnRateChoices(view->spawnRateIndex), options, capacity);
    case BALLS_MENU_BALL_LIMIT:
        return writeOptions(ballLimitChoices(*view), options, capacity);
    case BALLS_MENU_GAMEPLAY_BACKENDS:
        return writeBackendRows(gameplayBackendRows(*view), options, capacity);
    case BALLS_MENU_RELEASE:
        return writeOptions({releaseButton(*view)}, options, capacity);
    default:
        return 0;
    }
}

int32_t balls_menu_default_backends(int32_t preferredKind, const BallsProfile* profile, BallsUiOption* options,
                                    int32_t capacity) {
    std::span<const CandidateResult> scores;
    if (profile != nullptr && profile->profile.benchmark()) {
        scores = profile->profile.benchmark()->results;
    }
    return writeBackendRows(defaultBackendRows(toBackendKind(preferredKind), scores), options, capacity);
}

int32_t balls_menu_auto_ball_limit(BallsSession* session) {
    const std::shared_ptr<const SessionView> view = latestView(session);
    return view && isAutoBallLimit(*view) ? 1 : 0;
}

int32_t balls_menu_probe_status(BallsSession* session, char* buffer, int32_t size) {
    (void)session;
    return writeText(std::string_view{}, buffer, size);
}

int32_t balls_menu_device_rows(const BallsPick* picks, int32_t pickCount, BallsDevicePickRow* rows, int32_t capacity) {
    const std::vector<DevicePickRow> source = devicePickRows(toPicks(picks, pickCount));
    if (rows != nullptr) {
        const int32_t written = std::min(static_cast<int32_t>(source.size()), std::max(capacity, 0));
        for (int32_t index = 0; index < written; ++index) {
            const DevicePickRow& row = source[static_cast<size_t>(index)];
            BallsDevicePickRow& out = rows[index];
            std::memset(&out, 0, sizeof out);
            copyName(row.device, out.device, sizeof out.device);
            copyName(row.label, out.label, sizeof out.label);
            const size_t optionCount = std::min(row.options.size(), std::size(out.options));
            out.optionCount = static_cast<int32_t>(optionCount);
            for (size_t option = 0; option < optionCount; ++option) {
                const std::optional<BackendKind>& kind = row.kinds[option];
                writeOption(row.options[option], kind ? fromBackendKind(*kind) : -1, out.options[option]);
            }
        }
    }
    return static_cast<int32_t>(source.size());
}

int32_t balls_menu_pick_device(const BallsPick* picks, int32_t pickCount, int32_t row, int32_t option,
                               BallsPick* result, int32_t capacity) {
    const std::vector<BackendPick> current = toPicks(picks, pickCount);
    const std::vector<DevicePickRow> rows = devicePickRows(current);
    if (row < 0 || option < 0) {
        return -1;
    }
    const std::optional<std::vector<BackendPick>> next =
        pickDeviceOption(current, rows, static_cast<size_t>(row), static_cast<size_t>(option));
    if (!next) {
        return -1;
    }
    return writePicks(*next, result, capacity);
}

int32_t balls_menu_debug_rows(BallsSession* session, const BallsFrameTimings* timings, int32_t showBackend,
                              const char* renderer, BallsDebugRow* rows, int32_t capacity) {
    const std::shared_ptr<const SessionView> view = latestView(session);
    if (!view || timings == nullptr) {
        return 0;
    }
    DebugGadgetOptions gadget;
    gadget.showBackend = showBackend != 0;
    gadget.renderer = renderer != nullptr ? renderer : "";
    const std::vector<DebugRow> source = debugGadgetRows(*view, timings->timings, gadget);
    if (rows != nullptr) {
        const int32_t written = std::min(static_cast<int32_t>(source.size()), std::max(capacity, 0));
        for (int32_t index = 0; index < written; ++index) {
            const DebugRow& row = source[static_cast<size_t>(index)];
            BallsDebugRow& out = rows[index];
            std::memset(&out, 0, sizeof out);
            out.kind = static_cast<int32_t>(row.kind);
            out.tone = static_cast<int32_t>(row.tone);
            out.colorIndex = row.colorIndex;
            copyName(row.label, out.label, sizeof out.label);
            copyName(row.value, out.value, sizeof out.value);
        }
    }
    return static_cast<int32_t>(source.size());
}

int32_t balls_menu_message(int32_t which, int32_t kind, uint64_t balls, double fps, const char* text, char* buffer,
                           int32_t size) {
    switch (which) {
    case BALLS_MENU_TEXT_BACKEND_FAILED:
        return writeText(backendFailedMessage(toBackendKind(kind)), buffer, size);
    case BALLS_MENU_TEXT_PROBE_RESULT:

        (void)kind;
        (void)balls;
        (void)fps;
        return writeText(std::string_view{}, buffer, size);
    case BALLS_MENU_TEXT_COMPUTE_GPU:
        return writeText(computeGpuMessage(text != nullptr ? text : ""), buffer, size);
    default:
        return writeText(std::string_view{}, buffer, size);
    }
}

int32_t balls_menu_physics_on(const BallsPick* picks, int32_t pickCount, char* buffer, int32_t size) {
    return writeText(physicsOnMessage(toPicks(picks, pickCount)), buffer, size);
}

BallsEditorSession* balls_editor_session_create(int32_t mode, const char* levelName, const char* normalLevels,
                                                const char* performanceLevels) {
    if (normalLevels == nullptr || performanceLevels == nullptr) {
        setError("both level folders are required");
        return nullptr;
    }
    const GameMode gameMode = toGameMode(mode);
    const std::filesystem::path folder(gameMode == GameMode::Normal ? normalLevels : performanceLevels);
    std::optional<LevelFiles> files;
    if (hasText(levelName)) {
        files = levelFilesFor(folder, levelName);
    }
    return new BallsEditorSession{EditorSession(gameMode, std::move(files), std::filesystem::path(normalLevels),
                                                std::filesystem::path(performanceLevels))};
}

void balls_editor_session_destroy(BallsEditorSession* session) { delete session; }

void balls_editor_session_update(BallsEditorSession* session, float deltaSeconds) {
    if (session != nullptr) {
        session->session.update(deltaSeconds);
    }
}

void balls_editor_session_state(const BallsEditorSession* session, BallsEditorState* state) {
    if (session == nullptr || state == nullptr) {
        return;
    }
    const EditorSession& source = session->session;
    const LevelData& level = source.level();
    std::memset(state, 0, sizeof *state);
    state->width = level.bricks.width;
    state->height = level.bricks.height;
    state->playableHeight = level.bricks.playableHeight;
    state->launcherColumns = level.launchers.columns;
    state->launcherVisibleRows = level.launchers.visibleRows;
    state->launcherRows = level.launchers.rowCount();
    state->dirty = source.isDirty() ? 1 : 0;
    state->canUndo = source.canUndo() ? 1 : 0;
    state->canRedo = source.canRedo() ? 1 : 0;
    state->tool = static_cast<int32_t>(source.tool());
    state->brushColor = toIndex(source.brushColor());
    state->brushHits = source.brushHits();
    state->launcherBalls = source.launcherBalls();
    state->selectedBrick = source.selectedBrick();
    state->mode = fromGameMode(source.mode());
    state->playable = source.validation().isPlayable() ? 1 : 0;
    state->problemCount = static_cast<int32_t>(source.validation().problems.size());
    state->messageSecondsLeft = source.message().secondsLeft;
    state->messageIsError = source.message().isError ? 1 : 0;
}

int32_t balls_editor_session_text(const BallsEditorSession* session, int32_t which, char* buffer, int32_t size) {
    if (session == nullptr) {
        return writeText(std::string_view{}, buffer, size);
    }
    const EditorSession& source = session->session;
    switch (which) {
    case BALLS_EDITOR_TEXT_NAME:
        return writeText(source.name(), buffer, size);
    case BALLS_EDITOR_TEXT_SELECTION:
        return writeText(source.selectionText(), buffer, size);
    case BALLS_EDITOR_TEXT_SAVE_LABEL:
        return writeText(source.saveLabel(), buffer, size);
    case BALLS_EDITOR_TEXT_BACK_LABEL:
        return writeText(source.backLabel(), buffer, size);
    case BALLS_EDITOR_TEXT_FOOTER:
        return writeText(source.footerText(), buffer, size);
    case BALLS_EDITOR_TEXT_MESSAGE:
        return writeText(source.message().text, buffer, size);
    default:
        return writeText(std::string_view{}, buffer, size);
    }
}

int32_t balls_editor_session_problem(const BallsEditorSession* session, int32_t index, char* buffer, int32_t size) {
    if (session == nullptr || index < 0 ||
        index >= static_cast<int32_t>(session->session.validation().problems.size())) {
        return writeText(std::string_view{}, buffer, size);
    }
    return writeText(session->session.validation().problems[static_cast<size_t>(index)], buffer, size);
}

int32_t balls_editor_session_tally(const BallsEditorSession* session, int32_t color, char* buffer, int32_t size) {
    if (session == nullptr || color < 0 || color >= ballColorCount) {
        writeText(std::string_view{}, buffer, size);
        return 0;
    }
    const BallColor ballColor = ballColorFromIndex(color);
    writeText(session->session.colorTallyText(ballColor), buffer, size);
    return session->session.colorTallyMatches(ballColor) ? 1 : 0;
}

int32_t balls_editor_session_tool_label(int32_t tool, char* buffer, int32_t size) {
    for (const EditorToolButton& button : EditorSession::toolButtons()) {
        if (static_cast<int32_t>(button.tool) == tool) {
            return writeText(button.label, buffer, size);
        }
    }
    return writeText(std::string_view{}, buffer, size);
}

int32_t balls_editor_session_brick(const BallsEditorSession* session, int32_t column, int32_t row, int32_t* color,
                                   int32_t* hits) {
    int32_t id = emptyBrickId;
    int32_t brickColor = -1;
    int32_t brickHits = 0;
    if (session != nullptr) {
        const BrickLayout& bricks = session->session.level().bricks;
        if (column >= 0 && row >= 0 && column < bricks.width && row < bricks.height) {
            id = bricks.cellId(column, row);
            if (const BrickType* type = bricks.findType(id)) {
                brickColor = toIndex(type->color);
                brickHits = type->hits;
            }
        }
    }
    if (color != nullptr) {
        *color = brickColor;
    }
    if (hits != nullptr) {
        *hits = brickHits;
    }
    return id;
}

int32_t balls_editor_session_launcher(const BallsEditorSession* session, int32_t column, int32_t row, int32_t* color,
                                      int32_t* balls) {
    if (session == nullptr) {
        return 0;
    }
    const LauncherLayout& launchers = session->session.level().launchers;
    if (column < 0 || row < 0 || column >= launchers.columns || row >= launchers.rowCount()) {
        return 0;
    }
    const std::optional<Launcher>& slot = launchers.slot(column, row);
    if (!slot) {
        return 0;
    }
    if (color != nullptr) {
        *color = toIndex(slot->color);
    }
    if (balls != nullptr) {
        *balls = slot->ballCount;
    }
    return 1;
}

void balls_editor_session_set_tool(BallsEditorSession* session, int32_t tool) {
    if (session != nullptr && tool >= BALLS_TOOL_PAINT && tool <= BALLS_TOOL_PICK) {
        session->session.setTool(static_cast<EditorTool>(tool));
    }
}

void balls_editor_session_select_color(BallsEditorSession* session, int32_t color) {
    if (session != nullptr && color >= 0 && color < ballColorCount) {
        session->session.selectColor(ballColorFromIndex(color));
    }
}

void balls_editor_session_set_brush_hits(BallsEditorSession* session, int32_t hits) {
    if (session != nullptr) {
        session->session.setBrushHits(hits);
    }
}

void balls_editor_session_set_launcher_balls(BallsEditorSession* session, int32_t balls) {
    if (session != nullptr) {
        session->session.setLauncherBalls(balls);
    }
}

void balls_editor_session_begin_stroke(BallsEditorSession* session, int32_t erase) {
    if (session != nullptr) {
        session->session.beginStroke(erase != 0);
    }
}

void balls_editor_session_stroke_cell(BallsEditorSession* session, int32_t column, int32_t row) {
    if (session != nullptr) {
        session->session.strokeCell(column, row);
    }
}

void balls_editor_session_end_stroke(BallsEditorSession* session) {
    if (session != nullptr) {
        session->session.endStroke();
    }
}

void balls_editor_session_click_launcher(BallsEditorSession* session, int32_t column, int32_t row, int32_t click) {
    if (session != nullptr && click >= BALLS_LAUNCHER_SET && click <= BALLS_LAUNCHER_DELETE_ROW) {
        session->session.clickLauncher(column, row, static_cast<LauncherClick>(click));
    }
}

void balls_editor_session_set_name(BallsEditorSession* session, const char* name) {
    if (session != nullptr) {
        session->session.setName(name != nullptr ? name : "");
    }
}

void balls_editor_session_set_mode(BallsEditorSession* session, int32_t mode) {
    if (session != nullptr) {
        session->session.setMode(toGameMode(mode));
    }
}

void balls_editor_session_set_board_size(BallsEditorSession* session, int32_t width, int32_t brickRows,
                                         int32_t totalRows) {
    if (session != nullptr) {
        session->session.setBoardSize(width, brickRows, totalRows);
    }
}

void balls_editor_session_set_launcher_columns(BallsEditorSession* session, int32_t columns) {
    if (session != nullptr) {
        session->session.setLauncherColumns(columns);
    }
}

void balls_editor_session_set_visible_rows(BallsEditorSession* session, int32_t rows) {
    if (session != nullptr) {
        session->session.setVisibleRows(rows);
    }
}

void balls_editor_session_auto_fill(BallsEditorSession* session) {
    if (session != nullptr) {
        session->session.autoFillLaunchers();
    }
}

void balls_editor_session_undo(BallsEditorSession* session) {
    if (session != nullptr) {
        session->session.undo();
    }
}

void balls_editor_session_redo(BallsEditorSession* session) {
    if (session != nullptr) {
        session->session.redo();
    }
}

int32_t balls_editor_session_save(BallsEditorSession* session) {
    return session != nullptr && session->session.save() ? 1 : 0;
}

int32_t balls_editor_session_save_and_play(BallsEditorSession* session, BallsLevelInfo* level) {
    if (session == nullptr) {
        return 0;
    }
    const std::optional<LevelFiles> files = session->session.saveAndPlay();
    if (!files) {
        return 0;
    }
    if (level != nullptr) {
        std::memset(level, 0, sizeof *level);
        fillLevelInfo(*files, *level);
    }
    return 1;
}

int32_t balls_editor_session_back(BallsEditorSession* session) {
    return session == nullptr || session->session.back() == EditorLeave::Leave ? 1 : 0;
}

int32_t balls_editor_session_shortcut(BallsEditorSession* session, int32_t shortcut) {
    if (session == nullptr || shortcut < BALLS_SHORTCUT_UNDO || shortcut > BALLS_SHORTCUT_COLOR_1 + ballColorCount - 1) {
        return 0;
    }
    return session->session.handleShortcut(static_cast<EditorShortcut>(shortcut)) ? 1 : 0;
}

}
