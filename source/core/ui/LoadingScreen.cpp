#include "ui/LoadingScreen.h"

#include <format>

namespace {

// The running step's trailing dots cycle a few times a second, so the screen moves even while a step reports nothing.
constexpr double dotsPerSecond = 3.0;
constexpr int dotCycle = 4;

std::string stepTime(double seconds) {
    return seconds < 10.0 ? std::format("{:.2f} s", seconds) : std::format("{:.1f} s", seconds);
}

std::string runningDots(double seconds) {
    return std::string(static_cast<size_t>(static_cast<int>(seconds * dotsPerSecond) % dotCycle), '.');
}

}

LoadingScreenText loadingScreenText(const std::string& levelName, const LoadProgress* progress) {
    LoadingScreenText screen{"Loading " + levelName, {}, {}};
    if (!progress) {
        return screen;
    }
    const std::vector<LoadProgress::Step> steps = progress->steps();
    const size_t first = steps.size() > loadingScreenMaxLines ? steps.size() - loadingScreenMaxLines : 0;
    for (size_t index = first; index < steps.size(); ++index) {
        const LoadProgress::Step& step = steps[index];
        std::string text = step.text;
        if (!step.detail.empty()) {
            text += ": " + step.detail;
        }
        if (!step.done) {
            text += runningDots(step.seconds);
        }
        screen.lines.push_back({std::move(text), stepTime(step.seconds), !step.done});
    }
    screen.total = std::format("{} total", stepTime(progress->elapsedSeconds()));
    return screen;
}
