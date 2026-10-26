#include "UI/SBallsRoot.h"

#include "Framework/Application/SlateApplication.h"

void SBallsRoot::Construct(const FArguments& InArgs) { OnKey = InArgs._OnKey; }

void SBallsRoot::SetScreen(TSharedRef<SWidget> Screen) {
    ChildSlot[Screen];
    FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

FReply SBallsRoot::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) {
    if (!InKeyEvent.IsRepeat() && OnKey.IsBound() && OnKey.Execute(InKeyEvent.GetKey())) {
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply SBallsRoot::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) {
    // Clicks on empty space keep focus here so shortcuts keep working.
    return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}
