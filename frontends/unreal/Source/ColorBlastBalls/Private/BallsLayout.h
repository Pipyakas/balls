#pragma once

#include "CoreMinimal.h"

// Screen split shared by the Slate HUD and the board camera, in Slate units of the game viewport.
// Unlike the raylib front end it uses three columns: header and controls on the left, the board in the
// middle at full height, and the debug panel on the right.
struct FBallsGameplayLayout {
    FSlateRect Header;
    FSlateRect MainSection;
    FSlateRect InteractionSection;
    FSlateRect DebugSection;
    FSlateRect Board;
    float CellSize = 1.0f;

    FVector2D BoardToScreen(float Column, float Row) const {
        return FVector2D(Board.Left + Column * CellSize, Board.Top + Row * CellSize);
    }
};

FBallsGameplayLayout ComputeGameplayLayout(FVector2D ScreenSize, int32 BoardColumns, int32 BoardRows);
