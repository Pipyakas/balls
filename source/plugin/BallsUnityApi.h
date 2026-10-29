#pragma once

// Flat C interface over the engine-agnostic core, for hosts whose own build system owns the main loop: the Unity
// front end (a native plugin loaded with DllImport) is the first user. Everything crosses the boundary as C types:
// no STL, no exceptions, no callbacks. Pointers handed out here point into core-owned memory and stay valid until
// the next call that mutates the same session (update, load, restart, backend switch), so a host copies them
// immediately or renders from them within that frame.

#include <stdint.h>

#define BALLS_UNITY_ABI_VERSION 3

#if defined(_WIN32)
#define BALLS_API __declspec(dllexport)
#else
#define BALLS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors GameSession/BackendKind/CapacityProbe enums. Kept as int32_t so a host language needs no C++ enums.
enum {
    BALLS_MODE_NORMAL = 0,
    BALLS_MODE_PERFORMANCE = 1,
};

enum {
    BALLS_BACKEND_SCALAR = 0,
    BALLS_BACKEND_AVX2 = 1,
    BALLS_BACKEND_CUDA = 2,
    BALLS_BACKEND_HYBRID = 3,
    BALLS_BACKEND_VULKAN = 4,
    BALLS_BACKEND_COUNT = 5,
};

enum {
    BALLS_STATE_READY = 0,
    BALLS_STATE_RUNNING = 1,
    BALLS_STATE_WON = 2,
};

enum {
    BALLS_PROBE_IDLE = 0,
    BALLS_PROBE_SETTLING = 1,
    BALLS_PROBE_MEASURING = 2,
    BALLS_PROBE_CONFIRMING = 3,
    BALLS_PROBE_DONE = 4,
};

enum {
    BALLS_COLOR_RED = 0,
    BALLS_COLOR_ORANGE = 1,
    BALLS_COLOR_YELLOW = 2,
    BALLS_COLOR_GREEN = 3,
    BALLS_COLOR_BLUE = 4,
    BALLS_COLOR_INDIGO = 5,
    BALLS_COLOR_VIOLET = 6,
    BALLS_COLOR_COUNT = 7,
};

// Mirrors BallCollisionsChange: what turning ball collisions on did.
enum {
    BALLS_COLLISIONS_UNCHANGED = 0,
    BALLS_COLLISIONS_SWITCHING_TO_SCALAR = 1,
    BALLS_COLLISIONS_SCALAR_NOT_OFFERED = 2,
};

// Mirrors LevelDiskState: whether the level in play still matches its files.
enum {
    BALLS_DISK_UNCHANGED = 0,
    BALLS_DISK_CHANGED = 1,
    BALLS_DISK_REMOVED = 2,
};

enum {
    BALLS_ORIENTATION_AUTO = 0,
    BALLS_ORIENTATION_LANDSCAPE = 1,
    BALLS_ORIENTATION_PORTRAIT = 2,
};

// Packed ball render coordinates, the same encoding the CPU/CUDA backends pack for their own instanced draws:
// bits 0-13 x, bits 14-27 y, bits 28-31 color. Scale a field of fieldWidth x fieldHeight with
// balls_render_pack_scale().
typedef struct BallsSnapshot {
    const uint32_t* packed;
    uint64_t count;
    float fieldWidth;
    float fieldHeight;
    int32_t residentInRenderBuffer;
    int32_t visibilityGrid;
} BallsSnapshot;

typedef struct BallsBoardInfo {
    int32_t columns;
    int32_t rows;
    int32_t playableHeight;
    int32_t paddedWidth;
    int32_t paddedCellCount;
    int32_t brickCount;
    int32_t aliveBrickCount;
    uint64_t revision;
    int32_t launcherColumns;
    int32_t launcherVisibleRows;
    int32_t launcherRemainingBalls;
} BallsBoardInfo;

