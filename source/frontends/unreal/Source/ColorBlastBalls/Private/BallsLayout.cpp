#include "BallsLayout.h"

#include "Framework/Application/SlateApplication.h"

namespace {

// The game's "Gameplay layout": the board and its gadgets form one tight group, exactly GadgetGap apart, with any
// leftover space going to the outer margins.
constexpr float OuterMargin = 16.0f;
constexpr float GadgetGap = 8.0f;
constexpr float BoardPadding = 10.0f;
constexpr float HeaderWidth = 420.0f;
constexpr float HeaderHeight = 56.0f;
constexpr float DebugWidth = 200.0f;

constexpr float PortraitTopStrip = 72.0f;
constexpr float PortraitBottomShare = 0.30f;
constexpr float PortraitMinimumBottom = 200.0f;
constexpr float PortraitMaximumBottom = 620.0f;
constexpr float PortraitMinimumBoard = 240.0f;
// The compact bottom panel is one row of launcher buttons plus padding.
// The UI is laid out in pixels, so a thumb-sized button on a 1080 px wide phone is about 200 px.
constexpr float PortraitLauncherShare = 0.19f;
constexpr float PortraitLauncherMinimum = 120.0f;
constexpr float PortraitLauncherMaximum = 240.0f;
constexpr float PortraitCompactPadding = 48.0f;

LayoutOrientation ForcedOrientation = LayoutOrientation::Auto;
constexpr float PortraitAspect = 9.0f / 16.0f;
constexpr float LandscapeAspect = 16.0f / 10.0f;

// The part of the screen the layout may use: the whole screen for Auto, else a centered frame of the forced shape.
FSlateRect LayoutFrame(float ScreenWidth, float ScreenHeight) {
    if (ForcedOrientation == LayoutOrientation::Auto) {
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

float BoardCellSize(float AvailableWidth, float AvailableHeight, float Columns, float Rows) {
    return FMath::Max(1.0f, FMath::Min(AvailableWidth / FMath::Max(Columns, 1.0f),
                                       AvailableHeight / FMath::Max(Rows, 1.0f)));
}

// Display cutouts and rounded corners, in Slate units. The platform reports the margin in pixels.
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

float PortraitLauncherSize(float PanelWidth) {
    return FMath::Clamp(PanelWidth * PortraitLauncherShare, PortraitLauncherMinimum, PortraitLauncherMaximum);
}

FBallsGameplayLayout ComputeGameplayLayout(FVector2D ScreenSize, int32 BoardColumns, int32 BoardRows, float DpiScale,
                                           bool bCompactControls) {
    FBallsGameplayLayout Layout;
    const float ScreenWidth = static_cast<float>(ScreenSize.X);
    const float ScreenHeight = static_cast<float>(ScreenSize.Y);
    const float Columns = static_cast<float>(FMath::Max(BoardColumns, 1));
    const float Rows = static_cast<float>(FMath::Max(BoardRows, 1));
    const FSlateRect Frame = LayoutFrame(ScreenWidth, ScreenHeight);
    Layout.bPortrait = Frame.GetSize().Y > Frame.GetSize().X;

    // Everything below is laid out inside the frame and the safe area, so nothing is drawn under a cutout.
    FMargin Safe = SafeMargin(DpiScale);
    Safe.Left += Frame.Left;
    Safe.Top += Frame.Top;
    Safe.Right += ScreenWidth - Frame.Right;
    Safe.Bottom += ScreenHeight - Frame.Bottom;
    const float Left = Safe.Left + OuterMargin;
    const float Top = Safe.Top + OuterMargin;
    const float Width = FMath::Max(1.0f, ScreenWidth - Safe.Left - Safe.Right - 2.0f * OuterMargin);
    const float Height = FMath::Max(1.0f, ScreenHeight - Safe.Top - Safe.Bottom - 2.0f * OuterMargin);

    if (!Layout.bPortrait) {
        // Landscape: left gadget (header + user interaction), board frame, right gadget (debug).
        const float FixedWidth = HeaderWidth + DebugWidth + 2.0f * GadgetGap + 2.0f * BoardPadding;
        const float Cell = BoardCellSize(Width - FixedWidth, Height - 2.0f * BoardPadding, Columns, Rows);
        const float FrameWidth = Cell * Columns + 2.0f * BoardPadding;
        const float FrameHeight = Cell * Rows + 2.0f * BoardPadding;
        const float GroupWidth = HeaderWidth + GadgetGap + FrameWidth + GadgetGap + DebugWidth;
        const float GroupLeft = Left + (Width - GroupWidth) * 0.5f;
        const float FrameLeft = GroupLeft + HeaderWidth + GadgetGap;
        const float FrameTop = Top + (Height - FrameHeight) * 0.5f;

        Layout.CellSize = Cell;
        Layout.MainSection = FSlateRect(FrameLeft, FrameTop, FrameLeft + FrameWidth, FrameTop + FrameHeight);
        Layout.Board = FSlateRect(FrameLeft + BoardPadding, FrameTop + BoardPadding,
                                  FrameLeft + FrameWidth - BoardPadding, FrameTop + FrameHeight - BoardPadding);
        Layout.Header = FSlateRect(GroupLeft, Top, GroupLeft + HeaderWidth, Top + HeaderHeight);
        Layout.InteractionSection = FSlateRect(GroupLeft, Top + HeaderHeight, GroupLeft + HeaderWidth, Top + Height);
        Layout.DebugSection = FSlateRect(FrameLeft + FrameWidth + GadgetGap, Top, GroupLeft + GroupWidth, Top + Height);
    } else {
        // Portrait: top gadget (header and stats strip), board frame, bottom gadget (user interaction).
        const float CompactBottom = PortraitLauncherSize(Width) + PortraitCompactPadding;
        float BottomPanel = bCompactControls
                                ? CompactBottom
                                : FMath::Clamp(Height * PortraitBottomShare, PortraitMinimumBottom, PortraitMaximumBottom);
        const float Chrome = PortraitTopStrip + 2.0f * GadgetGap + 2.0f * BoardPadding;
        if (Height - Chrome - BottomPanel < PortraitMinimumBoard) {
            BottomPanel = FMath::Max(bCompactControls ? CompactBottom : 120.0f, Height - Chrome - PortraitMinimumBoard);
        }
        const float Cell = BoardCellSize(Width - 2.0f * BoardPadding, Height - Chrome - BottomPanel, Columns, Rows);
        const float FrameWidth = Cell * Columns + 2.0f * BoardPadding;
        const float FrameHeight = Cell * Rows + 2.0f * BoardPadding;
        const float GroupHeight = PortraitTopStrip + GadgetGap + FrameHeight + GadgetGap + BottomPanel;
        const float GroupTop = Top + (Height - GroupHeight) * 0.5f;
        const float FrameTop = GroupTop + PortraitTopStrip + GadgetGap;
        const float FrameLeft = Left + (Width - FrameWidth) * 0.5f;

        Layout.CellSize = Cell;
        Layout.MainSection = FSlateRect(FrameLeft, FrameTop, FrameLeft + FrameWidth, FrameTop + FrameHeight);
        Layout.Board = FSlateRect(FrameLeft + BoardPadding, FrameTop + BoardPadding,
                                  FrameLeft + FrameWidth - BoardPadding, FrameTop + FrameHeight - BoardPadding);
        Layout.Header = FSlateRect(Left, GroupTop, Left + Width, GroupTop + PortraitTopStrip);
        Layout.InteractionSection =
            FSlateRect(Left, FrameTop + FrameHeight + GadgetGap, Left + Width, GroupTop + GroupHeight);
        Layout.DebugSection = Layout.Header;
    }

    return Layout;
}
