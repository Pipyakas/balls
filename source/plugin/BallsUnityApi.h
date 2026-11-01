#pragma once

#include <stdint.h>

#define BALLS_UNITY_ABI_VERSION 7

#if defined(_WIN32)
#define BALLS_API __declspec(dllexport)
#else
#define BALLS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

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

enum {
    BALLS_COLLISIONS_UNCHANGED = 0,
    BALLS_COLLISIONS_SWITCHING_TO_SCALAR = 1,
    BALLS_COLLISIONS_SCALAR_NOT_OFFERED = 2,
};

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

typedef struct BallsBoardView {
    const int32_t* paddedCells;
    const int32_t* brickColors;
    const int32_t* brickHitsLeft;
    const float* brickRiseOffset;
    const float* brickHitFlash;
    const int32_t* aliveBricks;
    const int32_t* changedCells;
    int32_t changedCellCount;
    int32_t brickCount;
    int32_t aliveBrickCount;
} BallsBoardView;

typedef struct BallsStats {
    uint64_t ballCount;
    uint64_t liveBallCount;
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
    int32_t advancedSettings;
    char defaultBackend[64];
} BallsSettings;

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

typedef struct BallsBenchmarkRow {
    char label[128];
    int32_t kind;
    int32_t status;
    double ballStepsPerSecond;
    double progress;
    char failure[256];
    double estimatedBalls;
    char result[128];
} BallsBenchmarkRow;

typedef struct BallsFrameStats {
    double framesPerSecond;
    double worstFpsLastSecond;
    double workFramesPerSecond;
    double worstWorkFpsLastSecond;
    double frameMs;
    double workMs;
    double gpuMs;
} BallsFrameStats;

typedef struct BallsSession BallsSession;
typedef struct BallsFrameTimings BallsFrameTimings;
typedef struct BallsBenchmark BallsBenchmark;
typedef struct BallsProfile BallsProfile;

typedef struct BallsControlContext {
    const char* renderer;
    const char* screen;
    int32_t windowWidth;
    int32_t windowHeight;
    int32_t preferredBackend;
    int32_t vsync;
    const char* toast;
    const char* extraFields;
    const char* screenDetails;
    BallsFrameTimings* frames;
    BallsSettings settings;

    BallsSession* session;
    int32_t mode;
    const char* levelName;
    const char* loadError;
    int32_t diskState;
    int32_t paused;
    int32_t probeTargetFps;

    BallsBenchmark* benchmark;
    BallsProfile* profile;
} BallsControlContext;

enum {
    BALLS_CONTROL_NONE = 0,
    BALLS_CONTROL_SCREENSHOT = 1,
    BALLS_CONTROL_PLAY = 2,
    BALLS_CONTROL_MAIN_MENU = 3,
    BALLS_CONTROL_LEVEL_SELECT = 4,
    BALLS_CONTROL_EDITOR = 5,
    BALLS_CONTROL_QUIT = 6,
    BALLS_CONTROL_APPLY_SETTINGS = 7,
    BALLS_CONTROL_OPEN_SETTINGS = 8,
    BALLS_CONTROL_SET_VSYNC = 9,
    BALLS_CONTROL_RUN_BENCHMARK = 10,
    BALLS_CONTROL_SKIP_BENCHMARK = 11,
    BALLS_CONTROL_CONTINUE_BENCHMARK = 12,
    BALLS_CONTROL_SET_PAUSED = 13,
    BALLS_CONTROL_RESTART = 14,
    BALLS_CONTROL_START_PROBE = 15,
};

typedef struct BallsControlAction {
    int32_t kind;
    int32_t mode;
    int32_t value;
    char text[1024];
    BallsSettings settings;
} BallsControlAction;

typedef struct BallsControl BallsControl;
typedef struct BallsLevelWatcher BallsLevelWatcher;

BALLS_API int32_t balls_abi_version(void);

BALLS_API void balls_set_plugin_directory(const char* directory);

BALLS_API int32_t balls_last_error(char* buffer, int32_t size);

BALLS_API float balls_render_pack_scale(float fieldExtent);

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

typedef struct BallsDeviceInfo {
    char id[32];
    char name[256];
    int32_t type;
    uint64_t memoryBytes;
    int32_t vulkanDevice;
    int32_t cudaDevice;
} BallsDeviceInfo;

BALLS_API int32_t balls_devices_find(BallsDeviceInfo* devices, int32_t capacity);

BALLS_API int32_t balls_compute_gpu(char* buffer, int32_t size);
BALLS_API int32_t balls_set_compute_gpu(const char* preference);

