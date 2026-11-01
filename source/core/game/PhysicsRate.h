#pragma once

#include <algorithm>
#include <chrono>

class PhysicsRate {
public:
    using Clock = std::chrono::steady_clock;

    void frame(Clock::time_point start, Clock::time_point end, int ticks) {
        lastSeconds = std::chrono::duration<double>(end - start).count();
        longestSeconds = std::max(longestSeconds, lastSeconds);
        slowFrames += lastSeconds > slowFrameSeconds ? 1 : 0;
        ++framesInWindow;
        ticksInWindow += ticks;
        if (const double windowSeconds = std::chrono::duration<double>(end - windowStart).count();
            windowSeconds >= windowLengthSeconds) {
            perSecond = static_cast<double>(framesInWindow) / windowSeconds;
            ticksPerSecondValue = static_cast<double>(ticksInWindow) / windowSeconds;
            framesInWindow = 0;
            ticksInWindow = 0;
            windowStart = end;
        }
    }

    double lastFrameSeconds() const { return lastSeconds; }
    double lastFrameMs() const { return lastSeconds * millisecondsPerSecond; }
    double framesPerSecond() const { return perSecond; }
    double ticksPerSecond() const { return ticksPerSecondValue; }

    double longestFrameMs() const { return longestSeconds * millisecondsPerSecond; }
    long long slowFrameCount() const { return slowFrames; }

private:
    static constexpr double windowLengthSeconds = 0.5;
    static constexpr double millisecondsPerSecond = 1000.0;
    static constexpr double slowFrameSeconds = 1.0 / 60.0;

    Clock::time_point windowStart = Clock::now();
    int framesInWindow = 0;
    long long ticksInWindow = 0;
    double perSecond = 0.0;
    double ticksPerSecondValue = 0.0;
    double lastSeconds = 0.0;
    double longestSeconds = 0.0;
    long long slowFrames = 0;
};
