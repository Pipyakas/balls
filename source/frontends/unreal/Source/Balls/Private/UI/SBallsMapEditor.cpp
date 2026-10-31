#include "UI/SBallsMapEditor.h"

#include "BallsMapEditor.h"
#include "BallsPalette.h"
#include "BallsText.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateTypes.h"
#include "UI/BallsStyle.h"
#include "UI/SBallsMenus.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace {

// The SDL front end's editor panels: content 12 in from the sides and 8 from the top, controls 38 high and 8 apart,
// section titles 30 high after a 6 gap.
constexpr float PanelPadding = 12.0f;
constexpr float PanelTopPadding = 8.0f;
constexpr float ControlHeight = 38.0f;
constexpr float ControlGap = 8.0f;
constexpr float TitleHeight = 30.0f;
constexpr float TitleGap = 6.0f;
constexpr float SwatchHeight = 34.0f;
constexpr int SwatchColumns = 4;
// SDL's button label: 0.45 of the control height.
constexpr float ControlLabelSize = ControlHeight * 0.45f;

void DrawBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FSlateRect& Rect,
             const FSlateBrush* Brush, const FLinearColor& Tint) {
    FSlateDrawElement::MakeBox(Out, Layer,
                               Geometry.ToPaintGeometry(FVector2f(Rect.GetSize()),
                                                        FSlateLayoutTransform(FVector2f(Rect.GetTopLeft()))),
                               Brush, ESlateDrawEffect::None, Tint);
}

// An empty cell or slot is an outline, as in the SDL editor, so the grid reads as cells rather than one grey block.
void StrokeRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FSlateRect& Rect,
                float Thickness, const FLinearColor& Color) {
    DrawBox(Out, Layer, Geometry, FSlateRect(Rect.Left, Rect.Top, Rect.Right, Rect.Top + Thickness), BallsStyle::White(), Color);
    DrawBox(Out, Layer, Geometry, FSlateRect(Rect.Left, Rect.Bottom - Thickness, Rect.Right, Rect.Bottom), BallsStyle::White(),
            Color);
    DrawBox(Out, Layer, Geometry, FSlateRect(Rect.Left, Rect.Top, Rect.Left + Thickness, Rect.Bottom), BallsStyle::White(), Color);
    DrawBox(Out, Layer, Geometry, FSlateRect(Rect.Right - Thickness, Rect.Top, Rect.Right, Rect.Bottom), BallsStyle::White(),
            Color);
}

void DrawTextAt(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Text,
                const FVector2D& Position, const FSlateFontInfo& Font, const FLinearColor& Color) {
    const FVector2D Size = BallsStyle::Measure(Text, Font);
    FSlateDrawElement::MakeText(
        Out, Layer,
        Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position))), Text, Font,
        ESlateDrawEffect::None, Color);
}

void DrawTextInRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Text,
                    const FSlateRect& Rect, const FSlateFontInfo& Font, const FLinearColor& Color) {
    const FVector2D Size = BallsStyle::Measure(Text, Font);
    DrawTextAt(Out, Layer, Geometry, Text, Rect.GetCenter() - Size * 0.5, Font, Color);
}

FSlateRect CellRect(const FSlateRect& Grid, float Cell, int Column, int Row) {
    const float Left = Grid.Left + static_cast<float>(Column) * Cell;
    const float Top = Grid.Top + static_cast<float>(Row) * Cell;
    return FSlateRect(Left, Top, Left + Cell, Top + Cell);
}

TSharedRef<SWidget> Title(const FString& Text) {
    return SNew(SBox)
        .HeightOverride(TitleHeight)
        .VAlign(VAlign_Top)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Text))
            .Font(BallsStyle::Font(22.0f))
            .ColorAndOpacity(FSlateColor(BallsPalette::Linear(BallsPalette::Accent)))
        ];
}

// One 22-high muted line, as the SDL editor's mouse hints.
TSharedRef<SWidget> Body(const FString& Text) {
    return SNew(SBox)
        .HeightOverride(22.0f)
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Text))
            .Font(BallsStyle::Font(16.0f))
            .ColorAndOpacity(FSlateColor(BallsPalette::Linear(BallsPalette::TextMuted)))
        ];
}

