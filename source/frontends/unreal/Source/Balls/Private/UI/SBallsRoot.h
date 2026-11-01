#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

DECLARE_DELEGATE_RetVal_OneParam(bool, FOnBallsKey, const FKey&);
DECLARE_DELEGATE_OneParam(FOnBallsFilesDropped, const TArray<FString>&);

class SBallsRoot final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsRoot) {}
        SLATE_EVENT(FOnBallsKey, OnKey)
        SLATE_EVENT(FOnBallsFilesDropped, OnFilesDropped)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetScreen(TSharedRef<SWidget> Screen);

    void SetPopup(TSharedPtr<SWidget> Popup);

    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

    FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
    FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:

    SOverlay::FOverlaySlot* ScreenSlot = nullptr;
    SOverlay::FOverlaySlot* PopupSlot = nullptr;
    FOnBallsKey OnKey;
    FOnBallsFilesDropped OnFilesDropped;
};
