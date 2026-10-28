#include "BallsUnityApi.h"

#include "common/BallColor.h"
#include "editor/LevelEditor.h"
#include "game/FrameTimings.h"
#include "game/GameSession.h"
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
#include "sim/DeviceInventory.h"
#include "sim/Simulation.h"
#include "ui/GameplayLayout.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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

} // namespace

struct BallsSession {
    std::unique_ptr<GameSession> game;
    std::string levelDirectory;
    std::string levelName;

    // Board view scratch: BrickBoard keeps these per-brick values inside a vector<Brick>, so the flat arrays a host
    // reads are refreshed on demand and shared until the next update.
    std::vector<int32_t> brickHitsLeft;
    std::vector<float> brickRiseOffset;
    std::vector<float> brickHitFlash;

    void refreshBoardScratch() {
        const BrickBoard& board = game->simulation().board();
        const std::vector<Brick>& bricks = board.bricks();
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

struct BallsFrameTimings {
    FrameTimings timings;
};

struct BallsProfile {
    MachineProfile profile;
};

struct BallsEditor {
    LevelEditor editor;
    std::string levelDirectory;
    std::string levelName = "Untitled";
    mutable EditorValidation validation;
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
    // physicalDevices() opens the GPU drivers on its first call, so it is asked once per process and cached there.
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

}  // namespace

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

int32_t balls_compute_gpu(char* buffer, int32_t size) {
    return writeText(computeGpuPreference(), buffer, size);
}

int32_t balls_set_compute_gpu(const char* preference) {
    if (preference == nullptr) {
        return 0;
    }
    // The core takes "auto", "integrated"/"discrete", an adapter index or a name, and ignores what it cannot match;
    // answer the host for it, so a typo is told rather than silently leaving the old adapter in place.
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
    const Settings loaded = loadSettings(std::filesystem::path(path));
    settings->ballCollisions = loaded.ballCollisions ? 1 : 0;
    settings->vsync = loaded.vsync ? 1 : 0;
    settings->advancedSettings = loaded.advancedSettings ? 1 : 0;
    settings->orientation = loaded.layout == LayoutOrientation::Landscape  ? BALLS_ORIENTATION_LANDSCAPE
                            : loaded.layout == LayoutOrientation::Portrait ? BALLS_ORIENTATION_PORTRAIT
                                                                           : BALLS_ORIENTATION_AUTO;
    copyName(loaded.defaultBackend, settings->defaultBackend, sizeof(settings->defaultBackend));
    return 0;
}

int32_t balls_settings_save(const char* path, const BallsSettings* settings) {
    if (path == nullptr || settings == nullptr) {
        return -1;
    }
    Settings outgoing;
    outgoing.ballCollisions = settings->ballCollisions != 0;
    outgoing.vsync = settings->vsync != 0;
    outgoing.advancedSettings = settings->advancedSettings != 0;
    outgoing.layout = settings->orientation == BALLS_ORIENTATION_LANDSCAPE  ? LayoutOrientation::Landscape
                      : settings->orientation == BALLS_ORIENTATION_PORTRAIT ? LayoutOrientation::Portrait
                                                                           : LayoutOrientation::Auto;
    outgoing.defaultBackend = settings->defaultBackend;
    const Result<void> saved = saveSettings(std::filesystem::path(path), outgoing);
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
    auto session = std::make_unique<BallsSession>();
    session->levelDirectory = levelDirectory;
    session->levelName = levelName;
    session->game = std::make_unique<GameSession>(mode == BALLS_MODE_PERFORMANCE ? GameMode::Performance
                                                                                : GameMode::Normal,
                                                  *level, toBackendKind(backend));
    return session.release();
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
    session->game->reload(*level);
    return 0;
}

int32_t balls_session_restart(BallsSession* session) {
    if (session == nullptr) {
        return -1;
    }
    session->game->restart();
    return 0;
}

void balls_session_update(BallsSession* session, float deltaSeconds) {
    if (session == nullptr) {
        return;
    }
    // A backend built off the frame is adopted here, before the tick that would run on it (SessionRunner's order).
    session->game->pollBackgroundWork();
    session->game->update(deltaSeconds);
}

void balls_session_report_frame(BallsSession* session, double frameSeconds, double workSeconds) {
    if (session != nullptr) {
        session->game->reportFrameSeconds(frameSeconds, workSeconds);
    }
}

int32_t balls_session_mode(const BallsSession* session) {
    return session != nullptr && session->game->mode() == GameMode::Performance ? BALLS_MODE_PERFORMANCE
                                                                               : BALLS_MODE_NORMAL;
}

int32_t balls_session_state(const BallsSession* session) {
    if (session == nullptr) {
        return BALLS_STATE_READY;
    }
    switch (session->game->state()) {
    case SessionState::Running:
        return BALLS_STATE_RUNNING;
    case SessionState::Won:
        return BALLS_STATE_WON;
    case SessionState::Ready:
    default:
        return BALLS_STATE_READY;
    }
}

int32_t balls_session_backend(const BallsSession* session) {
    return session == nullptr ? BALLS_BACKEND_SCALAR : fromBackendKind(session->game->backend());
}

double balls_session_elapsed_seconds(const BallsSession* session) {
    return session == nullptr ? 0.0 : session->game->elapsedSeconds();
}

int32_t balls_session_choose_launcher(BallsSession* session, int32_t column, int32_t row) {
    return session != nullptr && session->game->chooseLauncher(column, row) ? 1 : 0;
}

void balls_session_start_auto_release(BallsSession* session) {
    if (session != nullptr) {
        session->game->startAutoRelease();
    }
}

void balls_session_select_spawn_rate(BallsSession* session, int32_t optionIndex) {
    if (session != nullptr) {
        session->game->selectSpawnRate(optionIndex);
    }
}

void balls_session_select_ball_multiplier(BallsSession* session, int32_t optionIndex) {
    if (session != nullptr) {
        session->game->selectBallMultiplier(optionIndex);
    }
}

void balls_session_select_ball_limit(BallsSession* session, int32_t optionIndex) {
    if (session != nullptr) {
        session->game->selectBallLimit(optionIndex);
    }
}

int32_t balls_session_spawn_rate_index(const BallsSession* session) {
    return session == nullptr ? 0 : session->game->spawnRateIndex();
}

int32_t balls_session_ball_multiplier_index(const BallsSession* session) {
    return session == nullptr ? 0 : session->game->ballMultiplierIndex();
}

int32_t balls_session_ball_limit_index(const BallsSession* session) {
    return session == nullptr ? 0 : session->game->ballLimitIndex();
}

uint64_t balls_session_ball_limit(const BallsSession* session) {
    return session == nullptr ? 0 : session->game->ballLimit();
}

int32_t balls_session_auto_releasing(const BallsSession* session) {
    return session != nullptr && session->game->isAutoReleasing() ? 1 : 0;
}

int32_t balls_session_switch_backend(BallsSession* session, int32_t kind) {
    if (session == nullptr) {
        return 0;
    }
    // The new backend is built off the frame; the session keeps running on the current one until it is ready.
    session->game->requestBackend(toBackendKind(kind));
    return 1;
}

int32_t balls_session_pending_backend(const BallsSession* session) {
    if (session == nullptr) {
        return -1;
    }
    const std::optional<BackendKind> pending = session->game->pendingBackend();
    return pending ? fromBackendKind(*pending) : -1;
}

int32_t balls_session_take_switch_outcome(BallsSession* session, int32_t* kind) {
    if (session == nullptr) {
        return 0;
    }
    const std::optional<BackendSwitchOutcome> outcome = session->game->takeBackendSwitchOutcome();
    if (!outcome) {
        return 0;
    }
    if (kind != nullptr) {
        *kind = fromBackendKind(outcome->kind);
    }
    return outcome->succeeded ? 1 : 2;
}

void balls_session_start_capacity_probe(BallsSession* session, double targetFps) {
    if (session != nullptr) {
        session->game->startCapacityProbe(targetFps);
    }
}

void balls_session_cancel_capacity_probe(BallsSession* session) {
    if (session != nullptr) {
        session->game->cancelCapacityProbe();
    }
}

void balls_session_probe(const BallsSession* session, BallsProbeInfo* probe) {
    if (session == nullptr || probe == nullptr) {
        return;
    }
    const CapacityProbe& source = session->game->capacityProbe();
    probe->active = source.isActive() ? 1 : 0;
    switch (source.phase()) {
    case ProbePhase::Settling:
        probe->phase = BALLS_PROBE_SETTLING;
        break;
    case ProbePhase::Measuring:
        probe->phase = BALLS_PROBE_MEASURING;
        break;
    case ProbePhase::Confirming:
        probe->phase = BALLS_PROBE_CONFIRMING;
        break;
    case ProbePhase::Done:
        probe->phase = BALLS_PROBE_DONE;
        break;
    case ProbePhase::Idle:
    default:
        probe->phase = BALLS_PROBE_IDLE;
        break;
    }
    probe->lastFps = source.lastFps();
    probe->targetFps = source.targetFps();
    probe->bestPassingCount = source.bestPassingCount();
}

int32_t balls_session_has_machine_ball_limit(const BallsSession* session) {
    return session != nullptr && session->game->machineBallLimit().has_value() ? 1 : 0;
}

uint64_t balls_session_machine_ball_limit(const BallsSession* session) {
    if (session == nullptr) {
        return 0;
    }
    const std::optional<BallCount> limit = session->game->machineBallLimit();
    return limit.value_or(0);
}

void balls_session_use_machine_ball_limit(BallsSession* session, uint64_t limit) {
    if (session != nullptr) {
        session->game->useMachineBallLimit(limit);
    }
}

void balls_session_fill_with_balls(BallsSession* session, uint64_t count) {
    if (session != nullptr) {
        session->game->fillWithBalls(count);
    }
}

void balls_session_set_visibility_grid(BallsSession* session, int32_t width, int32_t height) {
    if (session != nullptr) {
        session->game->simulation().backend().setVisibilityGrid(width, height);
    }
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
    switch (session->game->setBallCollisions(enabled != 0)) {
    case BallCollisionsChange::SwitchingToScalar:
        return BALLS_COLLISIONS_SWITCHING_TO_SCALAR;
    case BallCollisionsChange::ScalarNotOffered:
        return BALLS_COLLISIONS_SCALAR_NOT_OFFERED;
    case BallCollisionsChange::None:
        break;
    }
    return BALLS_COLLISIONS_UNCHANGED;
}

int32_t balls_ball_collisions_message(int32_t change, char* buffer, int32_t size) {
    const BallCollisionsChange kind = change == BALLS_COLLISIONS_SWITCHING_TO_SCALAR ? BallCollisionsChange::SwitchingToScalar
                                      : change == BALLS_COLLISIONS_SCALAR_NOT_OFFERED ? BallCollisionsChange::ScalarNotOffered
                                                                                      : BallCollisionsChange::None;
    return writeText(ballCollisionsMessage(kind), buffer, size);
}

int32_t balls_session_ball_collisions(const BallsSession* session) {
    return session != nullptr && session->game->simulation().ballCollisionsRequested() ? 1 : 0;
}

int32_t balls_session_launch_balls(BallsSession* session, int32_t color, uint64_t count) {
    if (session == nullptr || color < 0 || color >= BALLS_COLOR_COUNT) {
        return 0;
    }
    session->game->simulation().launchBalls(ballColorFromIndex(color), count);
    return 1;
}

int32_t balls_session_spawn_ball(BallsSession* session, float x, float y, float vx, float vy, int32_t color) {
    if (session == nullptr || color < 0 || color >= BALLS_COLOR_COUNT) {
        return 0;
    }
    session->game->simulation().spawnBalls({BallSpawn{x, y, vx, vy, color}});
    return 1;
}

void balls_session_tick(BallsSession* session, int32_t ticks) {
    if (session == nullptr || ticks <= 0) {
        return;
    }
    session->game->simulation().tick(ticks);
}

int32_t balls_session_last_backend_fault(const BallsSession* session, char* buffer, int32_t size) {
    return session == nullptr ? 0 : writeText(session->game->lastBackendFault(), buffer, size);
}

void balls_session_board_info(const BallsSession* session, BallsBoardInfo* info) {
    if (session == nullptr || info == nullptr) {
        return;
    }
    const BrickBoard& board = session->game->simulation().board();
    const LauncherQueue& launchers = session->game->launchers();
    info->columns = board.columns();
    info->rows = board.rows();
    info->playableHeight = session->game->level().bricks.playableHeight;
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
    if (session == nullptr || view == nullptr) {
        return;
    }
    session->refreshBoardScratch();
    const BrickBoard& board = session->game->simulation().board();
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
    if (session != nullptr) {
        session->game->simulation().board().clearChangedCells();
    }
}

void balls_session_stats(const BallsSession* session, BallsStats* stats) {
    if (session == nullptr || stats == nullptr) {
        return;
    }
    const Simulation& simulation = session->game->simulation();
    const SimulationStats& source = simulation.stats();
    stats->ballCount = simulation.backend().ballCount();
    stats->liveBallCount = simulation.liveBallCount();
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
    const QueuedLauncher* visible = session->game->launchers().visible(column, row);
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
    return session->game->launchers().remainingBalls()[static_cast<size_t>(color)];
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
    if (profile == nullptr) {
        return 0;
    }
    const std::optional<MachineProfile::MeasuredLimit> limit = profile->profile.ballLimit(toBackendKind(kind));
    if (!limit) {
        return 0;
    }
    if (targetFps != nullptr) {
        *targetFps = limit->targetFps;
    }
    if (balls != nullptr) {
        *balls = limit->balls;
    }
    return 1;
}

void balls_profile_remember_ball_limit(BallsProfile* profile, int32_t kind, double targetFps, uint64_t balls) {
    if (profile != nullptr) {
        profile->profile.rememberBallLimit(toBackendKind(kind), targetFps, balls);
    }
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

BallsEditor* balls_editor_create(void) {
    return new BallsEditor();
}

void balls_editor_destroy(BallsEditor* editor) {
    delete editor;
}

void balls_editor_new_level(BallsEditor* editor, const char* name, int32_t width, int32_t height,
                            int32_t playableHeight, int32_t launcherColumns, int32_t visibleRows) {
    if (editor == nullptr) {
        return;
    }
    editor->levelName = name != nullptr ? name : "Untitled";
    editor->editor.newLevel(editor->levelName, width, height, playableHeight, launcherColumns, visibleRows);
}

int32_t balls_editor_load_level(BallsEditor* editor, const char* levelDirectory, const char* levelName) {
    if (editor == nullptr) {
        return -1;
    }
    const auto level = loadLevelOrReport(levelDirectory, levelName);
    if (!level) {
        return -1;
    }
    editor->levelDirectory = levelDirectory;
    editor->levelName = levelName;
    editor->editor.load(*level);
    editor->editor.markSaved();
    return 0;
}

int32_t balls_editor_save_level(BallsEditor* editor, const char* levelDirectory) {
    if (editor == nullptr) {
        return -1;
    }
    const std::filesystem::path directory =
        levelDirectory != nullptr ? std::filesystem::path(levelDirectory) : std::filesystem::path(editor->levelDirectory);
    const LevelFiles files = levelFilesFor(directory, editor->levelName);
    const Result<void> saved = saveLevel(editor->editor.level(), files);
    if (!saved) {
        setError(saved.error());
        return -1;
    }
    editor->levelDirectory = directory.string();
    editor->editor.markSaved();
    return 0;
}

void balls_editor_info(const BallsEditor* editor, BallsEditorInfo* info) {
    if (editor == nullptr || info == nullptr) {
        return;
    }
    const LevelData& level = editor->editor.level();
    info->width = level.bricks.width;
    info->height = level.bricks.height;
    info->playableHeight = level.bricks.playableHeight;
    info->launcherColumns = level.launchers.columns;
    info->launcherVisibleRows = level.launchers.visibleRows;
    info->dirty = editor->editor.isDirty() ? 1 : 0;
    info->canUndo = editor->editor.canUndo() ? 1 : 0;
    info->canRedo = editor->editor.canRedo() ? 1 : 0;
    info->brickCount = static_cast<int32_t>(level.bricks.types.size());
}

int32_t balls_editor_brick_at(const BallsEditor* editor, int32_t column, int32_t row) {
    return editor == nullptr ? -1 : editor->editor.brickIdAt(column, row);
}

int32_t balls_editor_brick_color(const BallsEditor* editor, int32_t column, int32_t row) {
    if (editor == nullptr) {
        return -1;
    }
    const int brick = editor->editor.brickIdAt(column, row);
    if (brick < 0) {
        return -1;
    }
    for (const BrickType& type : editor->editor.level().bricks.types) {
        if (type.id == brick) {
            return toIndex(type.color);
        }
    }
    return -1;
}

int32_t balls_editor_brick_hits(const BallsEditor* editor, int32_t column, int32_t row) {
    if (editor == nullptr) {
        return 0;
    }
    const int brick = editor->editor.brickIdAt(column, row);
    if (brick < 0) {
        return 0;
    }
    for (const BrickType& type : editor->editor.level().bricks.types) {
        if (type.id == brick) {
            return type.hits;
        }
    }
    return 0;
}

void balls_editor_resize_board(BallsEditor* editor, int32_t width, int32_t height, int32_t playableHeight) {
    if (editor != nullptr) {
        editor->editor.resizeBoard(width, height, playableHeight);
    }
}

void balls_editor_paint_cell(BallsEditor* editor, int32_t column, int32_t row, int32_t color, int32_t hits) {
    if (editor == nullptr || color < 0 || color >= BALLS_COLOR_COUNT) {
        return;
    }
    editor->editor.paintCell(column, row, ballColorFromIndex(color), hits);
}

int32_t balls_editor_extend_brick(BallsEditor* editor, int32_t column, int32_t row) {
    if (editor == nullptr) {
        return 0;
    }
    const int brick = editor->editor.brickIdAt(column, row);
    return brick >= 0 && editor->editor.extendBrick(brick, column, row) ? 1 : 0;
}

void balls_editor_erase_cell(BallsEditor* editor, int32_t column, int32_t row) {
    if (editor != nullptr) {
        editor->editor.eraseCell(column, row);
    }
}

void balls_editor_set_brick_hits(BallsEditor* editor, int32_t column, int32_t row, int32_t hits) {
    if (editor == nullptr) {
        return;
    }
    const int brick = editor->editor.brickIdAt(column, row);
    if (brick >= 0) {
        editor->editor.setBrickHits(brick, hits);
    }
}

void balls_editor_set_brick_color(BallsEditor* editor, int32_t column, int32_t row, int32_t color) {
    if (editor == nullptr || color < 0 || color >= BALLS_COLOR_COUNT) {
        return;
    }
    const int brick = editor->editor.brickIdAt(column, row);
    if (brick >= 0) {
        editor->editor.setBrickColor(brick, ballColorFromIndex(color));
    }
}

void balls_editor_set_launcher_columns(BallsEditor* editor, int32_t columns) {
    if (editor != nullptr) {
        editor->editor.setLauncherColumns(columns);
    }
}

void balls_editor_set_visible_rows(BallsEditor* editor, int32_t rows) {
    if (editor != nullptr) {
        editor->editor.setVisibleRows(rows);
    }
}

void balls_editor_set_launcher(BallsEditor* editor, int32_t column, int32_t row, int32_t present, int32_t color,
                               int32_t ballCount) {
    if (editor == nullptr) {
        return;
    }
    if (present == 0) {
        editor->editor.setLauncher(column, row, std::nullopt);
        return;
    }
    if (color < 0 || color >= BALLS_COLOR_COUNT) {
        return;
    }
    editor->editor.setLauncher(column, row, Launcher{ballColorFromIndex(color), ballCount});
}

void balls_editor_remove_launcher_row(BallsEditor* editor, int32_t row) {
    if (editor != nullptr) {
        editor->editor.removeLauncherRow(row);
    }
}

void balls_editor_auto_fill_launchers(BallsEditor* editor, int32_t ballsPerLauncher) {
    if (editor != nullptr) {
        editor->editor.autoFillLaunchers(ballsPerLauncher);
    }
}

void balls_editor_begin_edit(BallsEditor* editor) {
    if (editor != nullptr) {
        editor->editor.beginEdit();
    }
}

int32_t balls_editor_undo(BallsEditor* editor) {
    return editor != nullptr && editor->editor.undo() ? 1 : 0;
}

int32_t balls_editor_redo(BallsEditor* editor) {
    return editor != nullptr && editor->editor.redo() ? 1 : 0;
}

void balls_editor_mark_saved(BallsEditor* editor) {
    if (editor != nullptr) {
        editor->editor.markSaved();
    }
}

void balls_editor_validate(const BallsEditor* editor, int32_t mode, BallsValidation* validation) {
    if (editor == nullptr || validation == nullptr) {
        return;
    }
    editor->validation = editor->editor.validate(mode == BALLS_MODE_PERFORMANCE ? GameMode::Performance
                                                                                : GameMode::Normal);
    for (int color = 0; color < BALLS_COLOR_COUNT; ++color) {
        validation->brickHits[color] = static_cast<uint64_t>(editor->validation.brickHits[static_cast<size_t>(color)]);
        validation->balls[color] = static_cast<uint64_t>(editor->validation.balls[static_cast<size_t>(color)]);
    }
    validation->problemCount = static_cast<int32_t>(editor->validation.problems.size());
    validation->playable = editor->validation.isPlayable() ? 1 : 0;
}

int32_t balls_editor_problem(const BallsEditor* editor, int32_t index, char* buffer, int32_t size) {
    if (editor == nullptr || index < 0 || static_cast<size_t>(index) >= editor->validation.problems.size()) {
        return 0;
    }
    return writeText(editor->validation.problems[static_cast<size_t>(index)], buffer, size);
}

} // extern "C"