TSharedRef<SWidget> Sized(TSharedRef<SWidget> Content, float Height = ControlHeight) {
    return SNew(SBox).HeightOverride(Height)[Content];
}

const FButtonStyle* SwatchStyle(BallColor Color) {
    static TArray<FButtonStyle> Styles = [] {
        TArray<FButtonStyle> Result;
        for (int Index = 0; Index < ballColorCount; ++Index) {
            const FLinearColor Base = BallsPalette::Linear(BallsPalette::Ball(ballColorFromIndex(Index)));
            FButtonStyle Style;
            Style.SetNormal(FSlateRoundedBoxBrush(Base, 0.0f));
            Style.SetHovered(FSlateRoundedBoxBrush(Base * 1.25f, 0.0f));
            Style.SetPressed(FSlateRoundedBoxBrush(Base * 0.8f, 0.0f));
            Style.SetNormalPadding(FMargin(0.0f));
            Style.SetPressedPadding(FMargin(0.0f));
            Result.Add(Style);
        }
        return Result;
    }();
    return &Styles[toIndex(Color)];
}

// Steppers change by 1, or by 10 with Shift held ("Map editor").
int32 StepSize() { return FSlateApplication::Get().GetModifierKeys().IsShiftDown() ? 10 : 1; }

TSharedRef<SWidget> Stepper(const FString& Label, int32 Min, int32 Max, TFunction<int32()> Get,
                            TFunction<void(int32)> Set) {
    constexpr float LabelShare = 0.45f;
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
    Row->AddSlot().FillWidth(LabelShare).VAlign(VAlign_Center)
    [
        SNew(STextBlock)
        .Text(FText::FromString(Label))
        .Font(BallsStyle::Font(ControlHeight * 0.5f))
        .ColorAndOpacity(FSlateColor(BallsPalette::Linear(BallsPalette::TextMuted)))
    ];
    Row->AddSlot().AutoWidth()
    [
        SNew(SBox).WidthOverride(ControlHeight)
        [
            BallsWidgets::Button(FText::FromString(TEXT("-")), [Get, Set, Min] { Set(FMath::Max(Min, Get() - StepSize())); },
                                 ControlLabelSize)
        ]
    ];
    Row->AddSlot().FillWidth(1.0f - LabelShare).VAlign(VAlign_Center)
    [
        SNew(STextBlock)
        .Text_Lambda([Get] { return FText::FromString(FString::FromInt(Get())); })
        .Font(BallsStyle::Font(ControlHeight * 0.5f))
        .ColorAndOpacity(FSlateColor(BallsPalette::Linear(BallsPalette::Text)))
        .Justification(ETextJustify::Center)
    ];
    Row->AddSlot().AutoWidth()
    [
        SNew(SBox).WidthOverride(ControlHeight)
        [
            BallsWidgets::Button(FText::FromString(TEXT("+")), [Get, Set, Max] { Set(FMath::Min(Max, Get() + StepSize())); },
                                 ControlLabelSize)
        ]
    ];
    return Row;
}

TSharedRef<SWidget> DarkPanel(TSharedRef<SWidget> Content, float Width) {
    return SNew(SBox)
        .WidthOverride(Width)
        [
            SNew(SBorder)
            .BorderImage(BallsStyle::White())
            .BorderBackgroundColor(FSlateColor(BallsPalette::Linear(BallsPalette::Panel)))
            .Padding(FMargin(PanelPadding, PanelTopPadding, PanelPadding, PanelPadding))
            [
                Content
            ]
        ];
}

