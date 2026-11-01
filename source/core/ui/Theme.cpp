#include "ui/Theme.h"

#include <algorithm>

namespace {

uint8_t scaleChannel(uint8_t channel, float factor) {
    return static_cast<uint8_t>(std::clamp(static_cast<float>(channel) * factor, 0.0f, 255.0f));
}

}

namespace theme {

ThemeColor ballColor(BallColor color) { return ballColors[toIndex(color)]; }

ThemeColor ballColor(int colorIndex) { return ballColors[std::clamp(colorIndex, 0, ballColorCount - 1)]; }

ThemeColor brickColor(BallColor color) { return shade(ballColor(color), brickShade); }

ThemeColor brickColor(int colorIndex) { return shade(ballColor(colorIndex), brickShade); }

ThemeColor shade(ThemeColor color, float factor) {
    return {scaleChannel(color.r, factor), scaleChannel(color.g, factor), scaleChannel(color.b, factor), color.a};
}

}