// Views into core-owned arrays. Every pointer is valid until the next balls_session_update() or a mutation.
typedef struct BallsBoardView {
    const int32_t* paddedCells;      // (rows + 2) * (cols + 2): brick index, or -1
    const int32_t* brickColors;      // per brick: BALLS_COLOR_*
    const int32_t* brickHitsLeft;    // per brick: remaining hits, 0 once broken
    const float* brickRiseOffset;    // per brick: settle-in animation offset, in cells
    const float* brickHitFlash;      // per brick: seconds of hit flash left (0.15 when just hit)
    const int32_t* aliveBricks;      // brick indices still on the board
    const int32_t* changedCells;     // padded cell indices dirtied since balls_session_clear_changed_cells()
    int32_t changedCellCount;
    int32_t brickCount;
    int32_t aliveBrickCount;
} BallsBoardView;

typedef struct BallsStats {
    uint64_t ballCount;         // balls the physics backend holds
    uint64_t liveBallCount;     // including spawns not yet handed to the backend
    uint64_t ballsByColor[BALLS_COLOR_COUNT];
    uint64_t totalBallsSpawned;
    uint64_t totalHits;
    double lastFramePhysicsMs;
    int32_t ticksLastFrame;
} BallsStats;

typedef struct BallsLauncher {
    int32_t present;
    int32_t color;
    int32_t ballCount;
    float riseOffset;
} BallsLauncher;

typedef struct BallsProbeInfo {
    int32_t active;
    int32_t phase;
    double lastFps;
    double targetFps;
    uint64_t bestPassingCount;
} BallsProbeInfo;

typedef struct BallsLevelInfo {
    char name[128];
    char bricksPath[512];
    char launchersPath[512];
} BallsLevelInfo;

typedef struct BallsSettings {
    int32_t ballCollisions;
    int32_t orientation;
    int32_t vsync;
    int32_t advancedSettings;  // whether the control gadget shows its Advanced Settings section
    char defaultBackend[64];
} BallsSettings;

typedef struct BallsValidation {
    uint64_t brickHits[BALLS_COLOR_COUNT];
    uint64_t balls[BALLS_COLOR_COUNT];
    int32_t problemCount;
    int32_t playable;
} BallsValidation;

// The game's gameplay layout for one frame size, in layout units (1 unit = 1 px at 1440x900). Computed by the core
// so every front end lays out the same way: header, main board frame, interaction gadget and debug gadget.
typedef struct BallsLayout {
    float header[4];
    float main[4];
    float interaction[4];
    float debug[4];
    float boardOrigin[2];
    float boardRect[4];
    float cellSize;
    int32_t portrait;
} BallsLayout;

typedef struct BallsEditorInfo {
    int32_t width;
    int32_t height;
    int32_t playableHeight;
    int32_t launcherColumns;
    int32_t launcherVisibleRows;
    int32_t dirty;
    int32_t canUndo;
    int32_t canRedo;
    int32_t brickCount;
} BallsEditorInfo;

// One row of the machine benchmark: a candidate backend, its state and its score.
typedef struct BallsBenchmarkRow {
    char label[128];
    int32_t kind;
    int32_t status;          // 0 pending, 1 running, 2 done, 3 failed, 4 unavailable
    double ballStepsPerSecond;
    double progress;         // 0..1 while running
    char failure[256];
    double estimatedBalls;   // balls this backend holds at 60 FPS, once done
    char result[128];        // the game's result text, empty while waiting or running
} BallsBenchmarkRow;

// The core's rolling one-second frame statistics, as the debug gadget shows them.
typedef struct BallsFrameStats {
    double framesPerSecond;
    double worstFpsLastSecond;
    double workFramesPerSecond;
    double worstWorkFpsLastSecond;
    double frameMs;  // averages over the window
    double workMs;
    double gpuMs;
} BallsFrameStats;

typedef struct BallsSession BallsSession;
typedef struct BallsFrameTimings BallsFrameTimings;
typedef struct BallsBenchmark BallsBenchmark;
typedef struct BallsProfile BallsProfile;

