#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

// Screen split shared by the Slate HUD and the board camera, in Slate units of the game viewport.
// The play field always claims the long axis edge-to-edge: full height in landscape (panels on the
// left/right), full width in portrait (panels on the top/bottom).
struct FBallsGameplayLayout {
    FSlateRect Header;
    FSlateRect MainSection;
    FSlateRect InteractionSection;
    FSlateRect DebugSection;
    FSlateRect Board;
    float CellSize = 1.0f;
    bool bPortrait = false;

    FVector2D BoardToScreen(float Column, float Row) const {
        return FVector2D(Board.Left + Column * CellSize, Board.Top + Row * CellSize);
    }
};

// The Settings page's Layout > Orientation. Auto follows the window shape; a forced orientation lays out inside a
// centered 9:16 (portrait) or 16:10 (landscape) frame when the window cannot take that shape, so the rest of the
// window stays empty (the game's "letterbox").
void SetLayoutOrientation(LayoutOrientation Orientation);

// Button size of the portrait launcher row for a panel of the given width.
float PortraitLauncherSize(float PanelWidth);

// ScreenSize is in Slate units and DpiScale is the Slate-to-pixel scale; the layout stays inside the platform's
// safe area (display cutout, rounded corners). bCompactControls shrinks the portrait bottom panel to one scrolling
// row of launchers so the board gets the rest of the height; performance mode needs the full-size panel.
FBallsGameplayLayout ComputeGameplayLayout(FVector2D ScreenSize, int32 BoardColumns, int32 BoardRows, float DpiScale,
                                           bool bCompactControls);