typedef struct BallsPick {
    char device[32];
    int32_t kind;
} BallsPick;

BALLS_API int32_t balls_picks_get(BallsPick* picks, int32_t capacity);

BALLS_API int32_t balls_picks_display(int32_t runningKind, BallsPick* picks, int32_t capacity);

BALLS_API int32_t balls_picks_set(const BallsPick* picks, int32_t count, char* problem, int32_t problemSize);

BALLS_API int32_t balls_pickable_kinds(const char* device, int32_t* kinds, int32_t capacity);

BALLS_API int32_t balls_default_hybrid_picks(BallsPick* picks, int32_t capacity);

BALLS_API int32_t balls_picks_label(const BallsPick* picks, int32_t count, char* buffer, int32_t size);
BALLS_API int32_t balls_kind_for_picks(const BallsPick* picks, int32_t count);

BALLS_API int32_t balls_backend_stored_name(int32_t kind, char* buffer, int32_t size);

BALLS_API int32_t balls_backend_parse_saved(const char* text);

BALLS_API int32_t balls_levels_find(const char* directory, BallsLevelInfo* levels, int32_t capacity);

BALLS_API BallsLevelWatcher* balls_level_watcher_create(const char* levelDirectory, const char* levelName);
BALLS_API void balls_level_watcher_destroy(BallsLevelWatcher* watcher);
BALLS_API int32_t balls_level_watcher_poll(BallsLevelWatcher* watcher);
BALLS_API void balls_level_watcher_mark_loaded(BallsLevelWatcher* watcher);
BALLS_API int32_t balls_level_disk_notice(int32_t state, char* buffer, int32_t size);

typedef struct BallsImportResult {
    int32_t status;
    char name[128];
    char message[256];
    int32_t hasWarning;
} BallsImportResult;

BALLS_API int32_t balls_levels_import(int32_t mode, const char* destination, const char* const* picked,
                                      int32_t pickedCount, BallsImportResult* results, int32_t capacity,
                                      char* summary, int32_t summarySize);

BALLS_API int32_t balls_settings_load(const char* path, BallsSettings* settings);
BALLS_API int32_t balls_settings_save(const char* path, const BallsSettings* settings);

BALLS_API BallsSession* balls_session_create(int32_t mode, const char* levelDirectory, const char* levelName,
                                             int32_t backend);

BALLS_API BallsSession* balls_session_create_threaded(int32_t mode, const char* levelDirectory, const char* levelName,
                                                      int32_t backend);
BALLS_API int32_t balls_session_threaded(const BallsSession* session);

BALLS_API int32_t balls_session_take_collisions_change(BallsSession* session);

BALLS_API double balls_session_physics_frame_ms(const BallsSession* session);
BALLS_API double balls_session_physics_fps(const BallsSession* session);
BALLS_API void balls_session_destroy(BallsSession* session);

BALLS_API int32_t balls_session_load_level(BallsSession* session, const char* levelDirectory, const char* levelName);
BALLS_API int32_t balls_session_restart(BallsSession* session);

BALLS_API void balls_session_update(BallsSession* session, float deltaSeconds);
BALLS_API void balls_session_report_frame(BallsSession* session, double frameSeconds, double workSeconds);

BALLS_API int32_t balls_session_mode(const BallsSession* session);
BALLS_API int32_t balls_session_state(const BallsSession* session);
BALLS_API int32_t balls_session_backend(const BallsSession* session);
BALLS_API double balls_session_elapsed_seconds(const BallsSession* session);

BALLS_API int32_t balls_session_choose_launcher(BallsSession* session, int32_t column, int32_t row);
BALLS_API void balls_session_start_auto_release(BallsSession* session);
BALLS_API void balls_session_select_spawn_rate(BallsSession* session, int32_t optionIndex);
BALLS_API void balls_session_select_ball_limit(BallsSession* session, int32_t optionIndex);
BALLS_API int32_t balls_session_spawn_rate_index(const BallsSession* session);
BALLS_API int32_t balls_session_ball_limit_index(const BallsSession* session);
BALLS_API uint64_t balls_session_ball_limit(const BallsSession* session);
BALLS_API int32_t balls_session_auto_releasing(const BallsSession* session);

BALLS_API int32_t balls_session_switch_backend(BallsSession* session, int32_t kind);
BALLS_API int32_t balls_session_pending_backend(const BallsSession* session);

BALLS_API int32_t balls_session_take_switch_outcome(BallsSession* session, int32_t* kind);

