#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"
#include "Math/Color.h"

// Same colors as the raylib front end (frontends/raylib/render/Palette.*), as sRGB FColors.
namespace BallsPalette {

inline const FColor Background(38, 30, 66);
inline const FColor BackgroundDeep(26, 20, 48);
inline const FColor Panel(56, 44, 96);
inline const FColor PanelLight(78, 62, 130);
inline const FColor Frame(120, 96, 200);
inline const FColor Text(245, 240, 255);
inline const FColor TextMuted(180, 170, 210);
inline const FColor Accent(255, 196, 64);
inline const FColor Danger(235, 80, 90);
inline const FColor Success(90, 210, 120);

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

inline FLinearColor Linear(FColor Color, float Alpha = 1.0f) {
    FLinearColor Result(Color);
    Result.A = Alpha;
    return Result;
}

}
