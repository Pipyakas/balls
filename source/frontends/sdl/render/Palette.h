#pragma once

#include "common/BallColor.h"

#include "app/Input.h"
#include "ui/Painter.h"

namespace palette {

inline constexpr Color background{0x16, 0x16, 0x16, 255};
inline constexpr Color backgroundDeep{0x10, 0x10, 0x10, 255};
inline constexpr Color panel{0x1d, 0x1d, 0x1d, 255};
inline constexpr Color panelLight{0x24, 0x24, 0x24, 255};
inline constexpr Color controlHover{0x30, 0x30, 0x30, 255};
inline constexpr Color controlPressed{0x3a, 0x3a, 0x3a, 255};
inline constexpr Color frame{255, 255, 255, 26};
inline constexpr Color text{0xd4, 0xd4, 0xd4, 255};
inline constexpr Color textStrong{0xff, 0xff, 0xff, 255};
inline constexpr Color textMuted{0xa3, 0xa3, 0xa3, 255};
inline constexpr Color textDim{0x70, 0x70, 0x70, 255};
inline constexpr Color accent{0x81, 0x90, 0xff, 255};
inline constexpr Color danger{0xeb, 0x50, 0x5a, 255};
inline constexpr Color success{0x5a, 0xd2, 0x78, 255};

inline constexpr const char* regularFontFile = "IBMPlexMono-Regular.ttf";
inline constexpr const char* strongFontFile = "IBMPlexMono-SemiBold.ttf";
inline constexpr float strongTextMinimumPixels = 24.0f;

Color ballColor(BallColor color);
Color ballColor(int colorIndex);
Color brickColor(BallColor color);
Color shade(Color color, float factor);

}
