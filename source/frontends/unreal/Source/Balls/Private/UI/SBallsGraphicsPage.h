#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FBallsApp;

class SBallsGraphicsPage final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsGraphicsPage) {}
        SLATE_ARGUMENT(FBallsApp*, App)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
