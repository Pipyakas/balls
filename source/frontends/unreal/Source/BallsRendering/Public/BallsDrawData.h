#pragma once

#include "CoreMinimal.h"

struct FBallsDrawData {
    TArray<uint32> Packed;

    FVector2f UnpackScale = FVector2f::ZeroVector;
    float Radius = 0.25f;

    float Brightness = 2.2f;

    FVector3f Origin = FVector3f::ZeroVector;
    FVector3f AxisX = FVector3f::ZeroVector;
    FVector3f AxisY = FVector3f::ZeroVector;
    TStaticArray<FLinearColor, 8> Palette;
};

struct FBallsCellImageUpdate {
    struct FRun {
        uint32 Offset = 0;
        uint32 Size = 0;
    };

    bool bReplace = false;
    uint32 TotalBytes = 0;

    TArray<uint8> Bytes;
    TArray<FRun> Runs;
};

struct FBallsCellDrawData {
    int32 Columns = 0;
    int32 Rows = 0;
    int32 TilesAcross = 0;
    FVector3f Origin = FVector3f::ZeroVector;
    FVector3f AxisX = FVector3f::ZeroVector;
    FVector3f AxisY = FVector3f::ZeroVector;
    TStaticArray<FLinearColor, 14> Palette;
};