class SBallsBrickGrid final : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SBallsBrickGrid) {}
        SLATE_ARGUMENT(TSharedPtr<FBallsMapEditor>, Editor)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) { Editor = InArgs._Editor; }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    bool SupportsKeyboardFocus() const override { return true; }

    FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override {
        if (Editor && Editor->HandleKey(InKeyEvent.GetKey())) {
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        if (!Editor) {
            return FReply::Unhandled();
        }
        const bool bRight = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
        const bool bLeft = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
        if (!bRight && !bLeft) {
            return FReply::Unhandled();
        }
        Mouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        const FIntPoint Cell = CellAt(MyGeometry, Mouse);
        if (Cell.X < 0) {
            return FReply::Unhandled();
        }
        Editor->BeginStroke(bRight);
        Editor->ApplyBrickCell(Cell.X, Cell.Y, bRight);
        bPainting = true;
        bErase = bRight;
        return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
    }

    FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        Mouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        if (Editor && bPainting) {
            const bool bDown = MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ||
                               MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton);
            if (!bDown) {
                Editor->EndStroke();
                bPainting = false;
            } else if (const FIntPoint Cell = CellAt(MyGeometry, Mouse); Cell.X >= 0) {
                Editor->ApplyBrickCell(Cell.X, Cell.Y, bErase);
            }
        }
        return FReply::Unhandled();
    }

    FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        if (Editor && bPainting) {
            Editor->EndStroke();
            bPainting = false;
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    void OnMouseLeave(const FPointerEvent& MouseEvent) override {
        Mouse = FVector2D(-1.0);
        if (Editor && bPainting) {
            Editor->EndStroke();
            bPainting = false;
        }
    }

    int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                  FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                  bool bParentEnabled) const override {
        if (!Editor) {
            return LayerId;
        }
        const BrickLayout& Bricks = Editor->Level().bricks;
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        DrawBox(OutDrawElements, LayerId, AllottedGeometry, FSlateRect(FVector2D::ZeroVector, Size),
                BallsStyle::RoundedBox(0.0f), BallsPalette::Linear(BallsPalette::BoardBackdrop));

        float Cell = 0.0f;
        const FSlateRect Grid = GridRect(AllottedGeometry, Cell);
        const FSlateRect BrickRows(Grid.Left, Grid.Top, Grid.Right, Grid.Top + Cell * static_cast<float>(Bricks.height));
        DrawBox(OutDrawElements, LayerId + 1, AllottedGeometry,
                FSlateRect(Grid.Left, Grid.Top, Grid.Right, Grid.Bottom),
                BallsStyle::White(), BallsPalette::Linear(BallsPalette::Shade(BallsPalette::BoardBackdrop, 0.7f)));
        DrawBox(OutDrawElements, LayerId + 1, AllottedGeometry, BrickRows, BallsStyle::White(),
                BallsPalette::Linear(BallsPalette::Panel));

        const FIntPoint Hovered = CellAt(AllottedGeometry, Mouse);
        for (int Row = 0; Row < Bricks.height; ++Row) {
            for (int Column = 0; Column < Bricks.width; ++Column) {
                const int Id = Bricks.cellId(Column, Row);
                const FSlateRect Rect = CellRect(Grid, Cell, Column, Row);
                const BrickType* Type = Id == emptyBrickId ? nullptr : Bricks.findType(Id);
                if (Type) {
                    FColor Fill = BallsPalette::Brick(Type->color);
                    if (Id == Editor->SelectedBrick()) {
                        Fill = BallsPalette::Shade(Fill, 1.35f);
                    }
                    DrawBox(OutDrawElements, LayerId + 2, AllottedGeometry, Rect, BallsStyle::RoundedBox(0.0f),
                            BallsPalette::Linear(Fill));
                    const float Border = FMath::Max(1.0f, Cell * 0.08f);
                    const FLinearColor Edge = BallsPalette::Linear(BallsPalette::Shade(Fill, 0.45f));
                    if (Column == 0 || Bricks.cellId(Column - 1, Row) != Id) {
                        DrawBox(OutDrawElements, LayerId + 3, AllottedGeometry,
                                FSlateRect(Rect.Left, Rect.Top, Rect.Left + Border, Rect.Bottom), BallsStyle::White(), Edge);
                    }
                    if (Column == Bricks.width - 1 || Bricks.cellId(Column + 1, Row) != Id) {
                        DrawBox(OutDrawElements, LayerId + 3, AllottedGeometry,
                                FSlateRect(Rect.Right - Border, Rect.Top, Rect.Right, Rect.Bottom), BallsStyle::White(), Edge);
                    }
                    if (Row == 0 || Bricks.cellId(Column, Row - 1) != Id) {
                        DrawBox(OutDrawElements, LayerId + 3, AllottedGeometry,
                                FSlateRect(Rect.Left, Rect.Top, Rect.Right, Rect.Top + Border), BallsStyle::White(), Edge);
                    }
                    if (Row == Bricks.height - 1 || Bricks.cellId(Column, Row + 1) != Id) {
                        DrawBox(OutDrawElements, LayerId + 3, AllottedGeometry,
                                FSlateRect(Rect.Left, Rect.Bottom - Border, Rect.Right, Rect.Bottom), BallsStyle::White(), Edge);
                    }
                    const bool bFirstCell = (Column == 0 || Bricks.cellId(Column - 1, Row) != Id) &&
                                            (Row == 0 || Bricks.cellId(Column, Row - 1) != Id);
                    if (Type->hits > 1 && bFirstCell && Cell >= 12.0f) {
                        DrawTextInRect(OutDrawElements, LayerId + 4, AllottedGeometry, FString::FromInt(Type->hits), Rect,
                                       BallsStyle::Font(Cell * 0.55f, true),
                                       BallsPalette::Linear(BallsPalette::Text));
                    }
                } else {
                    StrokeRect(OutDrawElements, LayerId + 2, AllottedGeometry, Rect, 1.0f,
                               BallsPalette::Linear(BallsPalette::Border));
                }
                if (Hovered.X == Column && Hovered.Y == Row) {
                    DrawBox(OutDrawElements, LayerId + 5, AllottedGeometry, Rect, BallsStyle::White(),
                            BallsPalette::Linear(BallsPalette::Accent, 0.22f));
                }
            }
        }

        DrawTextAt(OutDrawElements, LayerId + 6, AllottedGeometry,
                   BallsText::FromUtf8(Editor->FooterText()),
                   FVector2D(10.0f, Size.Y - 26.0f), BallsStyle::Font(17.0f),
                   BallsPalette::Linear(BallsPalette::TextMuted));
        return LayerId + 6;
    }

