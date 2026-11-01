#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FBallsApp;
class SVerticalBox;
class STextBlock;

class SBallsBenchmark final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsBenchmark) {}
        SLATE_ARGUMENT(FBallsApp*, App)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void Refresh();

private:
    EActiveTimerReturnType Poll(double CurrentTime, float DeltaTime);
    FString Signature() const;

    FBallsApp* App = nullptr;
    TSharedPtr<SVerticalBox> Rows;
    TSharedPtr<STextBlock> Footer;
    FString ShownSignature;
};
