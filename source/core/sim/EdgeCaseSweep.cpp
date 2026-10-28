#include "sim/EdgeCaseSweep.h"

#include "common/BallColor.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <chrono>

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

struct FrameWindow {
    double averageSeconds = 0.0;
    double worstSeconds = 0.0;
    bool faulted = false;
};

FrameWindow runFrames(Simulation& simulation, const EdgeSweepSettings& settings, double seconds) {
    FrameWindow window;
    const Clock::time_point begin = Clock::now();
    int frames = 0;
    while (frames < settings.minimumFrames || secondsSince(begin) < seconds) {
        const Clock::time_point frameStart = Clock::now();
        simulation.tick(settings.ticksPerFrame);
        window.worstSeconds = std::max(window.worstSeconds, secondsSince(frameStart));
        ++frames;
        if (!simulation.backend().faultMessage().empty()) {
            window.faulted = true;
            break;
        }
    }
    window.averageSeconds = secondsSince(begin) / static_cast<double>(std::max(frames, 1));
    return window;
}

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
    double launchSeconds = 0.0;
    BallCount launchedBalls = 0;
    int colorIndex = 0;
    for (BallCount target = settings.firstBalls;; target = static_cast<BallCount>(static_cast<double>(target) * settings.growth)) {
        const BallCount live = simulation.liveBallCount();
        const BallCount wanted = target > live ? target - live : 0;
        const Clock::time_point launchStart = Clock::now();
        for (BallCount remaining = wanted; remaining > 0;) {
            const BallCount batch = std::min(remaining, (wanted + ballColorCount - 1) / ballColorCount);
            simulation.launchBalls(ballColorFromIndex(colorIndex++ % ballColorCount), batch);
            remaining -= batch;
        }
        simulation.flushPendingSpawns();
        launchSeconds += secondsSince(launchStart);
        launchedBalls += wanted;
        const BallCount reached = simulation.liveBallCount();
        const FrameWindow warmup = runFrames(simulation, settings, settings.warmupSeconds);
        const FrameWindow measured = warmup.faulted ? warmup : runFrames(simulation, settings, settings.measureSeconds);
        if (measured.faulted) {
            result.stopReason = std::string("fault: ") + std::string(simulation.backend().faultMessage());
            break;
        }
        if (measured.averageSeconds > budgetSeconds) {
            result.failingBalls = reached;
            result.failingFrameMs = measured.averageSeconds * 1e3;
            result.stopReason = "frame budget";
            break;
        }
        result.passingBalls = reached;
        result.passingFrameMs = measured.averageSeconds * 1e3;
        result.passingWorstFrameMs = measured.worstSeconds * 1e3;
        if (reached < target) {
            result.stopReason = "capacity";
            break;
        }
        if (secondsSince(start) > settings.caseSeconds) {
            result.stopReason = "time";
            break;
        }
    }
    result.estimatedBalls = static_cast<double>(result.passingBalls);
    if (result.failingBalls > result.passingBalls && result.failingFrameMs > result.passingFrameMs) {
        const double budgetMs = budgetSeconds * 1e3;
        const double share = (budgetMs - result.passingFrameMs) / (result.failingFrameMs - result.passingFrameMs);
        result.estimatedBalls += share * static_cast<double>(result.failingBalls - result.passingBalls);
    }
    result.launchNanosecondsPerBall = launchedBalls > 0 ? launchSeconds * 1e9 / static_cast<double>(launchedBalls) : 0.0;
    result.aliveBricks = simulation.board().aliveBrickCount();
    result.seconds = secondsSince(start);
    return result;
}
