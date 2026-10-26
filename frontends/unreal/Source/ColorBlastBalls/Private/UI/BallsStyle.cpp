#include "UI/BallsStyle.h"

#include "BallsPalette.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace BallsStyle {

FSlateFontInfo Font(float Size, bool bBold) {
    // Slate font sizes are points at 96 DPI; the raylib sizes are pixels.
    return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::RoundToInt(Size * 0.75f));
}

const FButtonStyle& Button() {
    static const FButtonStyle Style = [] {
        FButtonStyle Result;
        Result.SetNormal(FSlateRoundedBoxBrush(BallsPalette::Linear(BallsPalette::PanelLight), 10.0f));
        Result.SetHovered(FSlateRoundedBoxBrush(BallsPalette::Linear(BallsPalette::Frame), 10.0f));
        Result.SetPressed(FSlateRoundedBoxBrush(BallsPalette::Linear(BallsPalette::Shade(BallsPalette::Frame, 0.8f)), 10.0f));
        Result.SetDisabled(FSlateRoundedBoxBrush(BallsPalette::Linear(BallsPalette::Panel), 10.0f));
        Result.SetNormalForeground(BallsPalette::Linear(BallsPalette::Text));
        Result.SetHoveredForeground(BallsPalette::Linear(BallsPalette::Text));
        Result.SetPressedForeground(BallsPalette::Linear(BallsPalette::Text));
        Result.SetDisabledForeground(BallsPalette::Linear(BallsPalette::TextMuted, 0.6f));
        Result.SetNormalPadding(FMargin(8.0f, 4.0f));
        Result.SetPressedPadding(FMargin(8.0f, 5.0f, 8.0f, 3.0f));
        return Result;
    }();
    return Style;
}

const FSlateBrush* RoundedBox(float Radius) {
    static TMap<int32, TUniquePtr<FSlateRoundedBoxBrush>> Brushes;
    TUniquePtr<FSlateRoundedBoxBrush>& Brush = Brushes.FindOrAdd(FMath::RoundToInt(Radius));
    if (!Brush) {
        Brush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, Radius);
    }
    return Brush.Get();
}

const FSlateBrush* Panel() {
    static const FSlateRoundedBoxBrush Brush(BallsPalette::Linear(BallsPalette::Panel, 0.94f), 12.0f);
    return &Brush;
}

const FSlateBrush* White() {
    static const FSlateColorBrush Brush(FLinearColor::White);
    return &Brush;
}

FVector2D Measure(const FString& Text, const FSlateFontInfo& FontInfo) {
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, FontInfo);
}

}
