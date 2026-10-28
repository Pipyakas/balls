#include "sim/EdgeCaseSweep.h"

#include "common/BallColor.h"
#include "game/SpawnPacer.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <chrono>

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

}

EdgeSweepResult sweepEdgeCase(const BrickLayout& layout, BackendKind kind, const EdgeSweepSettings& settings) {
    EdgeSweepResult result;
    const Clock::time_point start = Clock::now();
    BackendHandle backend = createBackend(kind);
    if (!backend) {
        result.stopReason = "unavailable";
        return result;
    }
    Simulation simulation(std::move(backend));
    simulation.load(layout);
    const float slotSize = simulation.tuning().ballRadius * visibilitySlotsPerRadius;
    simulation.backend().setVisibilityGrid(visibilitySlots(static_cast<float>(simulation.board().columns()), slotSize),
                                           visibilitySlots(static_cast<float>(simulation.board().rows()), slotSize));
    const double budgetSeconds = settings.frameBudgetShare / settings.targetFps;
    SpawnPacer pacer;
    double launchSeconds = 0.0;
    double windowSeconds = 0.0;
    double windowBalls = 0.0;
    int windowFrames = 0;
    int colorIndex = 0;
    for (int frame = 0;; ++frame) {
        const Clock::time_point frameStart = Clock::now();
        const BallCount room = simulation.ballRoom();
        const BallCount launched = std::min(pacer.allowance(), room);
        simulation.launchBalls(ballColorFromIndex(colorIndex++ % ballColorCount), launched);
        simulation.flushPendingSpawns();
        const double spawnSeconds = secondsSince(frameStart);
        pacer.recordSpawn(launched, spawnSeconds);
        launchSeconds += spawnSeconds;
        result.launchedBalls += launched;
        simulation.tick(settings.ticksPerFrame);
        const double frameSeconds = secondsSince(frameStart);
        pacer.reportFrame(frameSeconds, simulation.liveBallCount());
        if (!simulation.backend().faultMessage().empty()) {
            result.stopReason = std::string("fault: ") + std::string(simulation.backend().faultMessage());
            break;
        }
        if (frame >= settings.framesBeforeWorstCounts) {
            result.worstFrameMs = std::max(result.worstFrameMs, frameSeconds * 1e3);
        }
        windowSeconds += frameSeconds;
        windowBalls += static_cast<double>(simulation.liveBallCount());
        if (++windowFrames < settings.framesPerWindow) {
            continue;
        }
        const double averageFrame = windowSeconds / windowFrames;
        result.ballsAtBudget = windowBalls / windowFrames;
        result.windowFrameMs = averageFrame * 1e3;
        windowSeconds = 0.0;
        windowBalls = 0.0;
        windowFrames = 0;
        if (averageFrame > budgetSeconds) {
            result.ballsAtBudget *= budgetSeconds / averageFrame;
            result.stopReason = "frame budget";
            break;
        }
        if (room == 0) {
            result.stopReason = "capacity";
            break;
        }
        if (secondsSince(start) > settings.caseSeconds) {
            result.stopReason = "time";
            break;
        }
    }
    result.launchNanosecondsPerBall =
        result.launchedBalls > 0 ? launchSeconds * 1e9 / static_cast<double>(result.launchedBalls) : 0.0;
    result.aliveBricks = simulation.board().aliveBrickCount();
    result.seconds = secondsSince(start);
    return result;
}
