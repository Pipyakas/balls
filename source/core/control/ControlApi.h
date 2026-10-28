#pragma once

#include "control/ControlProtocol.h"
#include "game/FrameTimings.h"
#include "game/GameMode.h"
#include "game/GameSession.h"
#include "game/SessionView.h"
#include "game/Settings.h"
#include "level/LevelData.h"
#include "level/LevelWatcher.h"
#include "sim/BackendBenchmark.h"
#include "sim/BackendFactory.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The game's HTTP control API ("Launch options and automation"), parsed and answered here for every front end. A
// front end brings the transport (SDL a socket server, Unreal its HTTPServer) and implements ControlHost, which is
// what a request may do to it; this decides the routes, reads the parameters, applies the commands to the session
// and writes the JSON. Everything runs on the thread that owns the screens.

// The game being played, as the control API reaches it.
class ControlGame {
public:
    virtual ~ControlGame() = default;

    virtual GameMode gameMode() const = 0;
    virtual std::string levelName() const = 0;
    // Set while the level could not be loaded and the load-error screen shows instead.
    virtual std::string levelLoadError() const = 0;
    virtual LevelDiskState diskState() const = 0;
    // The session as the UI draws it, or null while the level is still loading.
    virtual std::shared_ptr<const SessionView> sessionView() = 0;
    // Runs a command on the session: at once on the game thread, or at the next physics frame of a SessionRunner.
    virtual void post(std::function<void(GameSession&)> command) = 0;

    virtual bool isPaused() const = 0;
    virtual void setPaused(bool paused) = 0;
    // Restart as the button does: it reloads files that changed on disk, or leaves when they were removed.
    virtual void restart() = 0;
    virtual int capacityProbeTargetFps() const = 0;
    // Remembers the target, as the gadget's stepper does, and starts the probe at it.
    virtual void startCapacityProbe(int targetFps) = 0;
};

// What /state reports about the front end itself.
struct ControlFrontEndState {
    std::string_view frontEnd;
    std::string renderer;
    std::string_view screen;
    int windowWidth = 0;
    int windowHeight = 0;
    const FrameTimings* frames = nullptr;
    BackendKind preferredBackend = BackendKind::Scalar;
    bool vsync = false;
    std::string toast;
    // More fields of this front end's own, as `"name":value` pairs joined by commas, or empty.
    std::string extraFields;
    // The /state "details" of a screen other than gameplay (level select, editor, benchmark), or empty.
    std::string screenDetails;
};

struct ControlBenchmarkState {
    std::string_view state = "none";  // none, running, done, skipped
    std::vector<CandidateResult> results;
    std::optional<BenchmarkCandidate> winner;
};

class ControlHost {
public:
    virtual ~ControlHost() = default;

    virtual ControlFrontEndState frontEndState() const = 0;
    // Null outside gameplay.
    virtual ControlGame* game() = 0;
    virtual std::filesystem::path levelDirectory(GameMode mode) const = 0;

    // Nothing, when the answer has to wait for the frame that writes the file; the front end then answers itself.
    virtual std::optional<ControlResponse> takeScreenshot(const std::string& path) = 0;
    virtual void play(GameMode mode, const LevelFiles& level) = 0;
    virtual void showMainMenu() = 0;
    // False where the front end has no such screen (the route then answers 404).
    virtual bool showLevelSelect(GameMode mode) = 0;
    virtual bool openEditor(GameMode mode, const std::optional<LevelFiles>& level) = 0;
    virtual void quit() = 0;

    virtual const Settings& settings() const = 0;
    // Saves the settings and applies them, the default backend too when one is given, as the Settings page does.
    virtual void applySettings(const Settings& settings, std::optional<BackendKind> defaultBackend) = 0;
    // Opens the Settings page, or the in-game settings overlay during a game.
    virtual void openSettings() = 0;
    virtual void setVsync(bool enabled) = 0;

    virtual ControlBenchmarkState benchmarkState() const = 0;
    virtual void runBenchmark() = 0;
    // False when the benchmark screen is not in a state where the action applies.
    virtual bool skipBenchmark() = 0;
    virtual bool continueFromBenchmark() = 0;

    // Routes only this front end has (renderer and window controls, its own screens); nothing when unknown.
    virtual std::optional<ControlResponse> handleOwnRoute(const ControlRequest&) { return std::nullopt; }
};

// Nothing when the host deferred the answer (a screenshot).
std::optional<ControlResponse> handleControlRequest(ControlHost& host, const ControlRequest& request);

// Every path the core answers, for transports that bind routes one by one.
std::span<const std::string_view> controlApiPaths();

std::string controlSettingsJson(const Settings& settings, BackendKind preferredBackend);
std::string controlBenchmarkJson(const ControlBenchmarkState& benchmark);
std::string controlGameJson(ControlGame& game);
