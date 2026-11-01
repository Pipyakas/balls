#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include <vector>

class FBallsApp;

namespace BallsWidgets {

TSharedRef<SWidget> Button(TAttribute<FText> Label, TFunction<void()> OnClicked, float FontSize = 22.0f,
                           TAttribute<bool> bEnabled = true, TAttribute<bool> bSelected = false);

TSharedRef<SWidget> Dropdown(TArray<FString> Options, TFunction<int32()> GetIndex, TFunction<void(int32)> OnSelect,
                             float ControlHeight = 44.0f, float FontSize = 20.0f);

enum class EChevron { Right, Down, Up };
TSharedRef<SWidget> Chevron(TAttribute<EChevron> Direction, TAttribute<FLinearColor> Color);

TSharedRef<SWidget> FrameTimingsStrip(const FrameTimings& Timings);

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

class SBallsSettingsPanel final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsSettingsPanel)
        : _bAllowBenchmark(true) {}
        SLATE_ARGUMENT(FBallsApp*, App)

        SLATE_ARGUMENT(bool, bAllowBenchmark)
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

class SScrollBox;

class SBallsLevelSelect final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsLevelSelect) {}
        SLATE_ARGUMENT(FBallsApp*, App)
        SLATE_ARGUMENT(GameMode, Mode)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh();
    void Highlight(const FString& LevelName);

private:
    EActiveTimerReturnType Poll(double CurrentTime, float DeltaTime);
    void RebuildList();

    FBallsApp* App = nullptr;
    GameMode Mode = GameMode::Normal;
    TSharedPtr<SScrollBox> List;

    std::string BuiltRows;
};
