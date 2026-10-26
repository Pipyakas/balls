#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

enum class BallColor : int32_t { Red, Orange, Yellow, Green, Blue, Indigo, Violet };

inline constexpr int ballColorCount = 7;

inline constexpr std::array<BallColor, ballColorCount> allBallColors{
    BallColor::Red,  BallColor::Orange, BallColor::Yellow, BallColor::Green,
    BallColor::Blue, BallColor::Indigo, BallColor::Violet,
};

std::string_view ballColorName(BallColor color);
std::optional<BallColor> parseBallColor(std::string_view name);

constexpr int toIndex(BallColor color) { return static_cast<int>(color); }
constexpr BallColor ballColorFromIndex(int index) { return static_cast<BallColor>(index); }
