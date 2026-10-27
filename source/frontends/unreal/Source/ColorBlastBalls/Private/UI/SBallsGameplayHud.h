#pragma once

#include "BallsLayout.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FBallsApp;
class FBallsGameplay;

// Everything 2D during a game: header, launcher grid or performance controls, debug panel, brick hit counts,
// pause / win overlay and toasts. The 3D board shows through the main section.
class SBallsGameplayHud final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsGameplayHud) {}
        SLATE_ARGUMENT(FBallsApp*, App)
        SLATE_ARGUMENT(TSharedPtr<FBallsGameplay>, Gameplay)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FBallsGameplayLayout CurrentLayout() const;
    TSharedRef<SWidget> MakePerformanceControls();
    TSharedRef<SWidget> MakeNormalPhysicsGroup();
    TSharedRef<SWidget> MakeOverlay();

    FBallsApp* App = nullptr;
    TSharedPtr<FBallsGameplay> Gameplay;
};
