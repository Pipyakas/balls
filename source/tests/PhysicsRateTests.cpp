#include "TestFramework.h"

#include "game/PhysicsRate.h"

#include <cmath>

TEST_CASE("physics rate reports the last frame's time and the frames per second over half a second") {
    using namespace std::chrono_literals;
    const PhysicsRate::Clock::time_point origin = PhysicsRate::Clock::now();
    PhysicsRate rate;
    rate.frame(origin, origin + 4ms);
    EXPECT(std::abs(rate.lastFrameMs() - 4.0) < 0.01);
    EXPECT(rate.framesPerSecond() == 0.0);

    // Frames every 10 ms; the rate appears once half a second has passed and reads about 100 per second.
    for (int frame = 1; frame <= 60; ++frame) {
        const PhysicsRate::Clock::time_point start = origin + frame * 10ms;
        rate.frame(start, start + 2ms);
    }
    EXPECT(std::abs(rate.lastFrameMs() - 2.0) < 0.01);
    EXPECT(rate.framesPerSecond() > 90.0 && rate.framesPerSecond() < 110.0);
}