private:
    FSlateRect GridRect(const FGeometry& Geometry, float& OutCell) const {
        const BrickLayout& Bricks = Editor->Level().bricks;
        const FVector2D Size = Geometry.GetLocalSize();
        const float InnerWidth = FMath::Max(1.0f, static_cast<float>(Size.X) - 28.0f);
        const float InnerHeight = FMath::Max(1.0f, static_cast<float>(Size.Y) - 28.0f);
        OutCell = FMath::Max(2.0f, FMath::Min(InnerWidth / static_cast<float>(FMath::Max(Bricks.width, 1)),
                                               InnerHeight / static_cast<float>(FMath::Max(Bricks.playableHeight, 1))));
        const float Width = OutCell * static_cast<float>(Bricks.width);
        const float Height = OutCell * static_cast<float>(Bricks.playableHeight);
        const float Left = (static_cast<float>(Size.X) - Width) * 0.5f;
        const float Top = (static_cast<float>(Size.Y) - Height) * 0.5f;
        return FSlateRect(Left, Top, Left + Width, Top + Height);
    }

    FIntPoint CellAt(const FGeometry& Geometry, const FVector2D& Point) const {
        const BrickLayout& Bricks = Editor->Level().bricks;
        float Cell = 0.0f;
        const FSlateRect Grid = GridRect(Geometry, Cell);
        if (!Grid.ContainsPoint(Point)) {
            return FIntPoint(-1, -1);
        }
        const int Column = static_cast<int>((Point.X - Grid.Left) / Cell);
        const int Row = static_cast<int>((Point.Y - Grid.Top) / Cell);
        if (Column < 0 || Row < 0 || Column >= Bricks.width || Row >= Bricks.height) {
            return FIntPoint(-1, -1);
        }
        return FIntPoint(Column, Row);
    }

    TSharedPtr<FBallsMapEditor> Editor;
    FVector2D Mouse = FVector2D(-1.0);
    bool bPainting = false;
    bool bErase = false;
};

