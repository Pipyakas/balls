#include "BallsGraphics.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RHI.h"
#include "RHIStrings.h"
#include "RenderUtils.h"

namespace {

constexpr int32 GroupCount = FBallsGraphicsSettings::GroupCount;
constexpr int32 LevelCount = FBallsGraphicsSettings::LevelCount;
constexpr int32 MinimumDynamicScreenPercentage = 50;

const TCHAR* const PresetNames[LevelCount] = {TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic")};

struct FGroupInfo {
    const TCHAR* Name;
    const TCHAR* Key;
    const TCHAR* Cvar;
};

const FGroupInfo Groups[GroupCount] = {
    {TEXT("View distance"), TEXT("viewdistance"), TEXT("sg.ViewDistanceQuality")},
    {TEXT("Anti-aliasing"), TEXT("antialiasing"), TEXT("sg.AntiAliasingQuality")},
    {TEXT("Post-processing"), TEXT("postprocess"), TEXT("sg.PostProcessQuality")},
    {TEXT("Shadows"), TEXT("shadows"), TEXT("sg.ShadowQuality")},
    {TEXT("Global illumination"), TEXT("globalillumination"), TEXT("sg.GlobalIlluminationQuality")},
    {TEXT("Reflections"), TEXT("reflections"), TEXT("sg.ReflectionQuality")},
    {TEXT("Textures"), TEXT("textures"), TEXT("sg.TextureQuality")},
    {TEXT("Effects"), TEXT("effects"), TEXT("sg.EffectsQuality")},
    {TEXT("Foliage"), TEXT("foliage"), TEXT("sg.FoliageQuality")},
    {TEXT("Shading"), TEXT("shading"), TEXT("sg.ShadingQuality")},
};

const TCHAR* const AntiAliasingNames[] = {TEXT("Off"), TEXT("FXAA"), TEXT("TAA"), TEXT("TAAU"), TEXT("TSR"), TEXT("FSR")};
const TCHAR* const GlobalIlluminationNames[] = {TEXT("Off"), TEXT("Lumen")};
const TCHAR* const ReflectionNames[] = {TEXT("Off"), TEXT("Screen space"), TEXT("Lumen")};
const TCHAR* const ShadowNames[] = {TEXT("Off"), TEXT("Shadow maps"), TEXT("Virtual")};

// AMD's FidelityFX plugins register one of these when installed; none ships with the engine.
const TCHAR* const FsrEnableCvars[] = {TEXT("r.FidelityFX.FSR4.Enabled"), TEXT("r.FidelityFX.FSR3.Enabled"),
                                       TEXT("r.FidelityFX.FSR.Enabled")};

IConsoleVariable* InstalledFsrCvar() {
    for (const TCHAR* Name : FsrEnableCvars) {
        if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name)) {
            return Variable;
        }
    }
    return nullptr;
}

const TCHAR* const SmNeeded = TEXT("needs Shader Model 6 (D3D12 or Vulkan)");

FString Squashed(const FString& Text) { return Text.Replace(TEXT(" "), TEXT("")).Replace(TEXT("-"), TEXT("")).ToLower(); }

template <typename Enum, int32 Count>
bool ParseName(const FString& Value, const TCHAR* const (&Names)[Count], Enum& Out) {
    for (int32 Index = 0; Index < Count; ++Index) {
        if (Squashed(Value) == Squashed(Names[Index])) {
            Out = static_cast<Enum>(Index);
            return true;
        }
    }
    return false;
}

bool ParseFlag(const FString& Value, bool& Out) {
    const FString Lower = Value.ToLower();
    if (Lower == TEXT("1") || Lower == TEXT("on") || Lower == TEXT("true")) {
        Out = true;
        return true;
    }
    if (Lower == TEXT("0") || Lower == TEXT("off") || Lower == TEXT("false")) {
        Out = false;
        return true;
    }
    return false;
}

bool ParseLevel(const FString& Value, int32& Out) {
    for (int32 Level = 0; Level < LevelCount; ++Level) {
        if (Value == FString::FromInt(Level) || Value.Equals(PresetNames[Level], ESearchCase::IgnoreCase)) {
            Out = Level;
            return true;
        }
    }
    return false;
}

}

