#include "BallsLayout.h"

namespace {

constexpr float OuterMargin = 16.0f;
constexpr float SectionGap = 12.0f;
constexpr float HeaderHeight = 96.0f;
constexpr float ControlsWidth = 400.0f;
constexpr float DebugPanelWidth = 300.0f;
constexpr float BoardPadding = 10.0f;

}

FBallsGameplayLayout ComputeGameplayLayout(FVector2D ScreenSize, int32 BoardColumns, int32 BoardRows) {
    // Three columns: controls | board | stats, so the board gets the full height of the window.
    FBallsGameplayLayout Layout;
    const float Width = static_cast<float>(ScreenSize.X);
    const float Height = static_cast<float>(ScreenSize.Y);
    const float Top = OuterMargin;
    const float Bottom = Height - OuterMargin;

    const float ControlsLeft = OuterMargin;
    const float ControlsRight = ControlsLeft + ControlsWidth;
    Layout.Header = FSlateRect(ControlsLeft, Top, ControlsRight, Top + HeaderHeight);
    Layout.InteractionSection = FSlateRect(ControlsLeft, Layout.Header.Bottom + SectionGap, ControlsRight, Bottom);

    Layout.DebugSection = FSlateRect(Width - OuterMargin - DebugPanelWidth, Top, Width - OuterMargin, Bottom);
    Layout.MainSection = FSlateRect(ControlsRight + SectionGap, Top, Layout.DebugSection.Left - SectionGap, Bottom);

    const float Columns = static_cast<float>(FMath::Max(BoardColumns, 1));
    const float Rows = static_cast<float>(FMath::Max(BoardRows, 1));
    const FVector2D MainSize = Layout.MainSection.GetSize();
    const float Cell = FMath::Max(1.0f, FMath::Min((static_cast<float>(MainSize.X) - BoardPadding * 2.0f) / Columns,
                                                   (static_cast<float>(MainSize.Y) - BoardPadding * 2.0f) / Rows));
    const float BoardWidth = Cell * Columns;
    const float BoardHeight = Cell * Rows;
    const float BoardLeft = Layout.MainSection.Left + (static_cast<float>(MainSize.X) - BoardWidth) * 0.5f;
    const float BoardTop = Layout.MainSection.Top + (static_cast<float>(MainSize.Y) - BoardHeight) * 0.5f;
    Layout.CellSize = Cell;
    Layout.Board = FSlateRect(BoardLeft, BoardTop, BoardLeft + BoardWidth, BoardTop + BoardHeight);
    return Layout;
}
