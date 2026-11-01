#include "UI/BallsStyle.h"

#include "BallsPalette.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace BallsStyle {

namespace {

constexpr float LineHeightPerEm = 1.3f;

FString FontFile(const TCHAR* Name) {
    FString Path = FString(UTF8_TO_TCHAR(BALLS_ASSET_DIR)) / TEXT("fonts") / Name;
    if (!IFileManager::Get().FileExists(*Path)) {
        Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("assets/fonts") / Name);
    }
    return Path;
}

TSharedPtr<const FCompositeFont> CompositeFont(const FString& File) {
    static TMap<FString, TSharedPtr<const FCompositeFont>> Composites;
    if (const TSharedPtr<const FCompositeFont>* Found = Composites.Find(File)) {
        return *Found;
    }
    TSharedPtr<const FCompositeFont> Composite =
        MakeShared<FStandaloneCompositeFont>(NAME_None, File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
    Composites.Add(File, Composite);
    return Composite;
}

}

FSlateFontInfo Font(float Size, bool bBold) {

    static TMap<int32, FSlateFontInfo> Cache;

    const float Points = FMath::Max(1.0f, FMath::RoundToFloat(Size * 0.75f / LineHeightPerEm * 4.0f) / 4.0f);
    const bool bStrong = bBold && Size >= StrongTextMinimumSize;
    const int32 Key = FMath::RoundToInt(Points * 4.0f) * 4 + (bStrong ? 2 : 0) + (bBold ? 1 : 0);
    if (const FSlateFontInfo* Found = Cache.Find(Key)) {
        return *Found;
    }
    static const FString Regular = FontFile(TEXT("IBMPlexMono-Regular.ttf"));
    static const FString Strong = FontFile(TEXT("IBMPlexMono-SemiBold.ttf"));

    const FString& File = bStrong ? Strong : Regular;
    const FSlateFontInfo Built = IFileManager::Get().FileExists(*File)
                                     ? FSlateFontInfo(CompositeFont(File), Points)
                                     : FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Points);
    Cache.Add(Key, Built);
    return Built;
}

const FButtonStyle& Button() {
    static const FButtonStyle Style = [] {
        auto Fill = [](const FColor& Color) {
            return FSlateRoundedBoxBrush(BallsPalette::Linear(Color), 0.0f, BallsPalette::Linear(BallsPalette::Border), 1.0f);
        };
        FButtonStyle Result;
        Result.SetNormal(Fill(BallsPalette::Control));
        Result.SetHovered(Fill(BallsPalette::ControlHover));
        Result.SetPressed(Fill(BallsPalette::ControlPressed));
        Result.SetDisabled(Fill(BallsPalette::Control));
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

const FButtonStyle& HeaderButton() {
    static const FButtonStyle Style = [] {
        auto Fill = [](const FColor& Color) { return FSlateRoundedBoxBrush(BallsPalette::Linear(Color), 0.0f); };
        FButtonStyle Result;
        Result.SetNormal(FSlateNoResource());
        Result.SetHovered(Fill(BallsPalette::ControlHover));
        Result.SetPressed(Fill(BallsPalette::ControlPressed));
        Result.SetDisabled(FSlateNoResource());
        Result.SetNormalPadding(FMargin(0.0f));
        Result.SetPressedPadding(FMargin(0.0f));
        return Result;
    }();
    return Style;
}

const FButtonStyle& DropdownButton() {

    static const FButtonStyle Style = [] {
        FButtonStyle Result;
        Result.SetNormal(FSlateNoResource());
        Result.SetHovered(FSlateNoResource());
        Result.SetPressed(FSlateNoResource());
        Result.SetDisabled(FSlateNoResource());
        Result.SetNormalPadding(FMargin(0.0f));
        Result.SetPressedPadding(FMargin(0.0f));
        return Result;
    }();
    return Style;
}

const FButtonStyle& SelectedButton() {
    static const FButtonStyle Style = [] {
        auto Fill = [](const FColor& Color) {
            return FSlateRoundedBoxBrush(BallsPalette::Linear(Color), 0.0f, BallsPalette::Linear(BallsPalette::Border), 1.0f);
        };
        FButtonStyle Result;
        Result.SetNormal(Fill(BallsPalette::Accent));
        Result.SetHovered(Fill(BallsPalette::Shade(BallsPalette::Accent, 1.12f)));
        Result.SetPressed(Fill(BallsPalette::Shade(BallsPalette::Accent, 0.88f)));
        Result.SetDisabled(Fill(BallsPalette::Accent));
        const FLinearColor Text = BallsPalette::Linear(BallsPalette::Background);
        Result.SetNormalForeground(Text);
        Result.SetHoveredForeground(Text);
        Result.SetPressedForeground(Text);
        Result.SetDisabledForeground(Text);
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
    static const FSlateRoundedBoxBrush Brush(BallsPalette::Linear(BallsPalette::Panel), 0.0f, BallsPalette::Linear(BallsPalette::Border), 1.0f);
    return &Brush;
}

const FSlateBrush* White() {
    static const FSlateColorBrush Brush(FLinearColor::White);
    return &Brush;
}

FVector2D Measure(const FString& Text, const FSlateFontInfo& FontInfo) {

    static TMap<TPair<FString, int32>, FVector2D> Cache;
    static TMap<TPair<FString, int32>, FVector2D> Previous;
    const TPair<FString, int32> Key(Text, FontInfo.Size);
    if (const FVector2D* Found = Cache.Find(Key)) {
        return *Found;
    }
    if (const FVector2D* Found = Previous.Find(Key)) {
        const FVector2D Size = *Found;
        Cache.Add(Key, Size);
        return Size;
    }
    const FVector2D Size =
        FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, FontInfo);
    if (Cache.Num() > 2048) {
        Previous = MoveTemp(Cache);
        Cache.Reset();
    }
    Cache.Add(Key, Size);
    return Size;
}

}