bool FBallsGraphicsSettings::operator==(const FBallsGraphicsSettings& Other) const {
    for (int32 Index = 0; Index < GroupCount; ++Index) {
        if (Groups[Index] != Other.Groups[Index]) {
            return false;
        }
    }
    return GlobalIllumination == Other.GlobalIllumination && Reflections == Other.Reflections &&
           Shadows == Other.Shadows && bNanite == Other.bNanite && AntiAliasing == Other.AntiAliasing &&
           ScreenPercentage == Other.ScreenPercentage && bDynamicResolution == Other.bDynamicResolution &&
           TargetFps == Other.TargetFps;
}

namespace BallsGraphics {

// Low is the old performance setting: the post chain costs more than this flat scene, so nothing optional runs.
// Medium and High add temporal anti-aliasing, screen-space reflections and the key light's shadows; Epic and
// Cinematic are the full UE5 look, with Lumen, Nanite and virtual shadow maps.
FBallsGraphicsSettings Preset(int32 Level) {
    Level = FMath::Clamp(Level, 0, LevelCount - 1);
    FBallsGraphicsSettings Settings;
    for (int32& Group : Settings.Groups) {
        Group = Level;
    }
    const bool bFullUe5 = Level >= 3;
    Settings.GlobalIllumination = bFullUe5 ? EBallsGlobalIllumination::Lumen : EBallsGlobalIllumination::None;
    Settings.Reflections = bFullUe5 ? EBallsReflections::Lumen
                           : Level > 0 ? EBallsReflections::ScreenSpace
                                       : EBallsReflections::None;
    Settings.Shadows = bFullUe5 ? EBallsShadows::Virtual : Level > 0 ? EBallsShadows::ShadowMaps : EBallsShadows::Off;
    Settings.bNanite = bFullUe5;
    Settings.AntiAliasing = Level == 0 ? EBallsAntiAliasing::None
                            : Level == 1 ? EBallsAntiAliasing::TAA
                                         : EBallsAntiAliasing::TSR;
    return Settings;
}

void ApplyPreset(int32 Level, FBallsGraphicsSettings& Settings) {
    const FBallsGraphicsSettings PresetSettings = Preset(Level);
    for (int32 Index = 0; Index < GroupCount; ++Index) {
        Settings.Groups[Index] = PresetSettings.Groups[Index];
    }
    Settings.GlobalIllumination = PresetSettings.GlobalIllumination;
    Settings.Reflections = PresetSettings.Reflections;
    Settings.Shadows = PresetSettings.Shadows;
    Settings.bNanite = PresetSettings.bNanite;
    Settings.AntiAliasing = PresetSettings.AntiAliasing;
}

FString PresetName(int32 Level) { return PresetNames[FMath::Clamp(Level, 0, LevelCount - 1)]; }

std::optional<int32> MatchingPreset(const FBallsGraphicsSettings& Settings) {
    for (int32 Level = 0; Level < LevelCount; ++Level) {
        const FBallsGraphicsSettings P = Preset(Level);
        bool bMatch = true;
        for (int32 Index = 0; Index < GroupCount; ++Index) {
            if (Settings.Groups[Index] != P.Groups[Index]) {
                bMatch = false;
                break;
            }
        }
        if (bMatch && Settings.GlobalIllumination == P.GlobalIllumination &&
            Settings.Reflections == P.Reflections && Settings.Shadows == P.Shadows &&
            Settings.bNanite == P.bNanite && Settings.AntiAliasing == P.AntiAliasing) {
            return Level;
        }
    }
    return std::nullopt;
}

FString Describe(const FBallsGraphicsSettings& Settings) {
    const std::optional<int32> Level = MatchingPreset(Settings);
    const FString Dynamic = Settings.bDynamicResolution
                                ? FString::Printf(TEXT(" dynamic, %d FPS"), Settings.TargetFps)
                                : FString();
    return FString::Printf(TEXT("%s - %s, %d%%%s"), Level ? *PresetName(*Level) : TEXT("Custom"),
                           *AntiAliasingName(Settings.AntiAliasing), Settings.ScreenPercentage,
                           *Dynamic);
}

FString GroupName(EBallsScalabilityGroup Group) { return Groups[static_cast<int32>(Group)].Name; }
FString AntiAliasingName(EBallsAntiAliasing Method) { return AntiAliasingNames[static_cast<int32>(Method)]; }
FString GlobalIlluminationName(EBallsGlobalIllumination Method) { return GlobalIlluminationNames[static_cast<int32>(Method)]; }
FString ReflectionsName(EBallsReflections Method) { return ReflectionNames[static_cast<int32>(Method)]; }
FString ShadowsName(EBallsShadows Method) { return ShadowNames[static_cast<int32>(Method)]; }
TArray<int32> ScreenPercentages() { return {50, 67, 75, 85, 100}; }

int32 MaximumTargetFps() {
    FScreenResolutionArray Resolutions;
    int32 MaxRefreshRate = 0;
    RHIGetAvailableResolutions(Resolutions, true);
    for (const FScreenResolutionRHI& Resolution : Resolutions) {
        MaxRefreshRate = FMath::Max(MaxRefreshRate, static_cast<int32>(Resolution.RefreshRate));
    }
    return MaxRefreshRate > 0 ? MaxRefreshRate : 240;
}

FString Unavailable(EBallsAntiAliasing Method) {
    if (Method == EBallsAntiAliasing::FSR && !InstalledFsrCvar()) {
        return TEXT("needs AMD's FidelityFX FSR plugin in the engine");
    }
    return FString();
}

FString UnavailableLumen() { return DoesPlatformSupportLumenGI(GMaxRHIShaderPlatform) ? FString() : FString(SmNeeded); }

FString UnavailableNanite() {
    if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform, false)) {
        return SmNeeded;
    }
    return UseNanite(GMaxRHIShaderPlatform) ? FString() : FString(TEXT("not enabled in this build"));
}

