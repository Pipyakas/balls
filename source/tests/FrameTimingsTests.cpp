#include "TestFramework.h"

#include "game/FrameTimings.h"

TEST_CASE("frame timings average the last second and keep its worst frame") {
    FrameTimings timings;
    for (int frame = 0; frame < 100; ++frame) {
        timings.record({.frameMs = 10.0, .workMs = 5.0});
    }
    timings.record({.frameMs = 50.0, .workMs = 40.0});
    EXPECT(timings.worstFpsLastSecond() == 20.0);
    EXPECT(timings.worstWorkFpsLastSecond() == 25.0);
    EXPECT(timings.framesPerSecond() < 100.0);
    EXPECT(timings.latest().frameMs == 50.0);
}

TEST_CASE("frame timings forget frames older than a second") {
    FrameTimings timings;
    timings.record({.frameMs = 500.0, .workMs = 500.0});
    for (int frame = 0; frame < 120; ++frame) {
        timings.record({.frameMs = 10.0, .workMs = 4.0, .presented = 0.5});
    }
    EXPECT(timings.framesPerSecond() == 100.0);
    EXPECT(timings.worstFpsLastSecond() == 100.0);
    EXPECT(timings.workFramesPerSecond() == 250.0);
    EXPECT(timings.shownFramesPerSecond() == 50.0);
}
