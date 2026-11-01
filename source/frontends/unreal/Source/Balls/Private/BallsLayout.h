#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

struct FBallsGameplayLayout {
    FSlateRect Header;
    FSlateRect MainSection;
    FSlateRect InteractionSection;
    FSlateRect DebugSection;

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

void SetLayoutOrientation(LayoutOrientation Orientation);

void SetWideScreenAnchor(WideScreenAnchor Anchor);

float PortraitLauncherSize(float PanelWidth);

FBallsGameplayLayout ComputeGameplayLayout(FVector2D ScreenSize, int32 BoardColumns, int32 BoardRows, float DpiScale,
                                           int32 LauncherRows);