FString UnavailableVirtualShadows() {
    return DoesPlatformSupportVirtualShadowMaps(GMaxRHIShaderPlatform) ? FString() : FString(SmNeeded);
}

FString Serialize(const FBallsGraphicsSettings& Settings) {
    FString Text;
    for (int32 Index = 0; Index < GroupCount; ++Index) {
        Text += FString::Printf(TEXT("sg.%s=%d;"), Groups[Index].Key, Settings.Groups[Index]);
    }
    Text += FString::Printf(TEXT("gi=%s;reflectionmethod=%s;shadowmethod=%s;nanite=%d;aa=%s;screen=%d;dynamic=%d;fps=%d"),
                            *Squashed(GlobalIlluminationName(Settings.GlobalIllumination)),
                            *Squashed(ReflectionsName(Settings.Reflections)), *Squashed(ShadowsName(Settings.Shadows)),
                            Settings.bNanite ? 1 : 0, *Squashed(AntiAliasingName(Settings.AntiAliasing)),
                            Settings.ScreenPercentage, Settings.bDynamicResolution ? 1 : 0,
                            FMath::Clamp(Settings.TargetFps, 1, MaximumTargetFps()));
    return Text;
}

FString ParseInto(const FString& Text, FBallsGraphicsSettings& Base) {
    TArray<FString> Pairs;
    Text.ParseIntoArray(Pairs, TEXT(";"));
    // A preset first, so the other keys in the same request adjust it rather than being replaced by it.
    for (const FString& Pair : Pairs) {
        FString Key;
        FString Value;
        if (Pair.Split(TEXT("="), &Key, &Value) && Key.TrimStartAndEnd().Equals(TEXT("preset"), ESearchCase::IgnoreCase)) {
            int32 Level = 0;
            if (!ParseLevel(Value.TrimStartAndEnd(), Level)) {
                return FString::Printf(TEXT("preset must be low, medium, high, epic or cinematic, not %s"), *Value);
            }
            ApplyPreset(Level, Base);
        }
    }
    for (const FString& Pair : Pairs) {
        FString Key;
        FString Value;
        if (!Pair.Split(TEXT("="), &Key, &Value)) {
            return FString::Printf(TEXT("%s is not key=value"), *Pair);
        }
        Key = Key.TrimStartAndEnd().ToLower();
        Value = Value.TrimStartAndEnd();
        bool bRead = true;
        if (Key == TEXT("preset")) {
            continue;
        } else if (Key.StartsWith(TEXT("sg."))) {
            bRead = false;
            for (int32 Index = 0; Index < GroupCount; ++Index) {
                if (Key.RightChop(3) == Groups[Index].Key) {
                    bRead = ParseLevel(Value, Base.Groups[Index]);
                }
            }
        } else if (Key == TEXT("gi")) {
            bRead = ParseName(Value, GlobalIlluminationNames, Base.GlobalIllumination);
        } else if (Key == TEXT("reflectionmethod")) {
            bRead = ParseName(Value, ReflectionNames, Base.Reflections);
        } else if (Key == TEXT("shadowmethod")) {
            bRead = ParseName(Value, ShadowNames, Base.Shadows);
        } else if (Key == TEXT("nanite")) {
            bRead = ParseFlag(Value, Base.bNanite);
        } else if (Key == TEXT("aa")) {
            bRead = ParseName(Value, AntiAliasingNames, Base.AntiAliasing);
        } else if (Key == TEXT("screen")) {
            bRead = Value.IsNumeric() && FCString::Atoi(*Value) >= MinimumDynamicScreenPercentage &&
                    FCString::Atoi(*Value) <= 100;
            if (bRead) {
                Base.ScreenPercentage = FCString::Atoi(*Value);
            }
        } else if (Key == TEXT("dynamic")) {
            bRead = ParseFlag(Value, Base.bDynamicResolution);
        } else if (Key == TEXT("fps")) {
            bRead = Value.IsNumeric();
            if (bRead) {
                Base.TargetFps = FMath::Clamp(FCString::Atoi(*Value), 1, MaximumTargetFps());
            }
        } else {
            return FString::Printf(TEXT("unknown graphics key %s"), *Key);
        }
        if (!bRead) {
            return FString::Printf(TEXT("cannot read %s=%s"), *Key, *Value);
        }
    }
    return FString();
}

