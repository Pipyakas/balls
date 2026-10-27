#include "BallsLayout.h"

#include "Framework/Application/SlateApplication.h"

namespace {

constexpr float HeaderHeight = 96.0f;
constexpr float MinimumControlsWidth = 300.0f;
constexpr float MinimumDebugWidth = 300.0f;

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
    Layout.bPortrait = ScreenHeight > ScreenWidth;

    // Everything below is laid out inside the safe area, so nothing is drawn under a cutout.
    const FMargin Safe = SafeMargin(DpiScale);
    const float Left = Safe.Left;
    const float Top = Safe.Top;
    const float Width = FMath::Max(1.0f, ScreenWidth - Safe.Left - Safe.Right);
    const float Height = FMath::Max(1.0f, ScreenHeight - Safe.Top - Safe.Bottom);

    if (!Layout.bPortrait) {
        // Landscape: the play field is a full-height centre column; the panels split the remaining width.
        const float MaxBoardWidth = FMath::Max(1.0f, Width - MinimumControlsWidth - MinimumDebugWidth);
        const float Cell = BoardCellSize(MaxBoardWidth, Height, Columns, Rows);
        const float BoardWidth = Cell * Columns;
        const float BoardHeight = Cell * Rows;
        const float BoardLeft = Left + (Width - BoardWidth) * 0.5f;
        const float BoardTop = Top + (Height - BoardHeight) * 0.5f;
        const float BoardRight = BoardLeft + BoardWidth;

        Layout.CellSize = Cell;
        Layout.Board = FSlateRect(BoardLeft, BoardTop, BoardRight, BoardTop + BoardHeight);
        Layout.Header = FSlateRect(Left, Top, BoardLeft, Top + HeaderHeight);
        Layout.InteractionSection = FSlateRect(Left, Top + HeaderHeight, BoardLeft, Top + Height);
        Layout.DebugSection = FSlateRect(BoardRight, Top, Left + Width, Top + Height);
        Layout.MainSection = FSlateRect(BoardLeft, Top, BoardRight, Top + Height);
    } else {
        // Portrait, matching the original mobile game: one stats/header strip on top, the play field
        // full-width in the middle, and the launcher buttons in a bottom panel.
        const float CompactBottom = PortraitLauncherSize(Width) + PortraitCompactPadding;
        float BottomPanel = bCompactControls
                                ? CompactBottom
                                : FMath::Clamp(Height * PortraitBottomShare, PortraitMinimumBottom, PortraitMaximumBottom);
        if (Height - PortraitTopStrip - BottomPanel < PortraitMinimumBoard) {
            BottomPanel = FMath::Max(bCompactControls ? CompactBottom : 120.0f,
                                     Height - PortraitTopStrip - PortraitMinimumBoard);
        }
        const float MainTop = Top + PortraitTopStrip;
        const float MainBottom = Top + Height - BottomPanel;
        const float Cell = BoardCellSize(Width, MainBottom - MainTop, Columns, Rows);
        const float BoardWidth = Cell * Columns;
        const float BoardHeight = Cell * Rows;
        const float BoardLeft = Left + (Width - BoardWidth) * 0.5f;
        const float BoardTop = MainTop + (MainBottom - MainTop - BoardHeight) * 0.5f;
        const float BoardBottom = BoardTop + BoardHeight;

        Layout.CellSize = Cell;
        Layout.Board = FSlateRect(BoardLeft, BoardTop, BoardLeft + BoardWidth, BoardBottom);
        Layout.Header = FSlateRect(Left, Top, Left + Width, MainTop);
        // The compact launcher row is a fixed panel at the bottom; the full panel starts under the board.
        Layout.InteractionSection =
            FSlateRect(Left, bCompactControls ? MainBottom : BoardBottom, Left + Width, Top + Height);
        Layout.DebugSection = Layout.Header;
        Layout.MainSection = FSlateRect(Left, MainTop, Left + Width, MainBottom);
    }

    return Layout;
}
