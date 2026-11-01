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

class ControlGame {
public:
    virtual ~ControlGame() = default;

    virtual GameMode gameMode() const = 0;
    virtual std::string levelName() const = 0;

    virtual std::string levelLoadError() const = 0;
    virtual LevelDiskState diskState() const = 0;

    virtual std::shared_ptr<const SessionView> sessionView() = 0;

    virtual void post(std::function<void(GameSession&)> command) = 0;

    virtual bool isPaused() const = 0;
    virtual void setPaused(bool paused) = 0;

    virtual void restart() = 0;
};

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

    std::string extraFields;

    std::string screenDetails;
};

struct ControlBenchmarkState {
    std::string_view state = "none";
    std::vector<CandidateResult> results;
    std::optional<BenchmarkCandidate> winner;
};

class ControlHost {
public:
    virtual ~ControlHost() = default;

    virtual ControlFrontEndState frontEndState() const = 0;

    virtual ControlGame* game() = 0;
    virtual std::filesystem::path levelDirectory(GameMode mode) const = 0;

    virtual std::optional<ControlResponse> takeScreenshot(const std::string& path) = 0;
    virtual void play(GameMode mode, const LevelFiles& level) = 0;
    virtual void showMainMenu() = 0;

    virtual bool showLevelSelect(GameMode mode) = 0;
    virtual bool openEditor(GameMode mode, const std::optional<LevelFiles>& level) = 0;
    virtual void quit() = 0;

    virtual const Settings& settings() const = 0;

    virtual void applySettings(const Settings& settings, std::optional<BackendKind> defaultBackend) = 0;

    virtual void openSettings() = 0;
    virtual void setVsync(bool enabled) = 0;

    virtual ControlBenchmarkState benchmarkState() const = 0;
    virtual void runBenchmark() = 0;

    virtual bool skipBenchmark() = 0;
    virtual bool continueFromBenchmark() = 0;

    virtual std::optional<ControlResponse> handleOwnRoute(const ControlRequest&) { return std::nullopt; }
};

std::optional<ControlResponse> handleControlRequest(ControlHost& host, const ControlRequest& request);

std::span<const std::string_view> controlApiPaths();

std::string controlSettingsJson(const Settings& settings, BackendKind preferredBackend);
std::string controlBenchmarkJson(const ControlBenchmarkState& benchmark);
std::string controlGameJson(ControlGame& game);
