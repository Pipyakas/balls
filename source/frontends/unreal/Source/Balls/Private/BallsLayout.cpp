#include "BallsLayout.h"

#include "Framework/Application/SlateApplication.h"

namespace {

LayoutOrientation ForcedOrientation = LayoutOrientation::Auto;
WideScreenAnchor WideScreen = WideScreenAnchor::ScreenEdges;
constexpr float PortraitAspect = 9.0f / 16.0f;
constexpr float LandscapeAspect = 16.0f / 10.0f;

FSlateRect LayoutFrame(float ScreenWidth, float ScreenHeight) {
    const bool bLandscapeScreen = ScreenWidth >= ScreenHeight;
    if (ForcedOrientation == LayoutOrientation::Auto ||
        (ForcedOrientation == LayoutOrientation::Landscape && bLandscapeScreen)) {
        return FSlateRect(0.0f, 0.0f, ScreenWidth, ScreenHeight);
    }
    const float Aspect = ForcedOrientation == LayoutOrientation::Portrait ? PortraitAspect : LandscapeAspect;
    float Width = ScreenWidth;
    float Height = ScreenHeight;
    if (ScreenWidth / FMath::Max(ScreenHeight, 1.0f) > Aspect) {
        Width = ScreenHeight * Aspect;
    } else {
        Height = ScreenWidth / Aspect;
    }
    const float Left = (ScreenWidth - Width) * 0.5f;
    const float Top = (ScreenHeight - Height) * 0.5f;
    return FSlateRect(Left, Top, Left + Width, Top + Height);
}

FMargin SafeMargin(float DpiScale) {
    FMargin Margin(0.0f);
    if (FSlateApplication::IsInitialized()) {
        FSlateApplication::Get().GetSafeZoneSize(Margin, FDeprecateSlateVector2D());
    }
    const float Inverse = 1.0f / FMath::Max(DpiScale, 1e-3f);
    return FMargin(FMath::RoundToFloat(Margin.Left * Inverse), FMath::RoundToFloat(Margin.Top * Inverse),
                   FMath::RoundToFloat(Margin.Right * Inverse), FMath::RoundToFloat(Margin.Bottom * Inverse));
}

}

void SetLayoutOrientation(LayoutOrientation Orientation) { ForcedOrientation = Orientation; }

void SetWideScreenAnchor(WideScreenAnchor Anchor) { WideScreen = Anchor; }

float PortraitLauncherSize(float PanelWidth) {
    constexpr float Share = 0.19f;
    return FMath::Clamp(PanelWidth * Share, 120.0f, 240.0f);
}

FBallsGameplayLayout ComputeGameplayLayout(FVector2D ScreenSize, int32 BoardColumns, int32 BoardRows, float DpiScale,
                                           int32 LauncherRows) {
    FBallsGameplayLayout Layout;
    const float ScreenWidth = static_cast<float>(ScreenSize.X);
    const float ScreenHeight = static_cast<float>(ScreenSize.Y);
    const FSlateRect Frame = LayoutFrame(ScreenWidth, ScreenHeight);

    const FMargin Safe = SafeMargin(DpiScale);
    const float OriginX = Frame.Left + Safe.Left;
    const float OriginY = Frame.Top + Safe.Top;
    const float Width = FMath::Max(1.0f, Frame.GetSize().X - Safe.Left - Safe.Right);
    const float Height = FMath::Max(1.0f, Frame.GetSize().Y - Safe.Top - Safe.Bottom);
    const CoreGameplayLayout Core =
        computeCoreGameplayLayout(Width, Height, BoardColumns, BoardRows, ForcedOrientation, LauncherRows, WideScreen);
    const auto Shift = [OriginX, OriginY](const LayoutRect& Rect) {
        return FSlateRect(OriginX + Rect.x, OriginY + Rect.y, OriginX + Rect.x + Rect.width,
                          OriginY + Rect.y + Rect.height);
    };

    Layout.bPortrait = Core.isPortrait;
    Layout.CellSize = Core.cellSize;
    Layout.Header = Shift(Core.header);
    Layout.MainSection = Shift(Core.main);
    Layout.InteractionSection = Shift(Core.interaction);
    Layout.DebugSection = Shift(Core.debug);
    Layout.LeftGadget = Shift(Core.leftGadget);
    Layout.RightGadget = Shift(Core.rightGadget);
    Layout.TopGadget = Shift(Core.topGadget);
    Layout.BottomGadget = Shift(Core.bottomGadget);
    Layout.Board = Shift(Core.boardRect);
    return Layout;
}
