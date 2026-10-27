#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"

struct FSlateBrush;

// Code-only Slate styling in the game's palette, so the project needs no style assets.
namespace BallsStyle {

// Text from this size up uses SemiBold when bold is requested; smaller text stays Regular.
inline constexpr float StrongTextMinimumSize = 24.0f;

FSlateFontInfo Font(float Size, bool bBold = false);
const FButtonStyle& Button();
// The selected option of a choice: accent fill.
const FButtonStyle& SelectedButton();
// White box meant to be tinted; Radius is nonzero only for decorative balls (UI is square).
const FSlateBrush* RoundedBox(float Radius);
const FSlateBrush* Panel();
const FSlateBrush* White();

// Pixel width and height of Text in Font, for centering text in OnPaint.
FVector2D Measure(const FString& Text, const FSlateFontInfo& FontInfo);

}
