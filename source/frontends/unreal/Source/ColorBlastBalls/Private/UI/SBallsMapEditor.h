#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FBallsApp;
class FBallsMapEditor;
class SEditableTextBox;

// Slate screen for the shared LevelEditor. The panels are Slate widgets; the brick and launcher grids
// are custom-painted leaf widgets in the centre column.
class SBallsMapEditor final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBallsMapEditor) {}
        SLATE_ARGUMENT(FBallsApp*, App)
        SLATE_ARGUMENT(TSharedPtr<FBallsMapEditor>, Editor)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                  FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                  bool bParentEnabled) const override;

private:
    TSharedRef<SWidget> MakeToolPanel();
    TSharedRef<SWidget> MakePropertyPanel();
    TSharedRef<SWidget> MakeCenter();

    FBallsApp* App = nullptr;
    TSharedPtr<FBallsMapEditor> Editor;
    TSharedPtr<SEditableTextBox> NameBox;
};
