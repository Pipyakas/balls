#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

DECLARE_DELEGATE_RetVal_OneParam(bool, FOnBallsKey, const FKey&);
DECLARE_DELEGATE_OneParam(FOnBallsFilesDropped, const TArray<FString>&);

// Full-viewport host for the current screen. Takes keyboard focus and routes keys to the app.
class SBallsRoot final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsRoot) {}
        SLATE_EVENT(FOnBallsKey, OnKey)
        SLATE_EVENT(FOnBallsFilesDropped, OnFilesDropped)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetScreen(TSharedRef<SWidget> Screen);
    // A page shown over the screen (the Graphics page), or none.
    void SetPopup(TSharedPtr<SWidget> Popup);

    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    // Files dropped on the window go to the level importer.
    FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
    FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
    // The screen, and the popup over it.
    SOverlay::FOverlaySlot* ScreenSlot = nullptr;
    SOverlay::FOverlaySlot* PopupSlot = nullptr;
    FOnBallsKey OnKey;
    FOnBallsFilesDropped OnFilesDropped;
};