// What the core's control API needs from the host for one request; the pointers only have to live for the call.
typedef struct BallsControlContext {
    const char* renderer;
    const char* screen;          // main-menu, level-select, gameplay, editor, settings, benchmark
    int32_t windowWidth;
    int32_t windowHeight;
    int32_t preferredBackend;    // BALLS_BACKEND_*
    int32_t vsync;
    const char* toast;           // the toast on screen, or empty
    const char* extraFields;     // more /state fields as "name":value pairs joined by commas, or empty
    const char* screenDetails;   // /state "details" JSON outside gameplay, or empty
    BallsFrameTimings* frames;
    BallsSettings settings;
    // The game in play; session is null outside gameplay.
    BallsSession* session;
    int32_t mode;
    const char* levelName;
    const char* loadError;
    int32_t diskState;           // BALLS_DISK_*
    int32_t paused;
    int32_t probeTargetFps;
    // The benchmark screen's run, or null; the stored record comes from profile.
    BallsBenchmark* benchmark;
    BallsProfile* profile;
} BallsControlContext;

// What a request asks the host to do after balls_control_handle returns, in order.
enum {
    BALLS_CONTROL_NONE = 0,
    BALLS_CONTROL_SCREENSHOT = 1,         // text: the path; the host answers the request itself once it is written
    BALLS_CONTROL_PLAY = 2,               // mode, text: the level name
    BALLS_CONTROL_MAIN_MENU = 3,
    BALLS_CONTROL_LEVEL_SELECT = 4,       // mode
    BALLS_CONTROL_EDITOR = 5,             // mode, text: the level name or empty for a new level
    BALLS_CONTROL_QUIT = 6,
    BALLS_CONTROL_APPLY_SETTINGS = 7,     // settings, value: the default backend or -1
    BALLS_CONTROL_OPEN_SETTINGS = 8,
    BALLS_CONTROL_SET_VSYNC = 9,          // value: 1 on, 0 off
    BALLS_CONTROL_RUN_BENCHMARK = 10,
    BALLS_CONTROL_SKIP_BENCHMARK = 11,
    BALLS_CONTROL_CONTINUE_BENCHMARK = 12,
    BALLS_CONTROL_SET_PAUSED = 13,        // value: 1 paused, 0 running
    BALLS_CONTROL_RESTART = 14,
    BALLS_CONTROL_START_PROBE = 15,       // value: the target FPS
};

typedef struct BallsControlAction {
    int32_t kind;
    int32_t mode;
    int32_t value;
    char text[1024];
    BallsSettings settings;
} BallsControlAction;

typedef struct BallsControl BallsControl;
typedef struct BallsEditor BallsEditor;
typedef struct BallsLevelWatcher BallsLevelWatcher;

// --- Process-wide --------------------------------------------------------------------------------------------

BALLS_API int32_t balls_abi_version(void);

// Where balls_physics_*.dll live when the host executable is not in the same folder (Unity's editor, for one).
// Call once at start-up, before creating a session.
BALLS_API void balls_set_plugin_directory(const char* directory);

// Last failure as text, for logging. Returns the length written.
BALLS_API int32_t balls_last_error(char* buffer, int32_t size);

BALLS_API float balls_render_pack_scale(float fieldExtent);

// The core's theme (ui/Theme.h), so the host converts the game's colors and sizes instead of copying them.
enum {
    BALLS_THEME_BACKGROUND = 0,
    BALLS_THEME_BOARD_BACKDROP = 1,
    BALLS_THEME_PANEL = 2,
    BALLS_THEME_CONTROL = 3,
    BALLS_THEME_CONTROL_HOVER = 4,
    BALLS_THEME_CONTROL_PRESSED = 5,
    BALLS_THEME_BORDER = 6,
    BALLS_THEME_TEXT = 7,
    BALLS_THEME_TEXT_STRONG = 8,
    BALLS_THEME_TEXT_MUTED = 9,
    BALLS_THEME_TEXT_DIM = 10,
    BALLS_THEME_ACCENT = 11,
    BALLS_THEME_DANGER = 12,
    BALLS_THEME_SUCCESS = 13,
    BALLS_THEME_COLOR_COUNT = 14,
};

