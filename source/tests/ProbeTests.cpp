#include "TestFramework.h"

#include "game/CapacityProbe.h"

#include <algorithm>

namespace {

constexpr BallCount cliffBalls = 2'000'000;
constexpr double fastFrame = 1.0 / 75.0;
constexpr double slowFrame = 1.0 / 45.0;
constexpr double freshBallDiscountSeconds = 0.8;
constexpr double freshBallDiscount = 1.15;
constexpr int frameBudget = 60 * 600;

constexpr double workSecondsPerBall = (1.0 / 60.0) / static_cast<double>(cliffBalls);

struct MachineModel {
    BallCount liveBalls = 0;
    double secondsSinceChange = 0.0;

    double work() const { return static_cast<double>(liveBalls) * workSecondsPerBall; }

    double nextFrame() {
        const bool fresh = secondsSinceChange < freshBallDiscountSeconds;
        const double effectiveCliff = static_cast<double>(cliffBalls) * (fresh ? freshBallDiscount : 1.0);
        const double frame = static_cast<double>(liveBalls) < effectiveCliff ? fastFrame : slowFrame;
        secondsSinceChange += frame;
        return frame;
    }

    void apply(BallCount addition, std::optional<BallCount> trim) {
        if (addition > 0) {
            liveBalls += addition;
            secondsSinceChange = 0.0;
        }
        if (trim) {
            liveBalls = std::min(liveBalls, *trim);
            secondsSinceChange = 0.0;
        }
    }
};

void runProbe(CapacityProbe& probe, MachineModel& machine) {
    probe.start();
    for (int frames = 0; frames < frameBudget && probe.isActive(); ++frames) {
        const double frame = machine.nextFrame();
        const BallCount addition = probe.update(frame, machine.work(), machine.liveBalls);
        machine.apply(addition, probe.takeTrimRequest());
    }
}

}

TEST_CASE("probe settles on a count that holds 60 fps in steady state") {
    CapacityProbe probe;
    MachineModel machine;
    runProbe(probe, machine);
    EXPECT(probe.phase() == ProbePhase::Done);
    EXPECT(probe.bestPassingCount() < cliffBalls);
    EXPECT(probe.bestPassingCount() * 100 >= cliffBalls * 90);
    EXPECT(machine.liveBalls == probe.bestPassingCount());
}

TEST_CASE("probe backs off when fresh balls made a window look better than steady state") {
    CapacityProbe probe;
    MachineModel machine;
    runProbe(probe, machine);
    EXPECT(probe.phase() == ProbePhase::Done);
    EXPECT(machine.liveBalls < cliffBalls);
    EXPECT(probe.lastFps() >= probe.targetFps() * probePassTolerance);
}

TEST_CASE("probe reaches the cliff in a handful of jumps") {
    CapacityProbe probe;
    MachineModel machine;
    probe.start();
    int additions = 0;
    for (int frames = 0; frames < frameBudget && probe.isActive(); ++frames) {
        const double frame = machine.nextFrame();
        const BallCount addition = probe.update(frame, machine.work(), machine.liveBalls);
        additions += addition > 0 ? 1 : 0;
        machine.apply(addition, probe.takeTrimRequest());
    }
    EXPECT(probe.phase() == ProbePhase::Done);
    EXPECT(additions <= 12);
}

TEST_CASE("cancel returns the probe to idle") {
    CapacityProbe probe;
    probe.start();
    const BallCount liveBalls = probe.update(fastFrame, 0.0, 0);
    EXPECT(probe.isActive());
    probe.cancel();
    EXPECT(probe.phase() == ProbePhase::Idle);
    EXPECT(!probe.isActive());
    EXPECT(probe.update(fastFrame, 0.0, liveBalls) == 0);
    EXPECT(!probe.takeTrimRequest().has_value());
}

TEST_CASE("probe judges an uncapped game by what a 60 fps frame and its ticks would cost") {
    constexpr double drawSeconds = 0.001;
    constexpr double tickSecondsPerBall = 4e-9;
    constexpr double tickRate = 120.0;
    constexpr int maximumTicksPerFrame = 4;
    const double cliff = (1.0 / 60.0 - drawSeconds) / (2.0 * tickSecondsPerBall);
    CapacityProbe probe;
    probe.start(60.0, tickRate);
    BallCount live = 0;
    double tickClock = 0.0;
    for (int frames = 0; frames < 200'000 && probe.isActive(); ++frames) {
        const double tickCost = static_cast<double>(live) * tickSecondsPerBall;
        const int ticks = std::min(maximumTicksPerFrame, static_cast<int>(tickClock * tickRate));
        const double frame = drawSeconds + ticks * tickCost;
        tickClock = std::min(tickClock - ticks / tickRate, maximumTicksPerFrame / tickRate) + frame;
        live += probe.update(frame, frame, live, std::numeric_limits<BallCount>::max(), ticks);
        if (const auto trim = probe.takeTrimRequest()) {
            live = std::min(live, *trim);
        }
    }
    EXPECT(probe.phase() == ProbePhase::Done);
    EXPECT(static_cast<double>(probe.bestPassingCount()) <= cliff);
    EXPECT(static_cast<double>(probe.bestPassingCount()) > cliff * 0.85);
}
