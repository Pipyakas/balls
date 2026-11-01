#include "UI/SBallsRoot.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

namespace {

TSharedRef<SWidget> NoPopup() { return SNew(SBox).Visibility(EVisibility::Collapsed); }

}

void SBallsRoot::Construct(const FArguments& InArgs) {
    OnKey = InArgs._OnKey;
    OnFilesDropped = InArgs._OnFilesDropped;
    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot().Expose(ScreenSlot)[SNullWidget::NullWidget]
        + SOverlay::Slot().Expose(PopupSlot)[NoPopup()]
    ];
}

void SBallsRoot::SetScreen(TSharedRef<SWidget> Screen) {
    ScreenSlot->AttachWidget(Screen);
    FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

void SBallsRoot::SetPopup(TSharedPtr<SWidget> Popup) {
    PopupSlot->AttachWidget(Popup ? Popup.ToSharedRef() : NoPopup());
    FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

FReply SBallsRoot::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) {
    if (!InKeyEvent.IsRepeat() && OnKey.IsBound() && OnKey.Execute(InKeyEvent.GetKey())) {
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply SBallsRoot::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) {

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
