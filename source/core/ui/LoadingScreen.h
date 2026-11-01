#pragma once

#include "common/LoadProgress.h"

#include <string>
#include <vector>

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

inline constexpr size_t loadingScreenMaxLines = 12;

LoadingScreenText loadingScreenText(const std::string& levelName, const LoadProgress* progress);
