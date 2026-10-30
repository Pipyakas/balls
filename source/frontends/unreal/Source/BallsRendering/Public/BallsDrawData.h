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

// The tiles of the core's cell image that changed since the last update, copied out on the game thread: the board
// snapshot they come from may be gone by the time the render thread uploads them.
struct FBallsCellImageUpdate {
    struct FRun {
        uint32 Offset = 0;
        uint32 Size = 0;
    };
    // Replaces the whole image (a new board): Bytes holds every tile and Runs is empty.
    bool bReplace = false;
    uint32 TotalBytes = 0;
    // Otherwise the changed runs of tiles back to back, each written at its Offset in the image.
    TArray<uint8> Bytes;
    TArray<FRun> Runs;
};

// How to draw the cell image: board size, the board plane in the world (as FBallsDrawData) and the seven brick
// colors followed by the same seven as they flash when hit.
struct FBallsCellDrawData {
    int32 Columns = 0;
    int32 Rows = 0;
    int32 TilesAcross = 0;
    FVector3f Origin = FVector3f::ZeroVector;
    FVector3f AxisX = FVector3f::ZeroVector;
    FVector3f AxisY = FVector3f::ZeroVector;
    TStaticArray<FLinearColor, 14> Palette;
};
