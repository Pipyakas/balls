#include "render/Palette.h"

#include <algorithm>
#include <array>

namespace {

constexpr std::array<Color, ballColorCount> ballColors{
    Color{235, 64, 72, 255},  Color{255, 150, 50, 255},  Color{250, 220, 60, 255}, Color{80, 205, 90, 255},
    Color{60, 150, 245, 255}, Color{100, 90, 220, 255}, Color{190, 90, 230, 255},
};

unsigned char scaleChannel(unsigned char channel, float factor) {
    return static_cast<unsigned char>(std::clamp(channel * factor, 0.0f, 255.0f));
}

}

namespace palette {

Color ballColor(BallColor color) { return ballColors[toIndex(color)]; }

Color ballColor(int colorIndex) { return ballColors[std::clamp(colorIndex, 0, ballColorCount - 1)]; }

Color brickColor(BallColor color) { return shade(ballColor(color), 0.92f); }

Color shade(Color color, float factor) {
    return {scaleChannel(color.r, factor), scaleChannel(color.g, factor), scaleChannel(color.b, factor), color.a};
}

}
