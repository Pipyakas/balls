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

// A full-width row showing the current value; clicking opens a vertical list under it (inline, so a scrolling
// gadget just grows). Choosing an entry or clicking the row again closes it.
TSharedRef<SWidget> Dropdown(TArray<FString> Options, TFunction<int32()> GetIndex, TFunction<void(int32)> OnSelect,
                             float ControlHeight = 44.0f, float FontSize = 20.0f);

// "-  value  +": Shift makes a click step by 10. Limits are inclusive.
TSharedRef<SWidget> Stepper(TFunction<int32()> GetValue, TFunction<void(int32)> OnChange, int32 Minimum, int32 Maximum,
                            float ControlHeight = 44.0f, float FontSize = 20.0f);

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

// The Settings options, shared by the Settings page (main menu) and the pause overlay. Changes apply immediately.
class SBallsSettingsPanel final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsSettingsPanel) {}
        SLATE_ARGUMENT(FBallsApp*, App)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};

class SBallsSettingsPage final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsSettingsPage) {}
        SLATE_ARGUMENT(FBallsApp*, App)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
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
