#include "sim/PhysicsAccuracy.h"

#include "common/BallColor.h"
#include "sim/ReferenceBackend.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace {

// Gaps in counts are taken against at least this many, so a curve near zero does not turn one ball into 100%.
constexpr double countSlack = 50.0;
constexpr int visibilitySlotsCap = 4096;

void destroyReference(PhysicsBackend* backend) { delete backend; }

double gap(double value, double reference) { return std::fabs(value - reference) / std::max(reference, countSlack); }

AccuracySample sample(Simulation& simulation, int frame) {
    AccuracySample result;
    result.frame = frame;
    result.bricks = simulation.board().aliveBrickCount();
    result.hits = simulation.stats().totalHits;
    const std::vector<BallSpawn> balls = simulation.backend().exportBalls();
    result.balls = balls.size();
    const float radius = simulation.tuning().ballRadius;
    const float speed = simulation.tuning().ballSpeed;
    const float width = static_cast<float>(simulation.board().columns());
    const float height = static_cast<float>(simulation.board().rows());
    constexpr float edgeTolerance = 1e-3f;
    for (const BallSpawn& ball : balls) {
        if (ball.x < radius - edgeTolerance || ball.x > width - radius + edgeTolerance || ball.y < radius - edgeTolerance ||
            ball.y > height - radius + edgeTolerance) {
            ++result.outsideField;
        }
        if (simulation.board().brickAt(static_cast<int>(std::floor(ball.x)), static_cast<int>(std::floor(ball.y))) >= 0) {
            ++result.insideBricks;
        }
        result.worstSpeedError = std::max(result.worstSpeedError,
                                          static_cast<double>(std::fabs(std::hypot(ball.vx, ball.vy) - speed) / speed));
    }
    return result;
}

}

BackendHandle createReferenceBackend(double longestSubStep) {
    return BackendHandle(new ReferenceBackend(longestSubStep), BackendRelease{destroyReference, nullptr});
}

AccuracyRun runAccuracyScenario(BackendHandle backend, const AccuracyScenario& scenario) {
    AccuracyRun run;
    Simulation simulation(std::move(backend));
    simulation.load(scenario.level);
    const float slotSize = simulation.tuning().ballRadius * visibilitySlotsPerRadius;
    simulation.backend().setDrawing(scenario.drawing);
    simulation.setTicksPerSecond(scenario.ticksPerSecond);
    const int ticksPerFrame = std::max(1, scenario.ticksPerFrame * scenario.ticksPerSecond / defaultTicksPerSecond);
    if (scenario.visibilityGrid) {
        simulation.backend().setVisibilityGrid(
            std::min(visibilitySlots(static_cast<float>(simulation.board().columns()), slotSize), visibilitySlotsCap),
            std::min(visibilitySlots(static_cast<float>(simulation.board().rows()), slotSize), visibilitySlotsCap));
    }
    const BallCount perColor = static_cast<BallCount>(scenario.balls / ballColorCount);
    for (const BallColor color : allBallColors) {
        if (scenario.launch) {
            simulation.launchBalls(color, perColor);
        } else {
            simulation.scatterBalls(color, perColor);
        }
    }
    for (int frame = 0; frame <= scenario.frames; ++frame) {
        if (frame % scenario.sampleEveryFrames == 0 || frame == scenario.frames) {
            run.samples.push_back(sample(simulation, frame));
        }
        if (const std::string_view fault = simulation.backend().faultMessage(); !fault.empty()) {
            run.fault = std::string(fault);
            return run;
        }
        if (frame < scenario.frames) {
            simulation.tick(ticksPerFrame);
        }
    }
    run.finalBalls = simulation.backend().exportBalls();
    return run;
}

AccuracyReport compareToReference(const AccuracyRun& run, const AccuracyRun& reference) {
    AccuracyReport report;
    for (size_t i = 0; i < std::min(run.samples.size(), reference.samples.size()); ++i) {
        const AccuracySample& ours = run.samples[i];
        const AccuracySample& theirs = reference.samples[i];
        report.worstBrickGap = std::max(report.worstBrickGap, gap(ours.bricks, theirs.bricks));
        report.worstBallGap =
            std::max(report.worstBallGap, gap(static_cast<double>(ours.balls), static_cast<double>(theirs.balls)));
        report.worstHitGap =
            std::max(report.worstHitGap, gap(static_cast<double>(ours.hits), static_cast<double>(theirs.hits)));
        report.worstOutsideField = std::max(report.worstOutsideField, ours.outsideField);
        report.worstInsideBricks = std::max(report.worstInsideBricks, ours.insideBricks);
        report.worstSpeedError = std::max(report.worstSpeedError, ours.worstSpeedError);
    }
    const bool sameBalls = !run.samples.empty() && !reference.samples.empty() &&
                           run.finalBalls.size() == reference.finalBalls.size() &&
                           run.samples.back().hits == 0 && reference.samples.back().hits == 0;
    if (sameBalls && !run.finalBalls.empty()) {
        std::vector<double> errors;
        errors.reserve(run.finalBalls.size());
        for (size_t i = 0; i < run.finalBalls.size(); ++i) {
            errors.push_back(std::hypot(run.finalBalls[i].x - reference.finalBalls[i].x,
                                        run.finalBalls[i].y - reference.finalBalls[i].y));
        }
        std::sort(errors.begin(), errors.end());
        double total = 0.0;
        for (const double error : errors) {
            total += error;
        }
        report.comparedPositions = true;
        report.meanPositionError = total / static_cast<double>(errors.size());
        report.p99PositionError = errors[std::min(errors.size() - 1, errors.size() * 99 / 100)];
        report.maxPositionError = errors.back();
    }
    return report;
}

std::string describeAccuracy(std::string_view backend, const AccuracyReport& report) {
    std::string text = std::format("{:<9} bricks {:5.1f}%  balls {:5.1f}%  hits {:5.1f}%  outside {}  in-brick {}  speed {:.4f}",
                                   backend, report.worstBrickGap * 100.0, report.worstBallGap * 100.0,
                                   report.worstHitGap * 100.0, report.worstOutsideField, report.worstInsideBricks,
                                   report.worstSpeedError);
    if (report.comparedPositions) {
        text += std::format("  position mean {:.4f} p99 {:.4f} max {:.4f}", report.meanPositionError,
                            report.p99PositionError, report.maxPositionError);
    }
    return text;
}
