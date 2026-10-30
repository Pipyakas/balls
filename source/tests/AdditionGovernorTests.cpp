#include "TestFramework.h"

#include "game/AdditionGovernor.h"

namespace {

constexpr double tickSeconds = 1.0 / 120.0;
constexpr double frameSeconds = 1.0 / 60.0;

}

TEST_CASE("the governor adds no balls once the step alone fills the frame budget") {
    AdditionGovernor governor(tickSeconds);
    governor.recordStep(0.008, 1, 100'000'000);
    governor.recordFrame(0.016);
    EXPECT(governor.allowance(100'000'000, true) == 0);
}

TEST_CASE("the governor's allowance keeps the predicted frame within 1/60 s") {
    AdditionGovernor governor(tickSeconds);
    const BallCount live = 10'000'000;
    const double stepPerTick = 0.002;
    governor.recordStep(stepPerTick, 1, live);
    governor.recordAdditions(0.001, 50'000);
    governor.recordFrame(stepPerTick + 0.001 + 0.0005);
    const BallCount allowance = governor.allowance(live, true);
    EXPECT(allowance > 0);
    const double stepPerBall = stepPerTick / static_cast<double>(live);
    const double predicted = 0.0005 + 2 * stepPerBall * static_cast<double>(live + allowance) +
                             0.001 / 50'000.0 * static_cast<double>(allowance);
    EXPECT(predicted < frameSeconds);
    EXPECT(governor.allowance(live, false) < allowance);
}

TEST_CASE("the governor still adds balls when the front end alone draws slower than the budget") {
    AdditionGovernor governor(tickSeconds);
    governor.recordStep(0.0001, 1, 1'000'000);
    governor.recordAdditions(0.0001, 100'000);
    governor.recordFrame(0.020);
    EXPECT(governor.allowance(1'000'000, true) > 0);
    EXPECT(governor.allowance(1'000'000, false) > 0);
}

TEST_CASE("the governor at most doubles the balls in one frame") {
    AdditionGovernor governor(tickSeconds);
    governor.recordStep(0.00001, 1, 1'000'000);
    governor.recordAdditions(0.0001, 1'000'000);
    governor.recordFrame(0.0002);
    EXPECT(governor.allowance(1'000'000, true) == 1'000'000);
}