enum {
    BALLS_THEME_BRICK_SHADE = 0,
    BALLS_THEME_BRICK_HIT_FLASH = 1,
    BALLS_THEME_BRICK_HIT_FLASH_SECONDS = 2,
    BALLS_THEME_TOAST_SECONDS = 3,
    BALLS_THEME_TOAST_TEXT_SIZE = 4,
    BALLS_THEME_OPTION_ROW_HEIGHT = 5,
    BALLS_THEME_OPTION_ROW_GAP = 6,
    BALLS_THEME_ADVANCED_HEADER_HEIGHT = 7,
    BALLS_THEME_SIZE_DISPLAY = 8,
    BALLS_THEME_SIZE_TITLE = 9,
    BALLS_THEME_SIZE_LARGE = 10,
    BALLS_THEME_SIZE_HEADING = 11,
    BALLS_THEME_SIZE_BIG = 12,
    BALLS_THEME_SIZE_MEDIUM = 13,
    BALLS_THEME_SIZE_BODY = 14,
    BALLS_THEME_SIZE_HEADER = 15,
    BALLS_THEME_SIZE_LABEL = 16,
    BALLS_THEME_SIZE_VALUE = 17,
    BALLS_THEME_SIZE_SMALL = 18,
    BALLS_THEME_SIZE_TINY = 19,
    BALLS_THEME_VALUE_COUNT = 20,
};

// Colors are sRGB packed as 0xRRGGBBAA; an unknown id gives 0.
BALLS_API uint32_t balls_theme_color(int32_t id);
BALLS_API uint32_t balls_theme_ball_color(int32_t colorIndex);
BALLS_API uint32_t balls_theme_brick_color(int32_t colorIndex);
BALLS_API float balls_theme_value(int32_t id);

BALLS_API void balls_compute_layout(float frameWidth, float frameHeight, int32_t boardColumns, int32_t boardRows,
                                    int32_t orientation, int32_t launcherRows, BallsLayout* layout);

BALLS_API int32_t balls_backend_available(int32_t kind);
BALLS_API int32_t balls_backend_name(int32_t kind, char* buffer, int32_t size);
BALLS_API int32_t balls_backend_reason(int32_t kind, char* buffer, int32_t size);
BALLS_API int32_t balls_backend_detail(int32_t kind, char* buffer, int32_t size);
BALLS_API int32_t balls_fastest_cpu_backend(void);

// One physical device the physics can run on, the CPU first (see sim/DeviceInventory.h). ``type`` is the core's
// PhysicsDeviceType: 0 CPU, 1 discrete GPU, 2 integrated GPU, 3 other GPU, 4 NPU. A -1 device index means that
// plugin cannot run on it.
typedef struct BallsDeviceInfo {
    char id[32];
    char name[256];
    int32_t type;
    uint64_t memoryBytes;
    int32_t vulkanDevice;
    int32_t cudaDevice;
} BallsDeviceInfo;

// Fills up to ``capacity`` devices and returns how many were written, or the total with a null pointer. The list is
// built once per process: the plugins open their drivers to answer, so calling this from OnGUI every frame is cheap
// but the first call is not.
BALLS_API int32_t balls_devices_find(BallsDeviceInfo* devices, int32_t capacity);
// The adapter the Vulkan physics plugin runs on: "auto" (default), an index, "integrated"/"discrete", or a name
// substring; ignored by backends with no adapter choice.
BALLS_API int32_t balls_compute_gpu(char* buffer, int32_t size);
BALLS_API int32_t balls_set_compute_gpu(const char* preference);

// Which backend runs on which device: the game's Devices rows. One pick runs that backend alone on that device, two
// or more run the composer (Hybrid) with one part per pick, and a device takes at most one pick.
typedef struct BallsPick {
    char device[32];  // PhysicalDevice::id, "cpu" or "gpu0"...
    int32_t kind;
} BallsPick;

