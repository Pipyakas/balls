#pragma once

#include "common/BallColor.h"

#include <array>
#include <cstdint>

struct ThemeColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

namespace theme {

inline constexpr ThemeColor background{0x16, 0x16, 0x16, 255};
inline constexpr ThemeColor boardBackdrop{0x10, 0x10, 0x10, 255};
inline constexpr ThemeColor panel{0x1d, 0x1d, 0x1d, 255};
inline constexpr ThemeColor control{0x24, 0x24, 0x24, 255};
inline constexpr ThemeColor controlHover{0x30, 0x30, 0x30, 255};
inline constexpr ThemeColor controlPressed{0x3a, 0x3a, 0x3a, 255};

inline constexpr ThemeColor border{255, 255, 255, 26};
inline constexpr ThemeColor text{0xd4, 0xd4, 0xd4, 255};
inline constexpr ThemeColor textStrong{0xff, 0xff, 0xff, 255};
inline constexpr ThemeColor textMuted{0xa3, 0xa3, 0xa3, 255};
inline constexpr ThemeColor textDim{0x70, 0x70, 0x70, 255};
inline constexpr ThemeColor accent{0x81, 0x90, 0xff, 255};
inline constexpr ThemeColor danger{0xeb, 0x50, 0x5a, 255};
inline constexpr ThemeColor success{0x5a, 0xd2, 0x78, 255};

inline constexpr std::array<ThemeColor, ballColorCount> ballColors{
    ThemeColor{235, 64, 72, 255},  ThemeColor{255, 150, 50, 255}, ThemeColor{250, 220, 60, 255},
    ThemeColor{80, 205, 90, 255},  ThemeColor{60, 150, 245, 255}, ThemeColor{100, 90, 220, 255},
    ThemeColor{190, 90, 230, 255},
};

inline constexpr float brickShade = 0.92f;
inline constexpr float brickHitFlash = 1.4f;
inline constexpr float brickHitFlashSeconds = 0.15f;

inline constexpr const char* regularFontFile = "IBMPlexMono-Regular.ttf";
inline constexpr const char* strongFontFile = "IBMPlexMono-SemiBold.ttf";

inline constexpr float displaySize = 72.0f;
inline constexpr float titleSize = 52.0f;
inline constexpr float largeSize = 44.0f;
inline constexpr float headingSize = 40.0f;
inline constexpr float bigSize = 28.0f;
inline constexpr float mediumSize = 26.0f;
inline constexpr float bodySize = 24.0f;
inline constexpr float headerSize = 22.0f;
inline constexpr float labelSize = 20.0f;
inline constexpr float valueSize = 18.0f;
inline constexpr float smallSize = 16.0f;
inline constexpr float tinySize = 15.0f;

inline constexpr float toastSeconds = 3.0f;

inline constexpr float repairToastSeconds = 8.0f;
inline constexpr float toastTextSize = bodySize;

inline constexpr float optionRowHeight = 44.0f;
inline constexpr float optionRowGap = 10.0f;
inline constexpr float advancedHeaderHeight = 40.0f;

ThemeColor ballColor(BallColor color);
ThemeColor ballColor(int colorIndex);
ThemeColor brickColor(BallColor color);
ThemeColor brickColor(int colorIndex);

ThemeColor shade(ThemeColor color, float factor);

}
