#pragma once

#include "common/LoadProgress.h"

#include <string>
#include <vector>

// What every front end's loading screen shows: the level's name, then each loading step with how long it took, the
// running one last with its live detail and timer, so a long load shows what it is doing rather than a still line.
struct LoadingScreenLine {
    std::string text;
    std::string time;
    bool running = false;
};

struct LoadingScreenText {
    std::string title;
    std::vector<LoadingScreenLine> lines;
    std::string total;
};

// The screen keeps the latest steps when a load has more than fit.
inline constexpr size_t loadingScreenMaxLines = 12;

LoadingScreenText loadingScreenText(const std::string& levelName, const LoadProgress* progress);
