#pragma once

#include "common/BallCount.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

// The steps of loading a level, written by the thread that loads it and read by the UI every frame for the loading
// screen. A step runs from its begin() to the next one, and may update its detail (a row count, say) as it goes.
class LoadProgress {
public:
    using Clock = std::chrono::steady_clock;

    struct Step {
        std::string text;
        std::string detail;
        double seconds = 0.0;
        bool done = false;
    };

    void begin(std::string text);
    void setDetail(std::string detail);
    void finish();

    // Every step so far; the running one is timed up to now.
    std::vector<Step> steps() const;
    double elapsedSeconds() const;
    bool isFinished() const;

private:
    void closeRunningStep(Clock::time_point now);

    mutable std::mutex mutex;
    Clock::time_point started = Clock::now();
    Clock::time_point stepStarted = started;
    Clock::time_point finishedAt = started;
    std::vector<Step> stepList;
    bool finished = false;
};

// Loading reports as it goes only when a front end asked for it; these keep the call sites free of null checks.
inline void beginLoadStep(LoadProgress* progress, std::string text) {
    if (progress) {
        progress->begin(std::move(text));
    }
}

inline void setLoadDetail(LoadProgress* progress, std::string detail) {
    if (progress) {
        progress->setDetail(std::move(detail));
    }
}
