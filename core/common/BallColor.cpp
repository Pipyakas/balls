#include "common/BallColor.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr std::array<std::string_view, ballColorCount> colorNames{
    "Red", "Orange", "Yellow", "Green", "Blue", "Indigo", "Violet",
};

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char lhs, char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs)) ==
                      std::tolower(static_cast<unsigned char>(rhs));
           });
}

}

std::string_view ballColorName(BallColor color) { return colorNames[toIndex(color)]; }

std::optional<BallColor> parseBallColor(std::string_view name) {
    for (int i = 0; i < ballColorCount; ++i) {
        if (equalsIgnoreCase(colorNames[i], name)) {
            return ballColorFromIndex(i);
        }
    }
    return std::nullopt;
}