class SBallsLauncherGrid final : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SBallsLauncherGrid) {}
        SLATE_ARGUMENT(TSharedPtr<FBallsMapEditor>, Editor)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) { Editor = InArgs._Editor; }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    bool SupportsKeyboardFocus() const override { return true; }

    FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override {
        if (Editor && Editor->HandleKey(InKeyEvent.GetKey())) {
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        if (!Editor) {
            return FReply::Unhandled();
        }
        const bool bRight = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
        const bool bLeft = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
        if (!bRight && !bLeft) {
            return FReply::Unhandled();
        }
        const FVector2D Point = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        const FIntPoint Cell = CellAt(MyGeometry, Point);
        if (Cell.X < 0) {
            return FReply::Unhandled();
        }
        const bool bShift = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
        Editor->ApplyLauncherCell(Cell.X, Cell.Y, bRight, bRight && bShift);
        return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
    }

    FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        Mouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        return FReply::Unhandled();
    }

    void OnMouseLeave(const FPointerEvent& MouseEvent) override { Mouse = FVector2D(-1.0); }

    int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                  FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                  bool bParentEnabled) const override {
        if (!Editor) {
            return LayerId;
        }
        const LauncherLayout& Launchers = Editor->Level().launchers;
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        DrawBox(OutDrawElements, LayerId, AllottedGeometry, FSlateRect(FVector2D::ZeroVector, Size),
                BallsStyle::RoundedBox(0.0f), BallsPalette::Linear(BallsPalette::Panel));

        float Cell = 0.0f;
        const FSlateRect Grid = GridRect(AllottedGeometry, Cell);
        const FIntPoint Hovered = CellAt(AllottedGeometry, Mouse);
        for (int Row = 0; Row <= Launchers.rowCount(); ++Row) {
            for (int Column = 0; Column < Launchers.columns; ++Column) {
                const FSlateRect CellBox = CellRect(Grid, Cell, Column, Row);
                const FSlateRect Rect(CellBox.Left + 3.0f, CellBox.Top + 3.0f, CellBox.Right - 3.0f, CellBox.Bottom - 3.0f);
                const bool bExists = Row < Launchers.rowCount();
                const std::optional<Launcher> Slot = bExists ? Launchers.slot(Column, Row) : std::nullopt;
                if (Slot) {
                    DrawBox(OutDrawElements, LayerId + 1, AllottedGeometry, Rect, BallsStyle::RoundedBox(0.0f),
                            BallsPalette::Linear(BallsPalette::Ball(Slot->color)));
                    DrawTextInRect(OutDrawElements, LayerId + 2, AllottedGeometry,
                                   BallsText::CompactCount(Slot->ballCount), Rect, BallsStyle::Font(FMath::Max(10.0f, Cell * 0.36f), true),
                                   BallsPalette::Linear(BallsPalette::Text));
                } else {
                    StrokeRect(OutDrawElements, LayerId + 1, AllottedGeometry, Rect, 1.0f,
                               BallsPalette::Linear(bExists ? BallsPalette::TextDim : BallsPalette::Border));
                }
                if (Row == Launchers.visibleRows - 1) {
                    DrawBox(OutDrawElements, LayerId + 2, AllottedGeometry,
                            FSlateRect(Grid.Left, Rect.Bottom + 4.0f, Grid.Right, Rect.Bottom + 6.0f), BallsStyle::White(),
                            BallsPalette::Linear(BallsPalette::Accent, 0.6f));
                }
                if (Hovered.X == Column && Hovered.Y == Row) {
                    DrawBox(OutDrawElements, LayerId + 3, AllottedGeometry, Rect, BallsStyle::White(),
                            BallsPalette::Linear(BallsPalette::Accent, 0.25f));
                }
            }
        }

        const EditorValidation Validation = Editor->Validation();
        float X = 12.0f;
        const float Y = static_cast<float>(Size.Y) - 28.0f;
        for (int Color = 0; Color < ballColorCount; ++Color) {
            if (Validation.brickHits[Color] == 0 && Validation.balls[Color] == 0) {
                continue;
            }
            DrawBox(OutDrawElements, LayerId + 4, AllottedGeometry, FSlateRect(X, Y + 4.0f, X + 13.0f, Y + 17.0f),
                    BallsStyle::RoundedBox(0.0f), BallsPalette::Linear(BallsPalette::Ball(ballColorFromIndex(Color))));
            const BallColor TallyColor = ballColorFromIndex(Color);
            const FString Text = BallsText::FromUtf8(Editor->ColorTallyText(TallyColor));
            const FLinearColor TextColor = Editor->ColorTallyMatches(TallyColor)
                                               ? BallsPalette::Linear(BallsPalette::Success)
                                               : BallsPalette::Linear(BallsPalette::Text);
            DrawTextAt(OutDrawElements, LayerId + 4, AllottedGeometry, Text, FVector2D(X + 18.0f, Y),
                       BallsStyle::Font(17.0f), TextColor);
            X += 30.0f + static_cast<float>(BallsStyle::Measure(Text, BallsStyle::Font(17.0f)).X);
        }
        return LayerId + 4;
    }

