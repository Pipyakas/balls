#include "UI/SBallsRoot.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"

void SBallsRoot::Construct(const FArguments& InArgs) {
    OnKey = InArgs._OnKey;
    OnFilesDropped = InArgs._OnFilesDropped;
}

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

FReply SBallsRoot::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) {
    const TSharedPtr<FExternalDragOperation> Operation = DragDropEvent.GetOperationAs<FExternalDragOperation>();
    return Operation && Operation->HasFiles() ? FReply::Handled() : FReply::Unhandled();
}

FReply SBallsRoot::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) {
    const TSharedPtr<FExternalDragOperation> Operation = DragDropEvent.GetOperationAs<FExternalDragOperation>();
    if (!Operation || !Operation->HasFiles() || !OnFilesDropped.IsBound()) {
        return FReply::Unhandled();
    }
    OnFilesDropped.Execute(Operation->GetFiles());
    return FReply::Handled();
}
