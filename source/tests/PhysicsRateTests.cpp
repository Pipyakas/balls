#include "TestFramework.h"

#include "game/PhysicsRate.h"

#include <cmath>

TEST_CASE("physics rate reports the last frame's time and the frames per second over half a second") {
    using namespace std::chrono_literals;
    const PhysicsRate::Clock::time_point origin = PhysicsRate::Clock::now();
    PhysicsRate rate;
    rate.frame(origin, origin + 4ms, 1);
    EXPECT(std::abs(rate.lastFrameMs() - 4.0) < 0.01);
    EXPECT(rate.framesPerSecond() == 0.0);

    // Frames every 10 ms; the rate appears once half a second has passed and reads about 100 per second.
    for (int frame = 1; frame <= 60; ++frame) {
        const PhysicsRate::Clock::time_point start = origin + frame * 10ms;
        rate.frame(start, start + 2ms, 1);
    }
    EXPECT(std::abs(rate.lastFrameMs() - 2.0) < 0.01);
    EXPECT(rate.framesPerSecond() > 90.0 && rate.framesPerSecond() < 110.0);
}

TEST_CASE("the tick rate counts the simulation ticks each physics frame ran, not the frames") {
    using namespace std::chrono_literals;
    const PhysicsRate::Clock::time_point origin = PhysicsRate::Clock::now();
    PhysicsRate rate;
    // 60 frames a second, each running two ticks: the 120 Hz simulation at the Physics frames setting's 60.
    for (int frame = 0; frame <= 40; ++frame) {
        const PhysicsRate::Clock::time_point start = origin + frame * 16667us;
        rate.frame(start, start + 1ms, 2);
    }
    EXPECT(rate.framesPerSecond() > 55.0 && rate.framesPerSecond() < 65.0);
    EXPECT(rate.ticksPerSecond() > 110.0 && rate.ticksPerSecond() < 130.0);
}
