#pragma once

#include <chrono>

// The debug gadget's Physics ms and Physics FPS: how long the last physics frame took and how many finished per
// second, averaged over half a second. One meter for every way a session runs (its own physics thread, a front end's
// game thread, the Unity plugin), so they all read the same.
class PhysicsRate {
public:
    using Clock = std::chrono::steady_clock;

    // A physics frame that started at start and finished at end.
    void frame(Clock::time_point start, Clock::time_point end) {
        lastSeconds = std::chrono::duration<double>(end - start).count();
        ++framesInWindow;
        if (const double windowSeconds = std::chrono::duration<double>(end - windowStart).count();
            windowSeconds >= windowLengthSeconds) {
            perSecond = static_cast<double>(framesInWindow) / windowSeconds;
            framesInWindow = 0;
            windowStart = end;
        }
    }

    double lastFrameSeconds() const { return lastSeconds; }
    double lastFrameMs() const { return lastSeconds * millisecondsPerSecond; }
    double framesPerSecond() const { return perSecond; }

private:
    static constexpr double windowLengthSeconds = 0.5;
    static constexpr double millisecondsPerSecond = 1000.0;

    Clock::time_point windowStart = Clock::now();
    int framesInWindow = 0;
    double perSecond = 0.0;
    double lastSeconds = 0.0;
};
