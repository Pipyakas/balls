#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"
#include "Math/Color.h"

// Same colors as the SDL front end (frontends/sdl/render/Palette.h), as sRGB FColors.
namespace BallsPalette {

// The UI scheme from the design ("Look and feel"): dark neutrals with one accent.
inline const FColor Background(0x16, 0x16, 0x16);
inline const FColor BackgroundDeep(0x10, 0x10, 0x10);
inline const FColor Panel(0x1d, 0x1d, 0x1d);
inline const FColor PanelLight(0x24, 0x24, 0x24);
inline const FColor ControlHover(0x30, 0x30, 0x30);
inline const FColor ControlPressed(0x3a, 0x3a, 0x3a);
// White at 10%: borders and dividers.
inline const FColor Frame(255, 255, 255, 26);
inline const FColor Text(0xd4, 0xd4, 0xd4);
inline const FColor TextStrong(0xff, 0xff, 0xff);
inline const FColor TextMuted(0xa3, 0xa3, 0xa3);
inline const FColor TextDim(0x70, 0x70, 0x70);
inline const FColor Accent(0x81, 0x90, 0xff);
inline const FColor Danger(0xeb, 0x50, 0x5a);
inline const FColor Success(0x5a, 0xd2, 0x78);

inline FColor Ball(int32 ColorIndex) {
    static const FColor Colors[ballColorCount] = {
        FColor(235, 64, 72),  FColor(255, 150, 50), FColor(250, 220, 60), FColor(80, 205, 90),
        FColor(60, 150, 245), FColor(100, 90, 220), FColor(190, 90, 230),
    };
    return Colors[FMath::Clamp(ColorIndex, 0, ballColorCount - 1)];
}

inline FColor Ball(BallColor Color) { return Ball(toIndex(Color)); }

inline FColor Shade(FColor Color, float Factor) {
    auto Scale = [Factor](uint8 Channel) { return static_cast<uint8>(FMath::Clamp(Channel * Factor, 0.0f, 255.0f)); };
    return FColor(Scale(Color.R), Scale(Color.G), Scale(Color.B), Color.A);
}

inline FColor Brick(BallColor Color) { return Shade(Ball(Color), 0.92f); }

// Alpha defaults to the color's own alpha.
inline FLinearColor Linear(FColor Color, float Alpha = -1.0f) {
    FLinearColor Result(Color);
    if (Alpha >= 0.0f) {
        Result.A = Alpha;
    }
    return Result;
}

}
