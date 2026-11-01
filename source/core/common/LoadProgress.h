#pragma once

#include "common/BallCount.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

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