BALLS_API void balls_session_start_capacity_probe(BallsSession* session, double targetFps);
BALLS_API void balls_session_cancel_capacity_probe(BallsSession* session);
BALLS_API void balls_session_probe(const BallsSession* session, BallsProbeInfo* probe);
BALLS_API int32_t balls_session_has_machine_ball_limit(const BallsSession* session);
BALLS_API uint64_t balls_session_machine_ball_limit(const BallsSession* session);
BALLS_API void balls_session_use_machine_ball_limit(BallsSession* session, uint64_t limit);
BALLS_API void balls_session_fill_with_balls(BallsSession* session, uint64_t count);

BALLS_API void balls_session_set_visibility_grid(BallsSession* session, int32_t width, int32_t height);

BALLS_API void balls_set_compute_gpu_preference(const char* preference);
BALLS_API int32_t balls_compute_gpu_preference(char* buffer, int32_t size);

BALLS_API int32_t balls_session_set_ball_collisions(BallsSession* session, int32_t enabled);
BALLS_API int32_t balls_ball_collisions_message(int32_t change, char* buffer, int32_t size);
BALLS_API int32_t balls_session_ball_collisions(const BallsSession* session);
BALLS_API int32_t balls_session_launch_balls(BallsSession* session, int32_t color, uint64_t count);

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

BALLS_API BallsBenchmark* balls_benchmark_create(const char* levelDirectory, const char* levelName);
BALLS_API void balls_benchmark_destroy(BallsBenchmark* benchmark);
BALLS_API void balls_benchmark_start(BallsBenchmark* benchmark);
BALLS_API void balls_benchmark_cancel(BallsBenchmark* benchmark);
BALLS_API int32_t balls_benchmark_finished(const BallsBenchmark* benchmark);

BALLS_API int32_t balls_benchmark_rows(BallsBenchmark* benchmark, BallsBenchmarkRow* rows, int32_t capacity);

BALLS_API int32_t balls_benchmark_winner(BallsBenchmark* benchmark);

BALLS_API BallsControl* balls_control_create(const char* normalLevels, const char* performanceLevels);
BALLS_API void balls_control_destroy(BallsControl* control);

BALLS_API int32_t balls_control_handle(BallsControl* control, const BallsControlContext* context, const char* target,
                                       char* json, int32_t size, int32_t* jsonLength);

BALLS_API int32_t balls_control_take_action(BallsControl* control, BallsControlAction* action);

BALLS_API BallsFrameTimings* balls_frame_timings_create(void);
BALLS_API void balls_frame_timings_destroy(BallsFrameTimings* timings);
BALLS_API void balls_frame_timings_record(BallsFrameTimings* timings, double frameMs, double workMs, double gpuMs);
BALLS_API void balls_frame_timings_stats(const BallsFrameTimings* timings, BallsFrameStats* stats);

BALLS_API BallsProfile* balls_profile_open(const char* directory);
BALLS_API void balls_profile_close(BallsProfile* profile);

BALLS_API int32_t balls_profile_ball_limit(const BallsProfile* profile, int32_t kind, double* targetFps, uint64_t* balls);
BALLS_API void balls_profile_remember_ball_limit(BallsProfile* profile, int32_t kind, double targetFps, uint64_t balls);

BALLS_API int32_t balls_profile_benchmark_score(const BallsProfile* profile, int32_t kind, double* ballStepsPerSecond);

BALLS_API void balls_profile_remember_benchmark(BallsProfile* profile, BallsBenchmark* benchmark, int32_t complete,
                                                const char* gameVersion);

enum {
    BALLS_SCREEN_MAIN_MENU = 0,
    BALLS_SCREEN_LEVEL_SELECT = 1,
    BALLS_SCREEN_GAMEPLAY = 2,
    BALLS_SCREEN_LOAD_ERROR = 3,
    BALLS_SCREEN_MAP_EDITOR = 4,
    BALLS_SCREEN_SETTINGS = 5,
    BALLS_SCREEN_MACHINE_BENCHMARK = 6,
};

enum {
    BALLS_OVERLAY_NONE = 0,
    BALLS_OVERLAY_PAUSE = 1,
    BALLS_OVERLAY_WIN = 2,
    BALLS_OVERLAY_SETTINGS = 3,
};

