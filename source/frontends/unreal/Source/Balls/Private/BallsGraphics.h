#pragma once

#include "CoreMinimal.h"

#include <optional>

class UWorld;

enum class EBallsScalabilityGroup : uint8 {
    ViewDistance,
    AntiAliasing,
    PostProcess,
    Shadows,
    GlobalIllumination,
    Reflections,
    Textures,
    Effects,
    Foliage,
    Shading,
    Count
};

enum class EBallsGlobalIllumination : uint8 { None, Lumen };
enum class EBallsReflections : uint8 { None, ScreenSpace, Lumen };
enum class EBallsShadows : uint8 { Off, ShadowMaps, Virtual };

enum class EBallsAntiAliasing : uint8 { None, FXAA, TAA, TAAU, TSR, FSR };

struct FBallsGraphicsSettings {
    static constexpr int32 GroupCount = static_cast<int32>(EBallsScalabilityGroup::Count);
    static constexpr int32 LevelCount = 5;

    int32 Groups[GroupCount] = {};
    EBallsGlobalIllumination GlobalIllumination = EBallsGlobalIllumination::None;
    EBallsReflections Reflections = EBallsReflections::None;
    EBallsShadows Shadows = EBallsShadows::Off;
    bool bNanite = false;
    EBallsAntiAliasing AntiAliasing = EBallsAntiAliasing::None;
    int32 ScreenPercentage = 100;
    bool bDynamicResolution = true;
    int32 TargetFps = 60;

    bool operator==(const FBallsGraphicsSettings& Other) const;
};

namespace BallsGraphics {

FBallsGraphicsSettings Preset(int32 Level);
void ApplyPreset(int32 Level, FBallsGraphicsSettings& Settings);
FString PresetName(int32 Level);

std::optional<int32> MatchingPreset(const FBallsGraphicsSettings& Settings);
FString Describe(const FBallsGraphicsSettings& Settings);

FString GroupName(EBallsScalabilityGroup Group);
FString AntiAliasingName(EBallsAntiAliasing Method);
FString GlobalIlluminationName(EBallsGlobalIllumination Method);
FString ReflectionsName(EBallsReflections Method);
FString ShadowsName(EBallsShadows Method);
TArray<int32> ScreenPercentages();
int32 MaximumTargetFps();

FString Unavailable(EBallsAntiAliasing Method);
FString UnavailableLumen();
FString UnavailableNanite();
FString UnavailableVirtualShadows();

FString Serialize(const FBallsGraphicsSettings& Settings);

FString ParseInto(const FString& Text, FBallsGraphicsSettings& Base);

TArray<TPair<FString, FString>> DebugRows(const FBallsGraphicsSettings& Settings, FIntPoint ViewportPixels);

void ApplyToWorld(UWorld* World, const FBallsGraphicsSettings& Settings);

}
