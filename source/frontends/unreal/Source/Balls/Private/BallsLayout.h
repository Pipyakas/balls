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
    // One panel per gadget, so the HUD draws the same shapes the design describes: left (header over interaction)
    // and right (debug) in landscape, top (header over debug strip) and bottom (interaction) in portrait.
    FSlateRect LeftGadget;
    FSlateRect RightGadget;
    FSlateRect TopGadget;
    FSlateRect BottomGadget;
    FSlateRect Board;
    float CellSize = 1.0f;
    bool bPortrait = false;

    FVector2D BoardToScreen(float Column, float Row) const {
        return FVector2D(Board.Left + Column * CellSize, Board.Top + Row * CellSize);
    }
};

// The Settings page's Layout > Orientation. Auto follows the window shape; a forced orientation lays out inside a
// centered 9:16 (portrait) or 16:10 (landscape) frame when the window has the other shape, so the rest of the
// window stays empty (the game's "letterbox"). Forced landscape in a landscape window uses the whole window.
void SetLayoutOrientation(LayoutOrientation Orientation);
// The Settings page's Wide screens: whether a window wider than 16:9 is filled to its edges or keeps 16:9 centred.
void SetWideScreenAnchor(WideScreenAnchor Anchor);

// Button size of the portrait launcher row for a panel of the given width.
float PortraitLauncherSize(float PanelWidth);

// ScreenSize is in Slate units and DpiScale is the Slate-to-pixel scale; the layout stays inside the platform's
// safe area (display cutout, rounded corners). LauncherRows is the level's visible launcher rows in Normal mode, 0
// in Performance: core sizes the portrait bottom gadget as rows * slot + gaps + 48, capped at the 38% panel.
FBallsGameplayLayout ComputeGameplayLayout(FVector2D ScreenSize, int32 BoardColumns, int32 BoardRows, float DpiScale,
                                           int32 LauncherRows);