// The configured picks, which may be empty. Returns how many there are, writing at most ``capacity``.
BALLS_API int32_t balls_picks_get(BallsPick* picks, int32_t capacity);
// What the device rows should show while ``runningKind`` is the active backend (the picks that describe it, else the
// picks Hybrid would compose or the kind's default device).
BALLS_API int32_t balls_picks_display(int32_t runningKind, BallsPick* picks, int32_t capacity);
// Replaces the picks. Returns 1 when they can run here; 0 otherwise, with the reason in ``problem``.
BALLS_API int32_t balls_picks_set(const BallsPick* picks, int32_t count, char* problem, int32_t problemSize);
// The kinds a device row offers, fastest first; Scalar only where nothing faster runs.
BALLS_API int32_t balls_pickable_kinds(const char* device, int32_t* kinds, int32_t capacity);
// The picks Hybrid uses when none are configured: the CPU and every discrete GPU.
BALLS_API int32_t balls_default_hybrid_picks(BallsPick* picks, int32_t capacity);
// "AVX2 (CPU) + CUDA (RTX 2060)" for menus, and the backend those picks run as.
BALLS_API int32_t balls_picks_label(const BallsPick* picks, int32_t count, char* buffer, int32_t size);
BALLS_API int32_t balls_kind_for_picks(const BallsPick* picks, int32_t count);
// The Settings default backend as the settings file stores it: the kind's name, or Hybrid as its device picks
// ("AVX2@cpu+CUDA@gpu0"), so a multi-device choice survives a restart.
BALLS_API int32_t balls_backend_stored_name(int32_t kind, char* buffer, int32_t size);
// Reads that value back, making stored picks the process-wide picks. -1 when it names nothing usable here.
BALLS_API int32_t balls_backend_parse_saved(const char* text);

// --- Levels and settings -------------------------------------------------------------------------------------

// Fills up to capacity entries and returns how many levels the directory holds.
BALLS_API int32_t balls_levels_find(const char* directory, BallsLevelInfo* levels, int32_t capacity);

// The core's LevelWatcher over a level's file pair. poll checks the files at most every 250 ms and returns a
// BALLS_DISK_* state; Changed stays until mark_loaded (or a new watcher).
BALLS_API BallsLevelWatcher* balls_level_watcher_create(const char* levelDirectory, const char* levelName);
BALLS_API void balls_level_watcher_destroy(BallsLevelWatcher* watcher);
BALLS_API int32_t balls_level_watcher_poll(BallsLevelWatcher* watcher);
BALLS_API void balls_level_watcher_mark_loaded(BallsLevelWatcher* watcher);
BALLS_API int32_t balls_level_disk_notice(int32_t state, char* buffer, int32_t size);

// One level pair the importer was given: what happened to it and the toast line that names it.
typedef struct BallsImportResult {
    int32_t status;  // 0 imported, 1 replaced an existing level, 2 failed
    char name[128];
    char message[256];
    int32_t hasWarning;
} BallsImportResult;

// Copies level pairs into a mode's level folder. ``picked`` may hold either file of a pair or a folder of pairs;
// the partner is found next to the picked file by name, and a pair is parsed before it is copied, so a broken file
// never lands in the folder. Returns how many results there were, writing at most ``capacity`` of them, and the
// toast-ready summary into ``summary``.
BALLS_API int32_t balls_levels_import(int32_t mode, const char* destination, const char* const* picked,
                                      int32_t pickedCount, BallsImportResult* results, int32_t capacity,
                                      char* summary, int32_t summarySize);

BALLS_API int32_t balls_settings_load(const char* path, BallsSettings* settings);
BALLS_API int32_t balls_settings_save(const char* path, const BallsSettings* settings);

// --- Session -------------------------------------------------------------------------------------------------

BALLS_API BallsSession* balls_session_create(int32_t mode, const char* levelDirectory, const char* levelName,
                                             int32_t backend);
// The same session run by the core's SessionRunner: physics on its own thread, commands posted to it and every read
// answered from the latest view it published, so a long physics step never holds up a rendered frame. Every
// balls_session_* call works on it; balls_session_update hands over the frame instead of stepping, and reads change
// only at the next update.
BALLS_API BallsSession* balls_session_create_threaded(int32_t mode, const char* levelDirectory, const char* levelName,
                                                      int32_t backend);
