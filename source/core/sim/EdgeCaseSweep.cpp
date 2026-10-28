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
    const float columns = static_cast<float>(simulation.board().columns());
    const float rows = static_cast<float>(simulation.board().rows());
    const float cellPixels = std::min(settings.screenWidth / columns, settings.screenHeight / rows);
    const float slotSize = visibilitySlotSize(simulation.tuning().ballRadius, cellPixels);
    simulation.backend().setVisibilityGrid(visibilitySlots(columns, slotSize), visibilitySlots(rows, slotSize));
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
    result.filledBalls = result.ballsAtBudget;
    if (simulation.backend().faultMessage().empty()) {
        const BallCount held = simulation.liveBallCount();
        const auto settledFrame = [&] {
            const Clock::time_point frameStart = Clock::now();
            const BallCount live = simulation.liveBallCount();
            const BallCount replaced = std::min(pacer.allowance(), held > live ? held - live : 0);
            simulation.launchBalls(ballColorFromIndex(colorIndex++ % ballColorCount), replaced);
            simulation.flushPendingSpawns();
            pacer.recordSpawn(replaced, secondsSince(frameStart));
            simulation.tick(settings.ticksPerFrame);
            const double frameSeconds = secondsSince(frameStart);
            pacer.reportFrame(frameSeconds, simulation.liveBallCount());
            return frameSeconds;
        };
        const double crossingSeconds = static_cast<double>(simulation.board().rows()) / simulation.tuning().ballSpeed;
        const int settleTicks = static_cast<int>(crossingSeconds * settings.settleTravels / simulation.tuning().tickSeconds);
        const Clock::time_point settleStart = Clock::now();
        for (int ticks = 0; ticks < settleTicks && secondsSince(settleStart) < settings.longestSettleSeconds;
             ticks += settings.ticksPerFrame) {
            settledFrame();
        }
        double settledSeconds = 0.0;
        double settledBalls = 0.0;
        for (int frame = 0; frame < settings.framesPerWindow; ++frame) {
            settledBalls += static_cast<double>(simulation.liveBallCount());
            settledSeconds += settledFrame();
        }
        const double averageFrame = settledSeconds / settings.framesPerWindow;
        result.settledFrameMs = averageFrame * 1e3;
        result.ballsAtBudget = settledBalls / settings.framesPerWindow * std::min(1.0, budgetSeconds / averageFrame);
    }
    result.launchNanosecondsPerBall =
        result.launchedBalls > 0 ? launchSeconds * 1e9 / static_cast<double>(result.launchedBalls) : 0.0;
    result.aliveBricks = simulation.board().aliveBrickCount();
    result.seconds = secondsSince(start);
    return result;
}
