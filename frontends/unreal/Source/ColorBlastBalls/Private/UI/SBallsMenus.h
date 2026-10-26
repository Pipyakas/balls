#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include <vector>

class FBallsApp;

namespace BallsWidgets {

// Themed button; Selected tints it with the accent color like the raylib toggles.
TSharedRef<SWidget> Button(TAttribute<FText> Label, TFunction<void()> OnClicked, float FontSize = 22.0f,
                           TAttribute<bool> bEnabled = true, TAttribute<bool> bSelected = false);

}

class SBallsMainMenu final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsMainMenu) {}
        SLATE_ARGUMENT(FBallsApp*, App)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                  FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                  bool bParentEnabled) const override;

private:
    FBallsApp* App = nullptr;
};

class SBallsLevelSelect final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsLevelSelect) {}
        SLATE_ARGUMENT(FBallsApp*, App)
        SLATE_ARGUMENT(GameMode, Mode)
        SLATE_ARGUMENT(std::vector<LevelFiles>, Levels)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
