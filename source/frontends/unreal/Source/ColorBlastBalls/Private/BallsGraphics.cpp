#include "BallsGraphics.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

namespace {

struct FQualityProfile {
    EBallsGraphics Quality;
    const TCHAR* Name;
    // 0 none, 1 FXAA, 2 TAA. TSR (=4) needs SM6 and costs more than this scene pays back.
    int32 AntiAliasingMethod;
    int32 ScreenPercentage;
    bool bBloom;
};

// The scene is flat colours, two shadowless lights and one instanced ball draw, so everything the post chain does
// beyond tonemapping is wasted. Minimum is the performance-mode sweep: every optional effect off, scalability on
// its lowest step, and a smaller frame upscaled without filtering.
const FQualityProfile Profiles[] = {
    {EBallsGraphics::Minimum, TEXT("Minimum"), 0, 67, false},
    {EBallsGraphics::Balanced, TEXT("Balanced"), 1, 85, true},
    {EBallsGraphics::High, TEXT("High"), 2, 100, true},
};

const FQualityProfile& ProfileFor(EBallsGraphics Quality) {
    for (const FQualityProfile& Profile : Profiles) {
        if (Profile.Quality == Quality) {
            return Profile;
        }
    }
    return Profiles[0];
}

// Optional passes that can be switched off outright; the ball draw and the tonemapper are the only things this
// frontend needs from the post chain.
const TCHAR* const CheapPostProcess[] = {
    TEXT("r.PostProcessAAQuality 0"),
    TEXT("r.LensFlareQuality 0"),
    TEXT("r.SceneColorFringeQuality 0"),
    TEXT("r.Tonemapper.Quality 0"),
    TEXT("r.Tonemapper.Sharpen 0"),
    TEXT("r.Tonemapper.GrainQuantization 0"),
    TEXT("r.DepthOfFieldQuality 0"),
    TEXT("r.AmbientOcclusionLevels 0"),
    TEXT("r.AmbientOcclusionStaticFraction 0"),
    TEXT("r.SSR.Quality 0"),
    TEXT("r.LightShafts 0"),
    TEXT("r.VolumetricFog 0"),
    TEXT("r.SeparateTranslucency 0"),
    TEXT("r.Upscale.Quality 0"),
    TEXT("r.DynamicGlobalIlluminationMethod 0"),
    TEXT("r.ReflectionMethod 0"),
    TEXT("r.Shadow.Virtual.Enable 0"),
    TEXT("r.DynamicRes.OperationMode 0"),
};

const TCHAR* const LowestScalability[] = {
    TEXT("sg.AntiAliasingQuality 0"), TEXT("sg.PostProcessQuality 0"), TEXT("sg.ShadowQuality 0"),
    TEXT("sg.EffectsQuality 0"),     TEXT("sg.TextureQuality 0"),     TEXT("sg.ViewDistanceQuality 0"),
    TEXT("sg.FoliageQuality 0"),     TEXT("sg.ShadingQuality 0"),
};

}

namespace BallsGraphics {

TArray<EBallsGraphics> All() { return {EBallsGraphics::Minimum, EBallsGraphics::Balanced, EBallsGraphics::High}; }

FString Name(EBallsGraphics Quality) { return ProfileFor(Quality).Name; }

std::optional<EBallsGraphics> Parse(const FString& Text) {
    for (const FQualityProfile& Profile : Profiles) {
        if (Text.Equals(Profile.Name, ESearchCase::IgnoreCase)) {
            return Profile.Quality;
        }
    }
    return std::nullopt;
}

void ApplyToWorld(UWorld* World, EBallsGraphics Quality) {
    if (!GEngine) {
        return;
    }
    const FQualityProfile& Profile = ProfileFor(Quality);
    const auto Run = [World](const FString& Command) { GEngine->Exec(World, *Command); };

    Run(FString::Printf(TEXT("r.AntiAliasingMethod %d"), Profile.AntiAliasingMethod));
    Run(FString::Printf(TEXT("r.ScreenPercentage %d"), Profile.ScreenPercentage));
    Run(FString::Printf(TEXT("r.DefaultFeature.Bloom %d"), Profile.bBloom ? 1 : 0));
    Run(FString::Printf(TEXT("r.BloomQuality %d"), Profile.bBloom ? 4 : 0));
    Run(TEXT("r.MotionBlurQuality 0"));
    if (Quality == EBallsGraphics::Minimum) {
        // Performance mode: everything optional off, lowest scalability step, and a filtered-free upscale.
        for (const TCHAR* Command : LowestScalability) {
            Run(Command);
        }
        for (const TCHAR* Command : CheapPostProcess) {
            Run(Command);
        }
        // Auto-exposure stays on, after the sweep since the lowest post-process step turns it off: without it the
        // fixed exposure draws the lit bricks nearly black.
        Run(TEXT("r.EyeAdaptationQuality 2"));
    } else {
        // Leaving Minimum must undo the sweep, or the low scalability step sticks.
        for (const TCHAR* Command : LowestScalability) {
            FString Restored(Command);
            Restored.ReplaceInline(TEXT(" 0"), *FString::Printf(TEXT(" %d"), Quality == EBallsGraphics::High ? 3 : 2));
            Run(Restored);
        }
        Run(TEXT("r.PostProcessAAQuality 1"));
        Run(TEXT("r.EyeAdaptationQuality 3"));
        Run(TEXT("r.Upscale.Quality 1"));
    }
    UE_LOG(LogTemp, Display, TEXT("BallsGraphics: %s (AA %d, screen %d%%, bloom %d)"), Profile.Name,
           Profile.AntiAliasingMethod, Profile.ScreenPercentage, Profile.bBloom ? 1 : 0);
}

}
