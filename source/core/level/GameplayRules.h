#pragma once

#include "common/Result.h"

#include <string_view>

// The rules every level plays by. They are read from Gameplay.txt in the levels folder, beside normal/ and
// performance/, so they can be tuned without a rebuild; a key the file leaves out keeps the value below.
struct GameplayRules {
    float ballRadius = 0.25f;
    float ballSpeed = 14.0f;
    int ticksPerSecond = 120;
    int maxTicksPerFrame = 4;
    // The game's rule: after this many bounces a ball heads for the nearest brick of its color. A ball counts up
    // to three bounces, so 1 to 3.
    int retargetBounces = 3;
    float launchHalfAngleDegrees = 55.0f;
    float normalSpawnRate = 60.0f;
};

inline constexpr std::string_view gameplayRulesFileName = "Gameplay.txt";

Result<GameplayRules> parseGameplayRules(std::string_view text);