BALLS_API int32_t balls_session_threaded(const BallsSession* session);
// A threaded session reports what turning ball collisions on did a frame or two later: the next BALLS_COLLISIONS_*
// change, or 0 when there is none. A session on the caller's thread returns the change from set_ball_collisions.
BALLS_API int32_t balls_session_take_collisions_change(BallsSession* session);
// How long the physics thread took for its last frame and how many it completes a second; 0 on the caller's thread.
BALLS_API double balls_session_physics_frame_ms(const BallsSession* session);
BALLS_API double balls_session_physics_fps(const BallsSession* session);
BALLS_API void balls_session_destroy(BallsSession* session);

// Re-reads a level from disk. Returns 0 on success.
BALLS_API int32_t balls_session_load_level(BallsSession* session, const char* levelDirectory, const char* levelName);
BALLS_API int32_t balls_session_restart(BallsSession* session);

// Drives one frame: launcher and brick animation, auto release, spawning, physics ticks and win detection.
BALLS_API void balls_session_update(BallsSession* session, float deltaSeconds);
BALLS_API void balls_session_report_frame(BallsSession* session, double frameSeconds, double workSeconds);

BALLS_API int32_t balls_session_mode(const BallsSession* session);
BALLS_API int32_t balls_session_state(const BallsSession* session);
BALLS_API int32_t balls_session_backend(const BallsSession* session);
BALLS_API double balls_session_elapsed_seconds(const BallsSession* session);

BALLS_API int32_t balls_session_choose_launcher(BallsSession* session, int32_t column, int32_t row);
BALLS_API void balls_session_start_auto_release(BallsSession* session);
BALLS_API void balls_session_select_spawn_rate(BallsSession* session, int32_t optionIndex);
BALLS_API void balls_session_select_ball_multiplier(BallsSession* session, int32_t optionIndex);
BALLS_API void balls_session_select_ball_limit(BallsSession* session, int32_t optionIndex);
BALLS_API int32_t balls_session_spawn_rate_index(const BallsSession* session);
BALLS_API int32_t balls_session_ball_multiplier_index(const BallsSession* session);
BALLS_API int32_t balls_session_ball_limit_index(const BallsSession* session);
BALLS_API uint64_t balls_session_ball_limit(const BallsSession* session);
BALLS_API int32_t balls_session_auto_releasing(const BallsSession* session);
// Requests a switch; the new backend is built in the background and takes over when it is ready, so the caller
// polls balls_session_pending_backend and then balls_session_take_switch_outcome once.
BALLS_API int32_t balls_session_switch_backend(BallsSession* session, int32_t kind);
BALLS_API int32_t balls_session_pending_backend(const BallsSession* session);
// 0 when no request has finished, 1 when it succeeded (the new backend is live), 2 when it failed.
BALLS_API int32_t balls_session_take_switch_outcome(BallsSession* session, int32_t* kind);

BALLS_API void balls_session_start_capacity_probe(BallsSession* session, double targetFps);
BALLS_API void balls_session_cancel_capacity_probe(BallsSession* session);
BALLS_API void balls_session_probe(const BallsSession* session, BallsProbeInfo* probe);
BALLS_API int32_t balls_session_has_machine_ball_limit(const BallsSession* session);
BALLS_API uint64_t balls_session_machine_ball_limit(const BallsSession* session);
BALLS_API void balls_session_use_machine_ball_limit(BallsSession* session, uint64_t limit);
BALLS_API void balls_session_fill_with_balls(BallsSession* session, uint64_t count);

// Grid resolution the GPU backends sample when drawing millions of balls; without one they fall back to copying
// every ball. Set it once after creating a session, to the render target's ball-area size in cells.
BALLS_API void balls_session_set_visibility_grid(BallsSession* session, int32_t width, int32_t height);

BALLS_API void balls_set_compute_gpu_preference(const char* preference);
BALLS_API int32_t balls_compute_gpu_preference(char* buffer, int32_t size);

// Applies the Ball collisions setting; requests Scalar when collisions need it and it is offered. Returns a
// BALLS_COLLISIONS_* value; balls_ball_collisions_message gives the toast for it.
BALLS_API int32_t balls_session_set_ball_collisions(BallsSession* session, int32_t enabled);
BALLS_API int32_t balls_ball_collisions_message(int32_t change, char* buffer, int32_t size);
BALLS_API int32_t balls_session_ball_collisions(const BallsSession* session);
BALLS_API int32_t balls_session_launch_balls(BallsSession* session, int32_t color, uint64_t count);