TArray<TPair<FString, FString>> DebugRows(const FBallsGraphicsSettings& Settings, FIntPoint ViewportPixels) {
    const auto Read = [](const TCHAR* Name) {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        return Variable ? Variable->GetFloat() : -1.0f;
    };
    const std::optional<int32> Level = MatchingPreset(Settings);
    FString FeatureLevel;
    GetFeatureLevelName(GMaxRHIFeatureLevel, FeatureLevel);
    const int32 Method = static_cast<int32>(Read(TEXT("r.AntiAliasingMethod")));
    const TCHAR* const MethodNames[] = {TEXT("Off"), TEXT("FXAA"), TEXT("TAA"), TEXT("MSAA"), TEXT("TSR")};
    FString AntiAliasing = Method >= 0 && Method < UE_ARRAY_COUNT(MethodNames) ? MethodNames[Method] : TEXT("?");
    if (Method == 2 && Read(TEXT("r.TemporalAA.Upsampling")) > 0.0f) {
        AntiAliasing = TEXT("TAAU");
    }
    if (const IConsoleVariable* Fsr = InstalledFsrCvar(); Fsr && Fsr->GetInt() > 0) {
        AntiAliasing = TEXT("FSR");
    }
    const float Percentage = Read(TEXT("r.ScreenPercentage"));
    const bool bDynamic = Read(TEXT("r.DynamicRes.OperationMode")) > 0.0f;
    const int32 Gi = static_cast<int32>(Read(TEXT("r.DynamicGlobalIlluminationMethod")));
    const int32 Reflections = static_cast<int32>(Read(TEXT("r.ReflectionMethod")));
    TArray<TPair<FString, FString>> Rows = {
        {TEXT("Preset"), Level ? PresetName(*Level) : FString(TEXT("Custom"))},
        {TEXT("Shader platform"), FString::Printf(TEXT("%s (%s)"),
                                                  *FDataDrivenShaderPlatformInfo::GetName(GMaxRHIShaderPlatform).ToString(),
                                                  *FeatureLevel)},
        {TEXT("Anti-aliasing"), AntiAliasing},
        {TEXT("Resolution"), bDynamic ? FString::Printf(TEXT("dynamic %d-%.0f%%, %d FPS target"), MinimumDynamicScreenPercentage,
                                                        Percentage, Settings.TargetFps)
                                      : FString::Printf(TEXT("%.0f%%"), Percentage)},
        {TEXT("Render size"), FString::Printf(TEXT("%dx%d of %dx%d"), FMath::RoundToInt(ViewportPixels.X * Percentage / 100.0f),
                                              FMath::RoundToInt(ViewportPixels.Y * Percentage / 100.0f), ViewportPixels.X,
                                              ViewportPixels.Y)},
        {TEXT("Global illumination"), Gi == 1 ? TEXT("Lumen") : Gi == 2 ? TEXT("Screen space") : TEXT("Off")},
        {TEXT("Reflections"), Reflections == 1 ? TEXT("Lumen") : Reflections == 2 ? TEXT("Screen space") : TEXT("Off")},
        {TEXT("Shadows"), Settings.Shadows == EBallsShadows::Off ? FString(TEXT("Off"))
                          : Read(TEXT("r.Shadow.Virtual.Enable")) > 0.0f ? FString(TEXT("Virtual"))
                                                                          : FString(TEXT("Shadow maps"))},
        {TEXT("Nanite"), Read(TEXT("r.Nanite")) > 0.0f && UseNanite(GMaxRHIShaderPlatform) ? TEXT("On") : TEXT("Off")},
        {TEXT("Scalability"), FString()},
    };
    for (int32 Index = 0; Index < GroupCount; ++Index) {
        Rows.Add({Groups[Index].Name, PresetName(Settings.Groups[Index])});
    }
    return Rows;
}

