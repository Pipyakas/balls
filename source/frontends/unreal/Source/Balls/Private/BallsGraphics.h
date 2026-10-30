#pragma once

#include "CoreMinimal.h"

#include <optional>

class UWorld;

// The design has no graphics settings; this is the Unreal port's own page. The engine's scalability groups (sg.*) set
// the bulk of the cost, each 0 (Low) to 4 (Cinematic); the UE5 features with the largest frame costs (Lumen,
// Nanite, virtual shadow maps) and the anti-aliasing / upscaling method get their own switches, since scalability
// alone never turns them on in a project that defaults them off. Every field applies at once, in game.

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
// TAAU is TAA reconstructing from a lower screen percentage; FSR needs AMD's FidelityFX plugin in the engine.
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
    bool bDynamicResolution = false;

    bool operator==(const FBallsGraphicsSettings& Other) const;
};

namespace BallsGraphics {

// Low, Medium, High, Epic, Cinematic: the engine's scalability level for every group, plus the features that
// level is meant to show.
FBallsGraphicsSettings Preset(int32 Level);
FString PresetName(int32 Level);
// The preset the settings equal, if any; anything changed after picking one makes them Custom.
std::optional<int32> MatchingPreset(const FBallsGraphicsSettings& Settings);
FString Describe(const FBallsGraphicsSettings& Settings);

FString GroupName(EBallsScalabilityGroup Group);
FString AntiAliasingName(EBallsAntiAliasing Method);
FString GlobalIlluminationName(EBallsGlobalIllumination Method);
FString ReflectionsName(EBallsReflections Method);
FString ShadowsName(EBallsShadows Method);
TArray<int32> ScreenPercentages();

// Why a feature cannot run here (the shader model, a missing plugin), or empty when it can.
FString Unavailable(EBallsAntiAliasing Method);
FString UnavailableLumen();
FString UnavailableNanite();
FString UnavailableVirtualShadows();

// Saved per user in GameUserSettings.ini as one line of key=value pairs, which the control API also takes.
FString Serialize(const FBallsGraphicsSettings& Settings);
// Starts from Base and overrides what Text names: "preset=epic", group names ("shadows=2"), "gi", "reflections",
// "shadowmethod", "nanite", "aa", "screen", "dynamic". Returns an error message for anything it cannot read.
FString ParseInto(const FString& Text, FBallsGraphicsSettings& Base);

// The debug gadget's Graphics rows (shown with Advanced Settings open): what the renderer actually runs, read back
// from the engine rather than from the settings, so a feature the machine refused shows as off.
TArray<TPair<FString, FString>> DebugRows(const FBallsGraphicsSettings& Settings, FIntPoint ViewportPixels);

// Frame-wide state: scalability groups, the rendering methods and screen resolution. Scene-side pieces (the key
// light's shadows, bloom on the camera) live in FBallsBoardView.
void ApplyToWorld(UWorld* World, const FBallsGraphicsSettings& Settings);

}
