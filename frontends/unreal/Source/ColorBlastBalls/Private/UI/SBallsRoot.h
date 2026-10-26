#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_RetVal_OneParam(bool, FOnBallsKey, const FKey&);

// Full-viewport host for the current screen. Takes keyboard focus and routes keys to the app.
class SBallsRoot final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsRoot) {}
        SLATE_EVENT(FOnBallsKey, OnKey)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetScreen(TSharedRef<SWidget> Screen);

    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
    FOnBallsKey OnKey;
};