private:
    FSlateRect GridRect(const FGeometry& Geometry, float& OutCell) const {
        const LauncherLayout& Launchers = Editor->Level().launchers;
        const FVector2D Size = Geometry.GetLocalSize();
        const float Rows = static_cast<float>(Launchers.rowCount() + 1);
        const float Columns = static_cast<float>(FMath::Max(Launchers.columns, 1));
        OutCell = FMath::Max(14.0f, FMath::Min3((static_cast<float>(Size.X) - 28.0f) / Columns,
                                                (static_cast<float>(Size.Y) - 28.0f) / Rows, 64.0f));
        const float Width = OutCell * Columns;
        const float Height = OutCell * Rows;
        const float Left = (static_cast<float>(Size.X) - Width) * 0.5f;
        return FSlateRect(Left, 14.0f, Left + Width, 14.0f + Height);
    }

    FIntPoint CellAt(const FGeometry& Geometry, const FVector2D& Point) const {
        const LauncherLayout& Launchers = Editor->Level().launchers;
        float Cell = 0.0f;
        const FSlateRect Grid = GridRect(Geometry, Cell);
        if (!Grid.ContainsPoint(Point)) {
            return FIntPoint(-1, -1);
        }
        const int Column = static_cast<int>((Point.X - Grid.Left) / Cell);
        const int Row = static_cast<int>((Point.Y - Grid.Top) / Cell);
        if (Column < 0 || Row < 0 || Column >= Launchers.columns || Row > Launchers.rowCount()) {
            return FIntPoint(-1, -1);
        }
        return FIntPoint(Column, Row);
    }

    TSharedPtr<FBallsMapEditor> Editor;
    FVector2D Mouse = FVector2D(-1.0);
};

}

void SBallsMapEditor::Construct(const FArguments& InArgs) {
    App = InArgs._App;
    Editor = InArgs._Editor;

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(BallsStyle::White())
        .BorderBackgroundColor(FSlateColor(BallsPalette::Linear(BallsPalette::Background)))
        .Padding(16.0f)
        [
            SNew(SSafeZone)
            .IsTitleSafe(true)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    MakeToolPanel()
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(16.0f, 0.0f)
                [
                    MakeCenter()
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    MakePropertyPanel()
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> SBallsMapEditor::MakeToolPanel() {
    TSharedPtr<FBallsMapEditor> E = Editor;
    auto SelectedTool = [E](FEditorTool Tool) {
        return TAttribute<bool>::CreateLambda([E, Tool] { return E->Tool() == Tool; });
    };

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Tool"))
    ];
    for (const EditorToolButton& Button : EditorSession::toolButtons()) {
        const EditorTool Tool = Button.tool;
        Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
        [
            Sized(BallsWidgets::Button(FText::FromString(BallsText::FromUtf8(Button.label)), [E, Tool] { E->SetTool(Tool); },
                                       ControlLabelSize, true, SelectedTool(Tool)))
        ];
    }

    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Color (1-7)"))
    ];
    for (int Row = 0; Row < 2; ++Row) {
        TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox);
        for (int Column = 0; Column < SwatchColumns; ++Column) {
            const int Index = Row * SwatchColumns + Column;
            const FMargin Gap(Column == 0 ? 0.0f : ControlGap * 0.5f, 0.0f, Column + 1 == SwatchColumns ? 0.0f : ControlGap * 0.5f, 0.0f);
            if (Index >= ballColorCount) {
                Swatches->AddSlot().FillWidth(1.0f).Padding(Gap)[SNullWidget::NullWidget];
                continue;
            }
            const BallColor Color = ballColorFromIndex(Index);
            Swatches->AddSlot().FillWidth(1.0f).Padding(Gap)
            [
                SNew(SBorder)
                .BorderImage(BallsStyle::White())
                .BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([E, Color] {
                    return E->BrushColor() == Color ? FSlateColor(BallsPalette::Linear(BallsPalette::TextStrong))
                                                    : FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
                }))
                .Padding(2.0f)
                [
                    SNew(SButton)
                    .ButtonStyle(SwatchStyle(Color))
                    .ContentPadding(FMargin(0.0f))
                    .OnClicked_Lambda([E, Color] {
                        E->SetBrushColor(Color);
                        return FReply::Handled();
                    })
                ]
            ];
        }
        Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
        [
            Sized(Swatches, SwatchHeight)
        ];
    }

    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Brick"))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(Stepper(TEXT("Hits"), 1, editorMaximumHits, [E] { return E->BrushHits(); }, [E](int32 Value) { E->SetBrushHits(Value); }))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        SNew(STextBlock)
        .Text_Lambda([E] {
            return FText::FromString(BallsText::FromUtf8(E->SelectionText()));
        })
        .Font(BallsStyle::Font(17.0f))
        .ColorAndOpacity(FSlateColor(BallsPalette::Linear(BallsPalette::TextMuted)))
    ];

    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Launcher"))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(Stepper(TEXT("Balls"), 1, editorMaximumLauncherBalls, [E] { return E->LauncherBalls(); },
                [E](int32 Value) { E->SetLauncherBalls(Value); }))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Body(TEXT("L-click set, R-click clear"))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Body(TEXT("Shift+R-click delete row"))
    ];
    return DarkPanel(Box, 270.0f);
}

