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

namespace {

// A machine where stepping is nearly free (the board is cleared), so the probe asks for its largest jumps, but the
// balls of a jump go in a slice per frame and past a cliff each frame takes most of a second.
constexpr BallCount spillBalls = 2'000'000;
constexpr BallCount drainSlice = 20'000;
constexpr double cheapWork = 0.001;
constexpr double spilledFrame = 0.45;
constexpr double simulatedSeconds = 60.0;

struct SpillingMachine {
    BallCount liveBalls = 0;
    BallCount pending = 0;

    double nextFrame() const { return liveBalls > spillBalls ? spilledFrame : 1.0 / 60.0; }

    void drain() {
        const BallCount batch = std::min(pending, drainSlice);
        liveBalls += batch;
        pending -= batch;
    }
};

}

TEST_CASE("probe drops a jump whose balls collapse the frame rate while they go in") {
    CapacityProbe probe;
    SpillingMachine machine;
    probe.start();
    double seconds = 0.0;
    while (seconds < simulatedSeconds && probe.isActive()) {
        const double frame = machine.nextFrame();
        seconds += frame;
        // As GameSession does: while a jump's balls go in, the probe only watches for a collapse.
        if (machine.pending > 0) {
            if (probe.abandonsJump(frame, machine.liveBalls)) {
                machine.pending = 0;
                if (const auto trim = probe.takeTrimRequest()) {
                    machine.liveBalls = std::min(machine.liveBalls, *trim);
                }
                continue;
            }
            machine.drain();
            if (machine.pending > 0) {
                continue;
            }
        }
        const double work = machine.liveBalls > spillBalls ? spilledFrame : cheapWork;
        machine.pending += probe.update(frame, work, machine.liveBalls);
        if (const auto trim = probe.takeTrimRequest()) {
            machine.liveBalls = std::min(machine.liveBalls, *trim);
        }
    }
    EXPECT(probe.phase() == ProbePhase::Done);
    EXPECT(probe.bestPassingCount() <= spillBalls);
    EXPECT(probe.bestPassingCount() * 100 >= spillBalls * 80);
    EXPECT(machine.nextFrame() < spilledFrame);
}

TEST_CASE("probe finishes when a count it once failed passes later") {
    // Stepping is nearly free (a cleared board), so only a hitch fails a window. After the bisect, counts at and past
    // that failure pass; the probe must confirm there rather than creep on by the minimum addition.
    constexpr double hitchStart = 1.0;
    constexpr double hitchEnd = 6.0;
    constexpr BallCount backendCapacity = 4'000'000'000;
    CapacityProbe probe;
    BallCount liveBalls = 0;
    probe.start();
    double seconds = 0.0;
    int additions = 0;
    for (int frames = 0; frames < frameBudget && probe.isActive(); ++frames) {
        const bool hitch = seconds >= hitchStart && seconds < hitchEnd;
        const double frame = hitch ? slowFrame : fastFrame;
        seconds += frame;
        const BallCount addition =
            probe.update(frame, hitch ? slowFrame : 0.001, liveBalls, backendCapacity - liveBalls);
        additions += addition > 0 ? 1 : 0;
        liveBalls += addition;
        if (const auto trim = probe.takeTrimRequest()) {
            liveBalls = std::min(liveBalls, *trim);
        }
    }
    EXPECT(probe.phase() == ProbePhase::Done);
    EXPECT(additions <= 30);
    EXPECT(liveBalls == probe.bestPassingCount());
}
