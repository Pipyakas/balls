#pragma once

#include "CoreMinimal.h"

// One frame of ball positions handed from the game thread to the render thread.
// Packed is the core's 32-bit render format: 14-bit x, 14-bit y, 4-bit color.
struct FBallsDrawData {
    TArray<uint32> Packed;
    // Board units per packed coordinate step on each axis.
    FVector2f UnpackScale = FVector2f::ZeroVector;
    float Radius = 0.25f;
    // Scene-linear brightness of a ball; above 1 so bloom picks them up. Without bloom 1.0 keeps the palette exact.
    float Brightness = 2.2f;
    // World transform of the board plane: world = Origin + AxisX * boardX + AxisY * boardY.
    FVector3f Origin = FVector3f::ZeroVector;
    FVector3f AxisX = FVector3f::ZeroVector;
    FVector3f AxisY = FVector3f::ZeroVector;
    TStaticArray<FLinearColor, 8> Palette;
};