// Deterministic entry points for the parity harness: one ball with an exact position and velocity, then a fixed
// number of ticks with no wall-clock coupling.
BALLS_API int32_t balls_session_spawn_ball(BallsSession* session, float x, float y, float vx, float vy, int32_t color);
BALLS_API void balls_session_tick(BallsSession* session, int32_t ticks);

BALLS_API int32_t balls_session_last_backend_fault(const BallsSession* session, char* buffer, int32_t size);

BALLS_API void balls_session_board_info(const BallsSession* session, BallsBoardInfo* info);
BALLS_API void balls_session_board_view(BallsSession* session, BallsBoardView* view);
BALLS_API void balls_session_clear_changed_cells(BallsSession* session);
BALLS_API void balls_session_stats(const BallsSession* session, BallsStats* stats);
BALLS_API int32_t balls_session_snapshot(BallsSession* session, BallsSnapshot* snapshot);
BALLS_API int32_t balls_session_launcher(const BallsSession* session, int32_t column, int32_t row,
                                         BallsLauncher* launcher);
BALLS_API uint64_t balls_session_remaining_balls_by_color(const BallsSession* session, int32_t color);

// --- Machine benchmark -----------------------------------------------------------------------------------------

// Runs every loadable backend on the level given, on a worker thread; the caller polls.
BALLS_API BallsBenchmark* balls_benchmark_create(const char* levelDirectory, const char* levelName);
BALLS_API void balls_benchmark_destroy(BallsBenchmark* benchmark);
BALLS_API void balls_benchmark_start(BallsBenchmark* benchmark);
BALLS_API void balls_benchmark_cancel(BallsBenchmark* benchmark);
BALLS_API int32_t balls_benchmark_finished(const BallsBenchmark* benchmark);
// Fills up to capacity rows and returns how many the run has.
BALLS_API int32_t balls_benchmark_rows(BallsBenchmark* benchmark, BallsBenchmarkRow* rows, int32_t capacity);
// The winner's kind, or -1 while nothing has finished.
BALLS_API int32_t balls_benchmark_winner(BallsBenchmark* benchmark);

// --- Control API -----------------------------------------------------------------------------------------------

// The game's HTTP control API answered by the core, for a host that brings its own transport. levelDirectories are the
// Normal and Performance level folders, for /play and /editor.
BALLS_API BallsControl* balls_control_create(const char* normalLevels, const char* performanceLevels);
BALLS_API void balls_control_destroy(BallsControl* control);
// Answers one request target ("/play?mode=normal&level=DA_FirstLevel"). Returns the HTTP status with the JSON body in
// json; 0 when the answer waits for the host (a screenshot); -1 when the path is not the core's, for the host's own
// routes. jsonLength gets the body's full length, which can exceed size. Gameplay commands are applied to the
// context's session before this returns; what the host has to do itself is queued for balls_control_take_action.
BALLS_API int32_t balls_control_handle(BallsControl* control, const BallsControlContext* context, const char* target,
                                       char* json, int32_t size, int32_t* jsonLength);
// The next queued action, or 0 when there is none.
BALLS_API int32_t balls_control_take_action(BallsControl* control, BallsControlAction* action);

// --- Frame timings -------------------------------------------------------------------------------------------

// workMs is the frame without waits for the display; pass frameMs when the host cannot tell them apart.
BALLS_API BallsFrameTimings* balls_frame_timings_create(void);
BALLS_API void balls_frame_timings_destroy(BallsFrameTimings* timings);
BALLS_API void balls_frame_timings_record(BallsFrameTimings* timings, double frameMs, double workMs, double gpuMs);
BALLS_API void balls_frame_timings_stats(const BallsFrameTimings* timings, BallsFrameStats* stats);

// --- Machine profile -----------------------------------------------------------------------------------------

