#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"

struct FSlateBrush;

// Code-only Slate styling in the raylib front end's palette, so the project needs no style assets.
namespace BallsStyle {

FSlateFontInfo Font(float Size, bool bBold = false);
const FButtonStyle& Button();
// Rounded white box meant to be tinted, with corner radii in Slate units.
const FSlateBrush* RoundedBox(float Radius);
const FSlateBrush* Panel();
const FSlateBrush* White();

// Pixel width and height of Text in Font, for centering text in OnPaint.
FVector2D Measure(const FString& Text, const FSlateFontInfo& FontInfo);

}
