#pragma once

#include "common/BallColor.h"

#include <raylib.h>

namespace palette {

inline constexpr Color background{38, 30, 66, 255};
inline constexpr Color backgroundDeep{26, 20, 48, 255};
inline constexpr Color panel{56, 44, 96, 255};
inline constexpr Color panelLight{78, 62, 130, 255};
inline constexpr Color frame{120, 96, 200, 255};
inline constexpr Color text{245, 240, 255, 255};
inline constexpr Color textMuted{180, 170, 210, 255};
inline constexpr Color accent{255, 196, 64, 255};
inline constexpr Color danger{235, 80, 90, 255};
inline constexpr Color success{90, 210, 120, 255};

Color ballColor(BallColor color);
Color ballColor(int colorIndex);
Color brickColor(BallColor color);
Color shade(Color color, float factor);

}
