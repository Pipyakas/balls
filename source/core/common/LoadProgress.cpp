#include "common/LoadProgress.h"

namespace {

double secondsBetween(LoadProgress::Clock::time_point start, LoadProgress::Clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

}

void LoadProgress::closeRunningStep(Clock::time_point now) {
    if (!stepList.empty() && !stepList.back().done) {
        stepList.back().seconds = secondsBetween(stepStarted, now);
        stepList.back().done = true;
    }
}

void LoadProgress::begin(std::string text) {
    std::lock_guard lock(mutex);
    const Clock::time_point now = Clock::now();
    closeRunningStep(now);
    stepList.push_back({std::move(text), {}, 0.0, false});
    stepStarted = now;
}

void LoadProgress::setDetail(std::string detail) {
    std::lock_guard lock(mutex);
    if (!stepList.empty() && !stepList.back().done) {
        stepList.back().detail = std::move(detail);
    }
}

void LoadProgress::finish() {
    std::lock_guard lock(mutex);
    const Clock::time_point now = Clock::now();
    closeRunningStep(now);
    finished = true;
    finishedAt = now;
}

std::vector<LoadProgress::Step> LoadProgress::steps() const {
    std::lock_guard lock(mutex);
    std::vector<Step> copy = stepList;
    if (!copy.empty() && !copy.back().done) {
        copy.back().seconds = secondsBetween(stepStarted, Clock::now());
    }
    return copy;
}

double LoadProgress::elapsedSeconds() const {
    std::lock_guard lock(mutex);
    return secondsBetween(started, finished ? finishedAt : Clock::now());
}

bool LoadProgress::isFinished() const {
    std::lock_guard lock(mutex);
    return finished;
}