void ApplyToWorld(UWorld* World, const FBallsGraphicsSettings& Settings) {
    if (!GEngine) {
        return;
    }
    const auto Run = [World](const FString& Command) { GEngine->Exec(World, *Command); };

    // The groups first: each one rewrites its own r.* values from the scalability ini, and the explicit methods
    // below must win over them.
    for (int32 Index = 0; Index < GroupCount; ++Index) {
        Run(FString::Printf(TEXT("%s %d"), Groups[Index].Cvar, Settings.Groups[Index]));
    }
    // The lowest post-process step also turns eye adaptation off, which drops the camera's manual exposure and
    // draws the lit bricks nearly black.
    if (Settings.Groups[static_cast<int32>(EBallsScalabilityGroup::PostProcess)] == 0) {
        Run(TEXT("r.EyeAdaptationQuality 2"));
    }
    Run(TEXT("r.MotionBlurQuality 0"));

    const bool bLumen = UnavailableLumen().IsEmpty();
    Run(FString::Printf(TEXT("r.DynamicGlobalIlluminationMethod %d"),
                        bLumen && Settings.GlobalIllumination == EBallsGlobalIllumination::Lumen ? 1 : 0));
    const int32 ReflectionMethod = Settings.Reflections == EBallsReflections::Lumen ? (bLumen ? 1 : 2)
                                   : Settings.Reflections == EBallsReflections::ScreenSpace ? 2
                                                                                            : 0;
    Run(FString::Printf(TEXT("r.ReflectionMethod %d"), ReflectionMethod));
    Run(FString::Printf(TEXT("r.Shadow.Virtual.Enable %d"),
                        Settings.Shadows == EBallsShadows::Virtual && UnavailableVirtualShadows().IsEmpty() ? 1 : 0));
    Run(FString::Printf(TEXT("r.Nanite %d"), Settings.bNanite && UnavailableNanite().IsEmpty() ? 1 : 0));

    IConsoleVariable* Fsr = InstalledFsrCvar();
    const bool bFsr = Settings.AntiAliasing == EBallsAntiAliasing::FSR && Fsr;
    int32 Method = 0;
    switch (Settings.AntiAliasing) {
    case EBallsAntiAliasing::None: Method = 0; break;
    case EBallsAntiAliasing::FXAA: Method = 1; break;
    case EBallsAntiAliasing::TAA:
    case EBallsAntiAliasing::TAAU: Method = 2; break;
    case EBallsAntiAliasing::TSR: Method = 4; break;
    // FSR replaces the temporal upscaler, so it sits on TAA; without the plugin TSR is the closest match.
    case EBallsAntiAliasing::FSR: Method = bFsr ? 2 : 4; break;
    default: break;
    }
    Run(FString::Printf(TEXT("r.AntiAliasingMethod %d"), Method));
    Run(FString::Printf(TEXT("r.TemporalAA.Upsampling %d"), Settings.AntiAliasing == EBallsAntiAliasing::TAAU ? 1 : 0));
    if (Fsr) {
        Fsr->Set(bFsr ? 1 : 0, ECVF_SetByCode);
    }

    Run(FString::Printf(TEXT("r.ScreenPercentage %d"), Settings.ScreenPercentage));
    Run(FString::Printf(TEXT("r.DynamicRes.OperationMode %d"), Settings.bDynamicResolution ? 2 : 0));
    Run(FString::Printf(TEXT("r.DynamicRes.MinScreenPercentage %d"), MinimumDynamicScreenPercentage));
    Run(FString::Printf(TEXT("r.DynamicRes.MaxScreenPercentage %d"), Settings.ScreenPercentage));
    const int32 TargetFps = FMath::Clamp(Settings.TargetFps, 1, MaximumTargetFps());
    Run(FString::Printf(TEXT("r.DynamicRes.FrameTimeBudget %.2f"), 1000.0f / static_cast<float>(TargetFps)));
    UE_LOG(LogTemp, Display, TEXT("BallsGraphics: %s"), *Serialize(Settings));
}

}
