#include "sim/EdgeCaseSweep.h"

#include "common/BallColor.h"
#include "game/SpawnPacer.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <chrono>

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

struct SweepRun {
    Simulation& simulation;
    const EdgeSweepSettings& settings;
    EdgeSweepResult& result;
    Clock::time_point start;
    double budgetSeconds;
    SpawnPacer pacer;
    double launchSeconds = 0.0;
    int colorIndex = 0;
    int frames = 0;

    // One game frame: launch up to `wanted` more balls through the pacer, then step.
    double frame(BallCount wanted) {
        const Clock::time_point frameStart = Clock::now();
        const BallCount launched = std::min({pacer.allowance(), simulation.ballRoom(), wanted});
        simulation.launchBalls(ballColorFromIndex(colorIndex++ % ballColorCount), launched);
        simulation.flushPendingSpawns();
        const double spawnSeconds = secondsSince(frameStart);
        pacer.recordSpawn(launched, spawnSeconds);
        launchSeconds += spawnSeconds;
        result.launchedBalls += launched;
        simulation.tick(settings.ticksPerFrame);
        const double frameSeconds = secondsSince(frameStart);
        pacer.reportFrame(frameSeconds, simulation.liveBallCount());
        if (++frames > settings.framesBeforeWorstCounts) {
            result.worstFrameMs = std::max(result.worstFrameMs, frameSeconds * 1e3);
        }
        return frameSeconds;
    }

    bool faulted() {
        if (simulation.backend().faultMessage().empty()) {
            return false;
        }
        result.stopReason = std::string("fault: ") + std::string(simulation.backend().faultMessage());
        return true;
    }

    // Launches as fast as the pacer allows until a window of frames is over budget, the backend is full or time is
    // up; false when the case faulted.
    bool fill() {
        for (;;) {
            double windowSeconds = 0.0;
            double windowBalls = 0.0;
            for (int i = 0; i < settings.framesPerWindow; ++i) {
                windowSeconds += frame(unlimitedBalls);
                windowBalls += static_cast<double>(simulation.liveBallCount());
                if (faulted()) {
                    return false;
                }
            }
            const double averageFrame = windowSeconds / settings.framesPerWindow;
            result.filledBalls = windowBalls / settings.framesPerWindow;
            result.windowFrameMs = averageFrame * 1e3;
            if (simulation.ballRoom() == 0) {
                result.stopReason = "capacity";
                return true;
            }
            if (secondsSince(start) > settings.caseSeconds) {
                result.stopReason = "time";
                return true;
            }
            if (averageFrame > budgetSeconds) {
                result.stopReason = "frame budget";
                return true;
            }
        }
    }

    // Runs one and a half crossings of the board (at most longestSeconds) replacing only the balls that die,
    // then measures a window; returns that window's average frame.
    double settle(double longestSeconds) {
        const BallCount held = simulation.liveBallCount();
        const auto replace = [&] {
            const BallCount live = simulation.liveBallCount();
            return frame(held > live ? held - live : 0);
        };
        const double crossingSeconds = static_cast<double>(simulation.board().rows()) / simulation.tuning().ballSpeed;
        const int settleTicks =
            static_cast<int>(crossingSeconds * settings.settleTravels / simulation.tuning().tickSeconds);
        const Clock::time_point settleStart = Clock::now();
        for (int ticks = 0; ticks < settleTicks && secondsSince(settleStart) < longestSeconds;
             ticks += settings.ticksPerFrame) {
            replace();
        }
        double settledSeconds = 0.0;
        double settledBalls = 0.0;
        for (int i = 0; i < settings.framesPerWindow; ++i) {
            settledBalls += static_cast<double>(simulation.liveBallCount());
            settledSeconds += replace();
        }
        const double averageFrame = settledSeconds / settings.framesPerWindow;
        result.settledFrameMs = averageFrame * 1e3;
        result.ballsAtBudget = settledBalls / settings.framesPerWindow * std::min(1.0, budgetSeconds / averageFrame);
        return averageFrame;
    }
};

}

void useGameVisibilityGrid(Simulation& simulation, const EdgeSweepSettings& settings) {
    const float columns = static_cast<float>(simulation.board().columns());
    const float rows = static_cast<float>(simulation.board().rows());
    const float cellPixels = std::min(settings.screenWidth / columns, settings.screenHeight / rows);
    const float slotSize = visibilitySlotSize(simulation.tuning().ballRadius, cellPixels);
    simulation.backend().setVisibilityGrid(visibilitySlots(columns, slotSize), visibilitySlots(rows, slotSize));
}

// Launching costs a frame more than the balls it adds will once they fly, so a fill can stop over budget while the
// settled board still has room: the case then goes on filling while the settled frame is under the refill share of
// the budget and there is time.
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
    useGameVisibilityGrid(simulation, settings);
    SweepRun run{simulation, settings, result, start, settings.frameBudgetShare / settings.targetFps};
    for (bool first = true; run.fill(); first = false) {
        const double settledFrame = run.settle(first ? settings.longestSettleSeconds : settings.refillSettleSeconds);
        if (run.faulted() || result.stopReason != "frame budget" || secondsSince(start) > settings.caseSeconds ||
            settledFrame > run.budgetSeconds * settings.refillBelowBudget) {
            break;
        }
    }
    result.launchNanosecondsPerBall =
        result.launchedBalls > 0 ? run.launchSeconds * 1e9 / static_cast<double>(result.launchedBalls) : 0.0;
    result.aliveBricks = simulation.board().aliveBrickCount();
    result.seconds = secondsSince(start);
    return result;
}