enum {
    BALLS_FLOW_NORMAL_MODE = 0,
    BALLS_FLOW_PERFORMANCE_MODE = 1,
    BALLS_FLOW_MAP_EDITOR = 2,
    BALLS_FLOW_SETTINGS = 3,
    BALLS_FLOW_QUIT = 4,
    BALLS_FLOW_PLAY_LEVEL = 5,
    BALLS_FLOW_EDIT_LEVEL = 6,
    BALLS_FLOW_NEW_LEVEL = 7,
    BALLS_FLOW_IMPORT = 8,
    BALLS_FLOW_OPEN_FOLDER = 9,
    BALLS_FLOW_TOGGLE_PAUSE = 10,
    BALLS_FLOW_RESUME = 11,
    BALLS_FLOW_RESTART = 12,
    BALLS_FLOW_PAUSE_SETTINGS = 13,
    BALLS_FLOW_MAIN_MENU = 14,
    BALLS_FLOW_NEXT_LEVEL = 15,
    BALLS_FLOW_RUN_MACHINE_BENCHMARK = 16,
    BALLS_FLOW_SKIP_BENCHMARK = 17,
    BALLS_FLOW_CONTINUE_FROM_BENCHMARK = 18,
    BALLS_FLOW_BACK = 19,
};

enum {
    BALLS_REQUEST_START_GAME = 0,
    BALLS_REQUEST_RELOAD_GAME = 1,
    BALLS_REQUEST_RESTART_GAME = 2,
    BALLS_REQUEST_END_GAME = 3,
    BALLS_REQUEST_OPEN_EDITOR = 4,
    BALLS_REQUEST_START_BENCHMARK = 5,
    BALLS_REQUEST_SKIP_BENCHMARK = 6,
    BALLS_REQUEST_PICK_LEVEL_FILES = 7,
    BALLS_REQUEST_OPEN_FOLDER = 8,
    BALLS_REQUEST_QUIT = 9,
};

enum {
    BALLS_FLOW_TEXT_TITLE = 0,
    BALLS_FLOW_TEXT_FOOTNOTE = 1,
    BALLS_FLOW_TEXT_GAME_TITLE = 2,
    BALLS_FLOW_TEXT_DISK_NOTICE = 3,
    BALLS_FLOW_TEXT_LOAD_ERROR = 4,
    BALLS_FLOW_TEXT_TOAST = 5,
    BALLS_FLOW_TEXT_SCREEN_NAME = 6,
    BALLS_FLOW_TEXT_LEVEL_FOLDER = 7,
};

typedef struct BallsFlow BallsFlow;

typedef struct BallsFlowState {
    int32_t screen;
    int32_t overlay;
    int32_t mode;
    int32_t paused;
    int32_t diskState;
    int32_t canRunMachineBenchmark;
    int32_t benchmarkSettled;
    int32_t buttonCount;
    int32_t levelCount;
    int32_t hasLevel;
    float toastSecondsLeft;
    int32_t toastIsError;
} BallsFlowState;

typedef struct BallsFlowButton {
    int32_t action;
    int32_t enabled;
    char label[64];
} BallsFlowButton;

typedef struct BallsFlowLevelRow {
    char name[128];
    int32_t highlighted;
} BallsFlowLevelRow;

typedef struct BallsFlowRequest {
    int32_t kind;
    int32_t mode;
    int32_t hasLevel;
    BallsLevelInfo level;
    char folder[512];
} BallsFlowRequest;

BALLS_API BallsFlow* balls_flow_create(const char* normalLevels, const char* performanceLevels);
BALLS_API void balls_flow_destroy(BallsFlow* flow);
BALLS_API void balls_flow_update(BallsFlow* flow, float deltaSeconds);

BALLS_API int32_t balls_flow_activate(BallsFlow* flow, int32_t action, int32_t row);
BALLS_API int32_t balls_flow_escape(BallsFlow* flow);

BALLS_API void balls_flow_show_main_menu(BallsFlow* flow);
BALLS_API void balls_flow_show_level_select(BallsFlow* flow, int32_t mode);

BALLS_API int32_t balls_flow_start_game(BallsFlow* flow, int32_t mode, const char* levelName);

BALLS_API void balls_flow_open_editor(BallsFlow* flow, int32_t mode, const char* levelName);
BALLS_API void balls_flow_show_settings(BallsFlow* flow);
BALLS_API void balls_flow_run_machine_benchmark(BallsFlow* flow);

