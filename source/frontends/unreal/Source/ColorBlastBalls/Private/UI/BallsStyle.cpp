#include "UI/BallsStyle.h"

#include "BallsPalette.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace BallsStyle {

namespace {

// Fonts live in the asset directory next to the levels: the repository's data/assets when it exists, else the
// packaged project's assets folder.
FString FontFile(const TCHAR* Name) {
    FString Path = FString(UTF8_TO_TCHAR(BALLS_ASSET_DIR)) / TEXT("fonts") / Name;
    if (!IFileManager::Get().FileExists(*Path)) {
        Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("assets/fonts") / Name);
    }
    return Path;
}

}

FSlateFontInfo Font(float Size, bool bBold) {
    static const FString Regular = FontFile(TEXT("IBMPlexMono-Regular.ttf"));
    static const FString Strong = FontFile(TEXT("IBMPlexMono-SemiBold.ttf"));
    // Slate font sizes are points at 96 DPI; the SDL sizes are pixels. Like SDL, only large text uses SemiBold.
    const int32 Points = FMath::Max(1, FMath::RoundToInt(Size * 0.75f));
    const FString& File = bBold && Size >= StrongTextMinimumSize ? Strong : Regular;
    if (!IFileManager::Get().FileExists(*File)) {
        return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Points);
    }
    return FSlateFontInfo(File, Points);
}

const FButtonStyle& Button() {
    static const FButtonStyle Style = [] {
        auto Fill = [](const FColor& Color) {
            return FSlateRoundedBoxBrush(BallsPalette::Linear(Color), 0.0f, BallsPalette::Linear(BallsPalette::Frame), 1.0f);
        };
        FButtonStyle Result;
        Result.SetNormal(Fill(BallsPalette::PanelLight));
        Result.SetHovered(Fill(BallsPalette::ControlHover));
        Result.SetPressed(Fill(BallsPalette::ControlPressed));
        Result.SetDisabled(Fill(BallsPalette::PanelLight));
        Result.SetNormalForeground(BallsPalette::Linear(BallsPalette::Text));
        Result.SetHoveredForeground(BallsPalette::Linear(BallsPalette::TextStrong));
        Result.SetPressedForeground(BallsPalette::Linear(BallsPalette::TextStrong));
        Result.SetDisabledForeground(BallsPalette::Linear(BallsPalette::TextDim));
        Result.SetNormalPadding(FMargin(8.0f, 4.0f));
        Result.SetPressedPadding(FMargin(8.0f, 4.0f));
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
    static const FSlateRoundedBoxBrush Brush(BallsPalette::Linear(BallsPalette::Panel), 0.0f, BallsPalette::Linear(BallsPalette::Frame), 1.0f);
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
