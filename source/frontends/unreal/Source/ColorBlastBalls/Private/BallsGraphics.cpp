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
    bool bMotionBlur;
};

// The engine defaults to TAA plus a full-resolution frame; for flat colours FXAA at a slightly lower screen
// percentage reads almost the same and costs a fraction of the frame.
const FQualityProfile Profiles[] = {
    {EBallsGraphics::Minimum, TEXT("Minimum"), 0, 70, false, false},
    {EBallsGraphics::Balanced, TEXT("Balanced"), 1, 85, true, false},
    {EBallsGraphics::High, TEXT("High"), 2, 100, true, false},
};

const FQualityProfile& ProfileFor(EBallsGraphics Quality) {
    for (const FQualityProfile& Profile : Profiles) {
        if (Profile.Quality == Quality) {
            return Profile;
        }
    }
    return Profiles[0];
}

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
    // Motion blur was already off project-wide; keep it off at every level and let the profile own the rest.
    const FString Commands[] = {
        FString::Printf(TEXT("r.AntiAliasingMethod %d"), Profile.AntiAliasingMethod),
        FString::Printf(TEXT("r.ScreenPercentage %d"), Profile.ScreenPercentage),
        FString::Printf(TEXT("r.DefaultFeature.Bloom %d"), Profile.bBloom ? 1 : 0),
        FString::Printf(TEXT("r.BloomQuality %d"), Profile.bBloom ? 4 : 0),
        FString::Printf(TEXT("r.MotionBlurQuality %d"), Profile.bMotionBlur ? 2 : 0),
    };
    for (const FString& Command : Commands) {
        GEngine->Exec(World, *Command);
    }
    UE_LOG(LogTemp, Display, TEXT("BallsGraphics: %s (AA %d, screen %d%%, bloom %d)"), Profile.Name,
           Profile.AntiAliasingMethod, Profile.ScreenPercentage, Profile.bBloom ? 1 : 0);
}

}
