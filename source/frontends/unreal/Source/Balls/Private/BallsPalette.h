#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"
#include "Math/Color.h"

namespace BallsPalette {

inline FColor FromTheme(ThemeColor Color) { return FColor(Color.r, Color.g, Color.b, Color.a); }

inline const FColor Background = FromTheme(theme::background);
inline const FColor BoardBackdrop = FromTheme(theme::boardBackdrop);
inline const FColor Panel = FromTheme(theme::panel);
inline const FColor Control = FromTheme(theme::control);
inline const FColor ControlHover = FromTheme(theme::controlHover);
inline const FColor ControlPressed = FromTheme(theme::controlPressed);
inline const FColor Border = FromTheme(theme::border);
inline const FColor Text = FromTheme(theme::text);
inline const FColor TextStrong = FromTheme(theme::textStrong);
inline const FColor TextMuted = FromTheme(theme::textMuted);
inline const FColor TextDim = FromTheme(theme::textDim);
inline const FColor Accent = FromTheme(theme::accent);
inline const FColor Danger = FromTheme(theme::danger);
inline const FColor Success = FromTheme(theme::success);

inline FColor Ball(int32 ColorIndex) { return FromTheme(theme::ballColor(ColorIndex)); }

inline FColor Ball(BallColor Color) { return FromTheme(theme::ballColor(Color)); }

inline FColor Shade(FColor Color, float Factor) {
    return FromTheme(theme::shade(ThemeColor{Color.R, Color.G, Color.B, Color.A}, Factor));
}

inline FColor Brick(BallColor Color) { return FromTheme(theme::brickColor(Color)); }

inline FLinearColor Linear(FColor Color, float Alpha = -1.0f) {
    FLinearColor Result(Color);
    if (Alpha >= 0.0f) {
        Result.A = Alpha;
    }
    return Result;
}

}
