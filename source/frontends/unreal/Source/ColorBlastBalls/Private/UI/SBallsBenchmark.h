#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FBallsApp;
class SVerticalBox;
class STextBlock;

// "First launch: machine benchmark": one row per backend with a progress bar while it runs, then its score.
// The winner is highlighted, and the main menu opens 1.5 s after the last row finishes or on Continue.
class SBallsBenchmark final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsBenchmark) {}
        SLATE_ARGUMENT(FBallsApp*, App)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    // Rows are rebuilt only when a status or a score changed, so the widget can be refreshed from a timer.
    void Refresh();

private:
    EActiveTimerReturnType Poll(double CurrentTime, float DeltaTime);
    FString Signature() const;

    FBallsApp* App = nullptr;
    TSharedPtr<SVerticalBox> Rows;
    TSharedPtr<STextBlock> Footer;
    FString ShownSignature;
};