TSharedRef<SWidget> SBallsMapEditor::MakePropertyPanel() {
    TSharedPtr<FBallsMapEditor> E = Editor;
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Level"))
    ];
    Box->AddSlot().AutoHeight()
    [
        SAssignNew(NameBox, SEditableTextBox)
        .Font(BallsStyle::Font(20.0f))
        .ForegroundColor(FSlateColor(BallsPalette::Linear(BallsPalette::Text)))
        .BackgroundColor(FSlateColor(BallsPalette::Linear(BallsPalette::Control)))
        .Padding(FMargin(8.0f, 4.0f))
        .Text(FText::FromString(BallsText::FromUtf8(E->LevelName())))
        .OnTextChanged_Lambda([E](const FText& Text) {
            const FTCHARToUTF8 Converted(*Text.ToString());
            E->SetLevelName(std::string(Converted.Get(), Converted.Length()));
        })
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            BallsWidgets::Button(FText::FromString(TEXT("Normal")), [E] { E->SetMode(GameMode::Normal); }, ControlLabelSize, true,
                                 TAttribute<bool>::CreateLambda([E] { return E->Mode() == GameMode::Normal; }))
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
        [
            BallsWidgets::Button(FText::FromString(TEXT("Perf")), [E] { E->SetMode(GameMode::Performance); }, ControlLabelSize,
                                 true,
                                 TAttribute<bool>::CreateLambda([E] { return E->Mode() == GameMode::Performance; }))
        ])
    ];

    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Board"))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(Stepper(TEXT("Width"), 1, editorMaximumBoardSize, [E] { return E->Level().bricks.width; },
                [E](int32 Value) { E->SetBoardWidth(Value); }))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(Stepper(TEXT("Brick rows"), 1, editorMaximumBoardSize, [E] { return E->Level().bricks.height; },
                [E](int32 Value) { E->SetBrickRows(Value); }))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(Stepper(TEXT("Total rows"), 1, editorMaximumTotalRows, [E] { return E->Level().bricks.playableHeight; },
                [E](int32 Value) { E->SetPlayableRows(Value); }))
    ];

    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Launchers"))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(Stepper(TEXT("Columns"), 1, editorMaximumLauncherColumns, [E] { return E->Level().launchers.columns; },
                [E](int32 Value) { E->SetLauncherColumns(Value); }))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(Stepper(TEXT("Visible rows"), 1, editorMaximumVisibleRows, [E] { return E->Level().launchers.visibleRows; },
                [E](int32 Value) { E->SetVisibleRows(Value); }))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(BallsWidgets::Button(FText::FromString(TEXT("Auto-fill to match bricks")), [E] { E->AutoFillLaunchers(); }, ControlLabelSize))
    ];

    Box->AddSlot().AutoHeight().Padding(0.0f, TitleGap, 0.0f, 0.0f)
    [
        Title(TEXT("Validation"))
    ];
    Box->AddSlot().AutoHeight()
    [
        SNew(STextBlock)
        .Font(BallsStyle::Font(17.0f))
        .AutoWrapText(true)
        .Text_Lambda([E] {
            const EditorValidation Validation = E->Validation();
            if (Validation.isPlayable()) {
                return FText::FromString(TEXT("Ready to play"));
            }
            FString Joined;
            for (const std::string& Problem : Validation.problems) {
                if (!Joined.IsEmpty()) {
                    Joined += TEXT("\n");
                }
                Joined += BallsText::FromUtf8(Problem);
            }
            return FText::FromString(Joined);
        })
        .ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([E] {
            return E->Validation().isPlayable() ? FSlateColor(BallsPalette::Linear(BallsPalette::Success))
                                                : FSlateColor(BallsPalette::Linear(BallsPalette::Danger));
        }))
    ];

    // Undo / Redo, Save, Save & Play and Back stay at the bottom of the panel, under the part that scrolls.
    const TSharedRef<SVerticalBox> Scrolling = Box;
    Box = SNew(SVerticalBox);
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            BallsWidgets::Button(FText::FromString(TEXT("Undo")), [E] { E->Undo(); }, ControlLabelSize,
                                 TAttribute<bool>::CreateLambda([E] { return E->CanUndo(); }))
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
        [
            BallsWidgets::Button(FText::FromString(TEXT("Redo")), [E] { E->Redo(); }, ControlLabelSize,
                                 TAttribute<bool>::CreateLambda([E] { return E->CanRedo(); }))
        ])
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(BallsWidgets::Button(TAttribute<FText>::CreateLambda([E] {
                                 return FText::FromString(BallsText::FromUtf8(E->SaveLabel()));
                             }),
                             [E] { E->Save(); }, ControlLabelSize))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(BallsWidgets::Button(FText::FromString(TEXT("Save & Play")), [E] { E->SaveAndPlay(); }, ControlLabelSize,
                             TAttribute<bool>::CreateLambda([E] { return E->Validation().isPlayable(); })))
    ];
    Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap)
    [
        Sized(BallsWidgets::Button(TAttribute<FText>::CreateLambda([E] {
                                 return FText::FromString(BallsText::FromUtf8(E->BackLabel()));
                             }),
                             [E] { E->Back(); }, ControlLabelSize))
    ];

    return DarkPanel(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1.0f)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                Scrolling
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, ControlGap, 0.0f, 0.0f)
        [
            Box
        ],
        330.0f);
}

