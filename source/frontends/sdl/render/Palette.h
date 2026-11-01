#pragma once

#include "common/BallColor.h"
#include "ui/Painter.h"
#include "ui/Theme.h"

namespace palette {

constexpr Color toColor(ThemeColor color) { return {color.r, color.g, color.b, color.a}; }

inline constexpr Color background = toColor(theme::background);
inline constexpr Color boardBackdrop = toColor(theme::boardBackdrop);
inline constexpr Color panel = toColor(theme::panel);
inline constexpr Color control = toColor(theme::control);
inline constexpr Color controlHover = toColor(theme::controlHover);
inline constexpr Color controlPressed = toColor(theme::controlPressed);
inline constexpr Color border = toColor(theme::border);
inline constexpr Color text = toColor(theme::text);
inline constexpr Color textStrong = toColor(theme::textStrong);
inline constexpr Color textMuted = toColor(theme::textMuted);
inline constexpr Color textDim = toColor(theme::textDim);
inline constexpr Color accent = toColor(theme::accent);
inline constexpr Color danger = toColor(theme::danger);
inline constexpr Color success = toColor(theme::success);

inline constexpr const char* regularFontFile = theme::regularFontFile;
inline constexpr const char* strongFontFile = theme::strongFontFile;

inline constexpr float strongTextMinimumPixels = 24.0f;

inline Color ballColor(BallColor color) { return toColor(theme::ballColor(color)); }
inline Color ballColor(int colorIndex) { return toColor(theme::ballColor(colorIndex)); }
inline Color brickColor(BallColor color) { return toColor(theme::brickColor(color)); }
inline Color shade(Color color, float factor) {
    const ThemeColor shaded = theme::shade(ThemeColor{color.r, color.g, color.b, color.a}, factor);
    return toColor(shaded);
}

}