BALLS_API void balls_flow_level_loaded(BallsFlow* flow);
BALLS_API void balls_flow_level_failed(BallsFlow* flow, const char* error);
BALLS_API void balls_flow_session_state(BallsFlow* flow, int32_t state);
BALLS_API void balls_flow_benchmark_finished(BallsFlow* flow);
BALLS_API void balls_flow_play_from_editor(BallsFlow* flow, int32_t mode, const char* levelName);
BALLS_API void balls_flow_import(BallsFlow* flow, const char* const* picked, int32_t pickedCount);
BALLS_API void balls_flow_show_toast(BallsFlow* flow, const char* text, int32_t isError);

BALLS_API int32_t balls_flow_take_request(BallsFlow* flow, BallsFlowRequest* request);

BALLS_API int32_t balls_flow_take_scroll_to_row(BallsFlow* flow);
BALLS_API void balls_flow_state(const BallsFlow* flow, BallsFlowState* state);
BALLS_API int32_t balls_flow_text(const BallsFlow* flow, int32_t which, char* buffer, int32_t size);
BALLS_API int32_t balls_flow_button(const BallsFlow* flow, int32_t index, BallsFlowButton* button);
BALLS_API int32_t balls_flow_level_row(const BallsFlow* flow, int32_t index, BallsFlowLevelRow* row);

typedef struct BallsUiOption {
    char label[128];
    int32_t enabled;
    int32_t selected;
    char detail[256];
    int32_t kind;
} BallsUiOption;

enum {
    BALLS_MENU_ORIENTATION = 0,
    BALLS_MENU_OFF_ON = 1,
    BALLS_MENU_SPAWN_RATE = 2,
    BALLS_MENU_BALL_LIMIT = 4,
    BALLS_MENU_GAMEPLAY_BACKENDS = 5,
    BALLS_MENU_RELEASE = 6,
    BALLS_MENU_BALL_COLLISIONS = 7,
};

BALLS_API int32_t balls_menu_options(int32_t list, int32_t value, BallsSession* session, BallsUiOption* options,
                                     int32_t capacity);

BALLS_API int32_t balls_menu_default_backends(int32_t preferredKind, const BallsProfile* profile,
                                              BallsUiOption* options, int32_t capacity);

BALLS_API int32_t balls_menu_auto_ball_limit(BallsSession* session);
BALLS_API int32_t balls_menu_probe_status(BallsSession* session, char* buffer, int32_t size);

typedef struct BallsDevicePickRow {
    char device[32];
    char label[256];
    int32_t optionCount;
    BallsUiOption options[12];
} BallsDevicePickRow;

BALLS_API int32_t balls_menu_device_rows(const BallsPick* picks, int32_t pickCount, BallsDevicePickRow* rows,
                                         int32_t capacity);

BALLS_API int32_t balls_menu_pick_device(const BallsPick* picks, int32_t pickCount, int32_t row, int32_t option,
                                         BallsPick* result, int32_t capacity);

typedef struct BallsDebugRow {
    int32_t kind;
    int32_t tone;
    int32_t colorIndex;
    char label[64];
    char value[192];
} BallsDebugRow;

BALLS_API int32_t balls_menu_debug_rows(BallsSession* session, const BallsFrameTimings* timings, int32_t showBackend,
                                        const char* renderer, BallsDebugRow* rows, int32_t capacity);

enum {
    BALLS_MENU_TEXT_BACKEND_FAILED = 0,
    BALLS_MENU_TEXT_PROBE_RESULT = 1,
    BALLS_MENU_TEXT_COMPUTE_GPU = 2,
};
BALLS_API int32_t balls_menu_message(int32_t which, int32_t kind, uint64_t balls, double fps, const char* text,
                                     char* buffer, int32_t size);
BALLS_API int32_t balls_menu_physics_on(const BallsPick* picks, int32_t pickCount, char* buffer, int32_t size);

typedef struct BallsEditorSession BallsEditorSession;

enum {
    BALLS_TOOL_PAINT = 0,
    BALLS_TOOL_EXTEND = 1,
    BALLS_TOOL_ERASE = 2,
    BALLS_TOOL_PICK = 3,
};

enum {
    BALLS_SHORTCUT_UNDO = 0,
    BALLS_SHORTCUT_REDO = 1,
    BALLS_SHORTCUT_SAVE = 2,
    BALLS_SHORTCUT_PAINT = 3,
    BALLS_SHORTCUT_EXTEND = 4,
    BALLS_SHORTCUT_ERASE = 5,
    BALLS_SHORTCUT_PICK = 6,
    BALLS_SHORTCUT_COLOR_1 = 7,
};

enum {
    BALLS_LAUNCHER_SET = 0,
    BALLS_LAUNCHER_CLEAR = 1,
    BALLS_LAUNCHER_DELETE_ROW = 2,
};