// The core's MachineProfile over a folder (machine.txt, benchmark.txt), loaded on open. It also reads the limit lines
// of the machine-profile.txt older Unity builds wrote there.
BALLS_API BallsProfile* balls_profile_open(const char* directory);
BALLS_API void balls_profile_close(BallsProfile* profile);
// 1 with the capacity probe's result for the backend, 0 when it was never probed here.
BALLS_API int32_t balls_profile_ball_limit(const BallsProfile* profile, int32_t kind, double* targetFps, uint64_t* balls);
BALLS_API void balls_profile_remember_ball_limit(BallsProfile* profile, int32_t kind, double targetFps, uint64_t balls);
// 1 with the best finished benchmark speed of the backend in the stored record, 0 when there is none.
BALLS_API int32_t balls_profile_benchmark_score(const BallsProfile* profile, int32_t kind, double* ballStepsPerSecond);
// Stores the benchmark's results, winner and this machine's fingerprint as the profile's record.
BALLS_API void balls_profile_remember_benchmark(BallsProfile* profile, BallsBenchmark* benchmark, int32_t complete,
                                                const char* gameVersion);

// --- Editor --------------------------------------------------------------------------------------------------

BALLS_API BallsEditor* balls_editor_create(void);
BALLS_API void balls_editor_destroy(BallsEditor* editor);

BALLS_API void balls_editor_new_level(BallsEditor* editor, const char* name, int32_t width, int32_t height,
                                      int32_t playableHeight, int32_t launcherColumns, int32_t visibleRows);
BALLS_API int32_t balls_editor_load_level(BallsEditor* editor, const char* levelDirectory, const char* levelName);
// Writes both files of a level back into its directory. Returns 0 on success.
BALLS_API int32_t balls_editor_save_level(BallsEditor* editor, const char* levelDirectory);
BALLS_API void balls_editor_info(const BallsEditor* editor, BallsEditorInfo* info);

// Brick id under a cell, or -1; gives the editor a way to pick what the cursor is over.
BALLS_API int32_t balls_editor_brick_at(const BallsEditor* editor, int32_t column, int32_t row);
BALLS_API int32_t balls_editor_brick_color(const BallsEditor* editor, int32_t column, int32_t row);
BALLS_API int32_t balls_editor_brick_hits(const BallsEditor* editor, int32_t column, int32_t row);

BALLS_API void balls_editor_resize_board(BallsEditor* editor, int32_t width, int32_t height, int32_t playableHeight);
BALLS_API void balls_editor_paint_cell(BallsEditor* editor, int32_t column, int32_t row, int32_t color, int32_t hits);
BALLS_API int32_t balls_editor_extend_brick(BallsEditor* editor, int32_t column, int32_t row);
BALLS_API void balls_editor_erase_cell(BallsEditor* editor, int32_t column, int32_t row);
BALLS_API void balls_editor_set_brick_hits(BallsEditor* editor, int32_t column, int32_t row, int32_t hits);
BALLS_API void balls_editor_set_brick_color(BallsEditor* editor, int32_t column, int32_t row, int32_t color);

BALLS_API void balls_editor_set_launcher_columns(BallsEditor* editor, int32_t columns);
BALLS_API void balls_editor_set_visible_rows(BallsEditor* editor, int32_t rows);
BALLS_API void balls_editor_set_launcher(BallsEditor* editor, int32_t column, int32_t row, int32_t present,
                                         int32_t color, int32_t ballCount);
BALLS_API void balls_editor_remove_launcher_row(BallsEditor* editor, int32_t row);
BALLS_API void balls_editor_auto_fill_launchers(BallsEditor* editor, int32_t ballsPerLauncher);

BALLS_API void balls_editor_begin_edit(BallsEditor* editor);
BALLS_API int32_t balls_editor_undo(BallsEditor* editor);
BALLS_API int32_t balls_editor_redo(BallsEditor* editor);
BALLS_API void balls_editor_mark_saved(BallsEditor* editor);

BALLS_API void balls_editor_validate(const BallsEditor* editor, int32_t mode, BallsValidation* validation);
BALLS_API int32_t balls_editor_problem(const BallsEditor* editor, int32_t index, char* buffer, int32_t size);

#ifdef __cplusplus
}
#endif