TSharedRef<SWidget> SBallsMapEditor::MakeCenter() {
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(0.66f)
        [
            SNew(SBallsBrickGrid).Editor(Editor)
        ]
        + SVerticalBox::Slot().FillHeight(0.34f).Padding(0.0f, 10.0f, 0.0f, 0.0f)
        [
            SNew(SBallsLauncherGrid).Editor(Editor)
        ];
}

int32 SBallsMapEditor::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                               FSlateWindowElementList& OutDrawElements, int32 LayerId,
                               const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const {
    int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
                                           InWidgetStyle, bParentEnabled);
    if (!Editor || !Editor->HasMessage()) {
        return Layer;
    }
    const FString Text = BallsText::FromUtf8(Editor->Message());
    const FSlateFontInfo Font = BallsStyle::Font(22.0f, true);
    const FVector2D Extent = BallsStyle::Measure(Text, Font);
    const FVector2D Size = AllottedGeometry.GetLocalSize();
    const float Alpha = FMath::Min(1.0f, Editor->MessageSeconds());
    const FSlateRect Rect((Size.X - Extent.X) * 0.5f - 18.0f, 18.0f,
                          (Size.X + Extent.X) * 0.5f + 18.0f, 62.0f);
    DrawBox(OutDrawElements, Layer + 1, AllottedGeometry, Rect, BallsStyle::RoundedBox(0.0f),
            BallsPalette::Linear(Editor->MessageIsError() ? BallsPalette::Danger : BallsPalette::Success, 0.85f * Alpha));
    DrawTextInRect(OutDrawElements, Layer + 2, AllottedGeometry, Text, Rect, Font,
                   BallsPalette::Linear(BallsPalette::Text, Alpha));
    return Layer + 2;
}
