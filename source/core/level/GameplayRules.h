#pragma once

#include "common/Result.h"

#include <string_view>

struct GameplayRules {
    float ballRadius = 0.25f;
    float ballSpeed = 14.0f;
    int ticksPerSecond = 120;
    int maxTicksPerFrame = 4;

    int retargetBounces = 3;
    float launchHalfAngleDegrees = 55.0f;
    float normalSpawnRate = 60.0f;
};

inline constexpr std::string_view gameplayRulesFileName = "Gameplay.txt";

Result<GameplayRules> parseGameplayRules(std::string_view text);
