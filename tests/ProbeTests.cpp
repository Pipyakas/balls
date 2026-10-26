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

struct MachineModel {
    BallCount liveBalls = 0;
    double secondsSinceChange = 0.0;

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
        const BallCount addition = probe.update(frame, machine.liveBalls);
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
    EXPECT(probe.bestPassingCount() * 4 >= cliffBalls * 3);
    EXPECT(machine.liveBalls == probe.bestPassingCount());
}

TEST_CASE("probe backs off when fresh balls made a window look better than steady state") {
    CapacityProbe probe;
    MachineModel machine;
    runProbe(probe, machine);
    EXPECT(probe.phase() == ProbePhase::Done);
    EXPECT(machine.liveBalls < cliffBalls);
    EXPECT(probe.lastFps() >= probeTargetFps);
}

TEST_CASE("cancel returns the probe to idle") {
    CapacityProbe probe;
    probe.start();
    const BallCount liveBalls = probe.update(fastFrame, 0);
    EXPECT(probe.isActive());
    probe.cancel();
    EXPECT(probe.phase() == ProbePhase::Idle);
    EXPECT(!probe.isActive());
    EXPECT(probe.update(fastFrame, liveBalls) == 0);
    EXPECT(!probe.takeTrimRequest().has_value());
}
