#pragma once

#include "CoreMinimal.h"

class UWorld;

// The design has no graphics quality, but the Unreal post chain costs more than the scene it draws: bloom, TAA,
// auto-exposure and a full-resolution screen add up to several milliseconds for a flat 2.5D picture. The port
// therefore starts at Minimum and lets the player trade looks for frame time in the Performance gadget.
enum class EBallsGraphics { Minimum, Balanced, High };

namespace BallsGraphics {

TArray<EBallsGraphics> All();
FString Name(EBallsGraphics Quality);
// Exact, case-insensitive parse of a saved or command-line name.
std::optional<EBallsGraphics> Parse(const FString& Text);
// Frame-wide pieces (anti-aliasing, screen percentage, bloom, motion blur). Camera-side pieces live in
// FBallsBoardView so they follow the board camera.
void ApplyToWorld(UWorld* World, EBallsGraphics Quality);

}