enum {
    BALLS_EDITOR_TEXT_NAME = 0,
    BALLS_EDITOR_TEXT_SELECTION = 1,
    BALLS_EDITOR_TEXT_SAVE_LABEL = 2,
    BALLS_EDITOR_TEXT_BACK_LABEL = 3,
    BALLS_EDITOR_TEXT_FOOTER = 4,
    BALLS_EDITOR_TEXT_MESSAGE = 5,
};

typedef struct BallsEditorState {
    int32_t width;
    int32_t height;
    int32_t playableHeight;
    int32_t launcherColumns;
    int32_t launcherVisibleRows;
    int32_t launcherRows;
    int32_t dirty;
    int32_t canUndo;
    int32_t canRedo;
    int32_t tool;
    int32_t brushColor;
    int32_t brushHits;
    int32_t launcherBalls;
    int32_t selectedBrick;
    int32_t mode;
    int32_t playable;
    int32_t problemCount;
    float messageSecondsLeft;
    int32_t messageIsError;
} BallsEditorState;

BALLS_API BallsEditorSession* balls_editor_session_create(int32_t mode, const char* levelName, const char* normalLevels,
                                                          const char* performanceLevels);
BALLS_API void balls_editor_session_destroy(BallsEditorSession* session);
BALLS_API void balls_editor_session_update(BallsEditorSession* session, float deltaSeconds);
BALLS_API void balls_editor_session_state(const BallsEditorSession* session, BallsEditorState* state);
BALLS_API int32_t balls_editor_session_text(const BallsEditorSession* session, int32_t which, char* buffer,
                                            int32_t size);
BALLS_API int32_t balls_editor_session_problem(const BallsEditorSession* session, int32_t index, char* buffer,
                                               int32_t size);

BALLS_API int32_t balls_editor_session_tally(const BallsEditorSession* session, int32_t color, char* buffer,
                                             int32_t size);
BALLS_API int32_t balls_editor_session_tool_label(int32_t tool, char* buffer, int32_t size);

BALLS_API int32_t balls_editor_session_brick(const BallsEditorSession* session, int32_t column, int32_t row,
                                             int32_t* color, int32_t* hits);
BALLS_API int32_t balls_editor_session_launcher(const BallsEditorSession* session, int32_t column, int32_t row,
                                                int32_t* color, int32_t* balls);

BALLS_API void balls_editor_session_set_tool(BallsEditorSession* session, int32_t tool);
BALLS_API void balls_editor_session_select_color(BallsEditorSession* session, int32_t color);
BALLS_API void balls_editor_session_set_brush_hits(BallsEditorSession* session, int32_t hits);
BALLS_API void balls_editor_session_set_launcher_balls(BallsEditorSession* session, int32_t balls);
BALLS_API void balls_editor_session_begin_stroke(BallsEditorSession* session, int32_t erase);
BALLS_API void balls_editor_session_stroke_cell(BallsEditorSession* session, int32_t column, int32_t row);
BALLS_API void balls_editor_session_end_stroke(BallsEditorSession* session);
BALLS_API void balls_editor_session_click_launcher(BallsEditorSession* session, int32_t column, int32_t row,
                                                   int32_t click);
BALLS_API void balls_editor_session_set_name(BallsEditorSession* session, const char* name);
BALLS_API void balls_editor_session_set_mode(BallsEditorSession* session, int32_t mode);
BALLS_API void balls_editor_session_set_board_size(BallsEditorSession* session, int32_t width, int32_t brickRows,
                                                   int32_t totalRows);
BALLS_API void balls_editor_session_set_launcher_columns(BallsEditorSession* session, int32_t columns);
BALLS_API void balls_editor_session_set_visible_rows(BallsEditorSession* session, int32_t rows);
BALLS_API void balls_editor_session_auto_fill(BallsEditorSession* session);
BALLS_API void balls_editor_session_undo(BallsEditorSession* session);
BALLS_API void balls_editor_session_redo(BallsEditorSession* session);
BALLS_API int32_t balls_editor_session_save(BallsEditorSession* session);

BALLS_API int32_t balls_editor_session_save_and_play(BallsEditorSession* session, BallsLevelInfo* level);

BALLS_API int32_t balls_editor_session_back(BallsEditorSession* session);
BALLS_API int32_t balls_editor_session_shortcut(BallsEditorSession* session, int32_t shortcut);

#ifdef __cplusplus
}
#endif
