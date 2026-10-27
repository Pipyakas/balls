#include "UI/SBallsGameplayHud.h"

#include "BallsApp.h"
#include "BallsGameplay.h"
#include "BallsPalette.h"
#include "BallsText.h"
#include "Rendering/DrawElements.h"
#include "UI/BallsStyle.h"
#include "UI/SBallsMenus.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace {

constexpr float ControlRowHeight = 44.0f;
constexpr float ControlGap = 10.0f;
constexpr float OverlayWidth = 420.0f;
constexpr float OverlayButtonHeight = 60.0f;
constexpr float LauncherGapShare = 0.18f;
constexpr float MaximumLauncherSize = 200.0f;
constexpr float BackRowFade = 0.55f;
constexpr float MinimumLabelPixels = 9.0f;
// A touch that moves further than this (Slate units) scrolls the launcher row instead of picking a launcher.
constexpr float DragThreshold = 8.0f;

enum class EAlign { Left, Center, Right };

FSlateRect Shrink(const FSlateRect& Rect, float Amount) {
    return FSlateRect(Rect.Left + Amount, Rect.Top + Amount, Rect.Right - Amount, Rect.Bottom - Amount);
}

FPaintGeometry RectGeometry(const FGeometry& Geometry, const FSlateRect& Rect) {
    return Geometry.ToPaintGeometry(FVector2f(Rect.GetSize()), FSlateLayoutTransform(FVector2f(Rect.GetTopLeft())));
}

void DrawBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FSlateRect& Rect,
             const FSlateBrush* Brush, FLinearColor Tint) {
    FSlateDrawElement::MakeBox(Out, Layer, RectGeometry(Geometry, Rect), Brush, ESlateDrawEffect::None, Tint);
}

void DrawText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Text,
              FVector2D Position, const FSlateFontInfo& Font, FLinearColor Color, EAlign Align = EAlign::Left) {
    const FVector2D Size = BallsStyle::Measure(Text, Font);
    if (Align == EAlign::Center) {
        Position.X -= Size.X * 0.5;
    } else if (Align == EAlign::Right) {
        Position.X -= Size.X;
    }
    FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position))),
                                Text, Font, ESlateDrawEffect::None, Color);
}

void DrawTextInRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& Text,
                    const FSlateRect& Rect, const FSlateFontInfo& Font, FLinearColor Color) {
    const FVector2D Size = BallsStyle::Measure(Text, Font);
    const FVector2D Position = Rect.GetCenter() - Size * 0.5;
    FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position))),
                                Text, Font, ESlateDrawEffect::None, Color);
}

struct FLauncherGrid {
    FVector2D Origin;
    float SlotSize = 0.0f;
    float Gap = 0.0f;

    FSlateRect SlotRect(int32 Column, float Row) const {
        const float Left = static_cast<float>(Origin.X) + static_cast<float>(Column) * (SlotSize + Gap);
        const float Top = static_cast<float>(Origin.Y) + Row * (SlotSize + Gap);
        return FSlateRect(Left, Top, Left + SlotSize, Top + SlotSize);
    }
};

FLauncherGrid ComputeLauncherGrid(const FSlateRect& Area, int32 Columns, int32 VisibleRows) {
    const float SafeColumns = static_cast<float>(FMath::Max(Columns, 1));
    const float SafeRows = static_cast<float>(FMath::Max(VisibleRows, 1));
    const FVector2D AreaSize = Area.GetSize();
    const float ByWidth = static_cast<float>(AreaSize.X) / (SafeColumns + (SafeColumns + 1.0f) * LauncherGapShare);
    const float ByHeight = static_cast<float>(AreaSize.Y) / (SafeRows + (SafeRows + 1.0f) * LauncherGapShare);
    FLauncherGrid Grid;
    Grid.SlotSize = FMath::Min3(ByWidth, ByHeight, MaximumLauncherSize);
    Grid.Gap = Grid.SlotSize * LauncherGapShare;
    const float GridWidth = SafeColumns * Grid.SlotSize + (SafeColumns - 1.0f) * Grid.Gap;
    const float GridHeight = SafeRows * Grid.SlotSize + (SafeRows - 1.0f) * Grid.Gap;
    Grid.Origin = FVector2D(Area.Left + (AreaSize.X - GridWidth) * 0.5, Area.Top + (AreaSize.Y - GridHeight) * 0.5);
    return Grid;
}

struct FPortraitLauncherSlot {
    const QueuedLauncher* Queued = nullptr;
    int32 Column = 0;
    int32 Row = 0;
    FSlateRect Rect;
};

struct FPortraitLauncherGrid {
    TArray<FPortraitLauncherSlot> Slots;
    float SlotSize = 0.0f;
    float Gap = 0.0f;
    // How far the row can scroll sideways; 0 when every launcher fits.
    float MaximumScroll = 0.0f;
};

// Portrait puts the launchers in one compact row at the bottom, regardless of the queue's own column/row
// layout. Buttons keep a fixed size and the row scrolls sideways when it is wider than the panel.
FPortraitLauncherGrid ComputePortraitLauncherGrid(const LauncherQueue& Queue, const FSlateRect& Area, float Scroll) {
    FPortraitLauncherGrid Grid;
    for (int32 Column = 0; Column < Queue.columnCount(); ++Column) {
        for (int32 Row = 0; Row < Queue.visibleRows(); ++Row) {
            if (const QueuedLauncher* Queued = Queue.visible(Column, Row)) {
                FPortraitLauncherSlot Slot;
                Slot.Queued = Queued;
                Slot.Column = Column;
                Slot.Row = Row;
                Grid.Slots.Add(Slot);
            }
        }
    }
    const int32 Count = Grid.Slots.Num();
    const FVector2D Size = Area.GetSize();
    // Area is the panel shrunk by the 12-unit margin on each side.
    Grid.SlotSize = FMath::Min(PortraitLauncherSize(static_cast<float>(Size.X) + 24.0f), static_cast<float>(Size.Y));
    Grid.Gap = Grid.SlotSize * LauncherGapShare;
    const float RowWidth = Count * Grid.SlotSize + FMath::Max(Count - 1, 0) * Grid.Gap;
    Grid.MaximumScroll = FMath::Max(0.0f, RowWidth - static_cast<float>(Size.X));
    const float Offset = RowWidth <= Size.X ? (static_cast<float>(Size.X) - RowWidth) * 0.5f
                                            : -FMath::Clamp(Scroll, 0.0f, Grid.MaximumScroll);
    const float Top = Area.Top + (static_cast<float>(Size.Y) - Grid.SlotSize) * 0.5f;
    for (int32 Index = 0; Index < Count; ++Index) {
        const float Left = Area.Left + Offset + Index * (Grid.SlotSize + Grid.Gap);
        Grid.Slots[Index].Rect = FSlateRect(Left, Top, Left + Grid.SlotSize, Top + Grid.SlotSize);
    }
    return Grid;
}

FSlateRect BrickLabelArea(const Brick& Piece, const FBallsGameplayLayout& Layout) {
    int32 MinColumn = Piece.cells.front().column;
    int32 MaxColumn = MinColumn;
    int32 MinRow = Piece.cells.front().row;
    int32 MaxRow = MinRow;
    float SumColumn = 0.0f;
    float SumRow = 0.0f;
    for (const BrickCell& Cell : Piece.cells) {
        MinColumn = FMath::Min(MinColumn, Cell.column);
        MaxColumn = FMath::Max(MaxColumn, Cell.column);
        MinRow = FMath::Min(MinRow, Cell.row);
        MaxRow = FMath::Max(MaxRow, Cell.row);
        SumColumn += static_cast<float>(Cell.column) + 0.5f;
        SumRow += static_cast<float>(Cell.row) + 0.5f;
    }
    const float Rise = Piece.riseOffset;
    const int32 BoxArea = (MaxColumn - MinColumn + 1) * (MaxRow - MinRow + 1);
    if (BoxArea == static_cast<int32>(Piece.cells.size())) {
        const FVector2D TopLeft = Layout.BoardToScreen(static_cast<float>(MinColumn), static_cast<float>(MinRow) + Rise);
        const FVector2D BottomRight =
            Layout.BoardToScreen(static_cast<float>(MaxColumn + 1), static_cast<float>(MaxRow + 1) + Rise);
        return FSlateRect(TopLeft, BottomRight);
    }
    // Irregular shapes: label the cell nearest the centroid so the number sits on the brick.
    const float Count = static_cast<float>(Piece.cells.size());
    const float CentroidX = SumColumn / Count;
    const float CentroidY = SumRow / Count;
    const BrickCell* Nearest = &Piece.cells.front();
    float NearestDistance = TNumericLimits<float>::Max();
    for (const BrickCell& Cell : Piece.cells) {
        const float Dx = static_cast<float>(Cell.column) + 0.5f - CentroidX;
        const float Dy = static_cast<float>(Cell.row) + 0.5f - CentroidY;
        if (Dx * Dx + Dy * Dy < NearestDistance) {
            NearestDistance = Dx * Dx + Dy * Dy;
            Nearest = &Cell;
        }
    }
    const FVector2D TopLeft = Layout.BoardToScreen(static_cast<float>(Nearest->column), static_cast<float>(Nearest->row) + Rise);
    return FSlateRect(TopLeft, TopLeft + FVector2D(Layout.CellSize));
}

FLinearColor FpsColor(double Fps) {
    return BallsPalette::Linear(Fps >= 60.0 ? BallsPalette::Success : BallsPalette::Danger);
}

class FPanelWriter {
public:
    FPanelWriter(FSlateWindowElementList& InOut, int32 InLayer, const FGeometry& InGeometry, const FSlateRect& InArea,
                 float InLineHeight = 34.0f)
        : Out(InOut), Layer(InLayer), Geometry(InGeometry), Area(InArea), Cursor(InArea.Top + Padding),
          LineHeight(InLineHeight) {}

    void Heading(const FString& Text) {
        Cursor += 6.0f;
        DrawText(Out, Layer, Geometry, Text, FVector2D(Area.Left + Padding, Cursor), BallsStyle::Font(24.0f, true),
                 BallsPalette::Linear(BallsPalette::Accent));
        Cursor += LineHeight + 4.0f;
    }

    void Row(const FString& Label, const FString& Value, FLinearColor ValueColor = BallsPalette::Linear(BallsPalette::Text)) {
        DrawText(Out, Layer, Geometry, Label, FVector2D(Area.Left + Padding, Cursor), BallsStyle::Font(TextSize),
                 BallsPalette::Linear(BallsPalette::TextMuted));
        DrawText(Out, Layer, Geometry, Value, FVector2D(Area.Right - Padding, Cursor), BallsStyle::Font(TextSize),
                 ValueColor, EAlign::Right);
        Cursor += LineHeight;
    }

    void ColorRow(int32 ColorIndex, const FString& Value) {
        constexpr float Swatch = 14.0f;
        const float SwatchTop = Cursor + (TextSize - Swatch) * 0.5f + 2.0f;
        DrawBox(Out, Layer, Geometry, FSlateRect(Area.Left + Padding, SwatchTop, Area.Left + Padding + Swatch, SwatchTop + Swatch),
                BallsStyle::RoundedBox(0.0f), BallsPalette::Linear(BallsPalette::Ball(ColorIndex)));
        DrawText(Out, Layer, Geometry, BallsText::FromUtf8(ballColorName(ballColorFromIndex(ColorIndex))),
                 FVector2D(Area.Left + Padding + Swatch + 8.0f, Cursor), BallsStyle::Font(TextSize),
                 BallsPalette::Linear(BallsPalette::TextMuted));
        DrawText(Out, Layer, Geometry, Value, FVector2D(Area.Right - Padding, Cursor), BallsStyle::Font(TextSize),
                 BallsPalette::Linear(BallsPalette::Text), EAlign::Right);
        Cursor += LineHeight;
    }

private:
    static constexpr float TextSize = 20.0f;
    static constexpr float Padding = 20.0f;

    FSlateWindowElementList& Out;
    int32 Layer;
    const FGeometry& Geometry;
    FSlateRect Area;
    float Cursor;
    float LineHeight;
};

// Custom-painted parts of the HUD, plus launcher clicks.
class SBallsGameplayCanvas final : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SBallsGameplayCanvas) {}
        SLATE_ARGUMENT(FBallsApp*, App)
        SLATE_ARGUMENT(TSharedPtr<FBallsGameplay>, Gameplay)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) {
        App = InArgs._App;
        Gameplay = InArgs._Gameplay;
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

    FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        const FVector2D Point = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        Mouse = Point;
        if (!bPressed) {
            return FReply::Unhandled();
        }
        // A sideways drag scrolls the launcher row; the press then no longer picks a launcher.
        if (!bDragging && FMath::Abs(Point.X - PressPoint.X) > DragThreshold) {
            bDragging = true;
        }
        if (bDragging) {
            Scroll = FMath::Clamp(Scroll - static_cast<float>(Point.X - LastPoint.X), 0.0f, MaximumScroll(MyGeometry));
        }
        LastPoint = Point;
        return FReply::Handled();
    }

    void OnMouseLeave(const FPointerEvent& MouseEvent) override { Mouse = FVector2D(-1.0); }

    FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        GameSession* Session = Gameplay->GetSession();
        if (!Session || Gameplay->GetMode() != GameMode::Normal || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) {
            return FReply::Unhandled();
        }
        const FVector2D Point = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        const FBallsGameplayLayout Layout = LayoutFor(MyGeometry);
        if (Layout.bPortrait) {
            // Launchers fire on release so a drag can scroll the row first. Only one touch drives the row; a
            // second finger must not pick another launcher.
            if (bPressed || !Layout.InteractionSection.ContainsPoint(Point)) {
                return FReply::Unhandled();
            }
            bPressed = true;
            bDragging = false;
            PressPoint = Point;
            LastPoint = Point;
            return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        int32 Column = -1;
        int32 Row = -1;
        const bool bHit = FindLauncher(MyGeometry, Point, Column, Row);
        UE_LOG(LogTemp, Verbose, TEXT("BallsHud: click %.0f,%.0f hit %d at %d,%d"), Point.X, Point.Y, bHit, Column, Row);
        if (bHit) {
            Gameplay->ChooseLauncher(Column, Row);
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override {
        if (!bPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) {
            return FReply::Unhandled();
        }
        bPressed = false;
        const FVector2D Point = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        if (!bDragging) {
            int32 Column = -1;
            int32 Row = -1;
            const bool bHit = FindLauncher(MyGeometry, Point, Column, Row);
            UE_LOG(LogTemp, Verbose, TEXT("BallsHud: tap %.0f,%.0f hit %d at %d,%d"), Point.X, Point.Y, bHit, Column, Row);
            if (bHit) {
                Gameplay->ChooseLauncher(Column, Row);
            }
        }
        bDragging = false;
        // A finger leaves no hover; keeping the position would highlight whichever launcher slides under it.
        if (MouseEvent.IsTouchEvent()) {
            Mouse = FVector2D(-1.0);
        }
        return FReply::Handled().ReleaseMouseCapture();
    }

    int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
                  FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& WidgetStyle,
                  bool bParentEnabled) const override {
        GameSession* Session = Gameplay->GetSession();
        const FVector2D Size = Geometry.GetLocalSize();
        if (!Session) {
            DrawBox(Out, LayerId, Geometry, FSlateRect(FVector2D::ZeroVector, Size), BallsStyle::White(),
                    BallsPalette::Linear(BallsPalette::Background));
            DrawText(Out, LayerId + 1, Geometry, TEXT("Level failed to load"), FVector2D(Size.X * 0.5, 200.0),
                     BallsStyle::Font(40.0f, true), BallsPalette::Linear(BallsPalette::Danger), EAlign::Center);
            DrawText(Out, LayerId + 1, Geometry, Gameplay->GetLoadError(), FVector2D(Size.X * 0.5, 260.0),
                     BallsStyle::Font(22.0f), BallsPalette::Linear(BallsPalette::Text), EAlign::Center);
            DrawText(Out, LayerId + 1, Geometry, TEXT("Fix the file and save - it reloads automatically"),
                     FVector2D(Size.X * 0.5, 300.0), BallsStyle::Font(22.0f),
                     BallsPalette::Linear(BallsPalette::TextMuted), EAlign::Center);
            PaintToast(Out, LayerId + 2, Geometry);
            return LayerId + 2;
        }

        const Simulation& Sim = Session->simulation();
        const FBallsGameplayLayout Layout = LayoutFor(Geometry);
        const FLinearColor PanelTint = BallsPalette::Linear(BallsPalette::Panel, 0.97f);

        if (Layout.bPortrait) {
            DrawBox(Out, LayerId, Geometry, Layout.Header, BallsStyle::RoundedBox(0.0f), PanelTint);
            DrawText(Out, LayerId + 1, Geometry,
                     Gameplay->GetMode() == GameMode::Normal ? TEXT("Normal Mode") : TEXT("Performance Mode"),
                     FVector2D(Layout.Header.Left + 20.0f, Layout.Header.Top + 7.0f), BallsStyle::Font(15.0f),
                     BallsPalette::Linear(BallsPalette::TextMuted));
            DrawText(Out, LayerId + 1, Geometry, BallsText::FromUtf8(Gameplay->GetFiles().name),
                     FVector2D(Layout.Header.Left + 20.0f, Layout.Header.Top + 25.0f), BallsStyle::Font(24.0f, true),
                     BallsPalette::Linear(BallsPalette::Text));
            PaintStatsStrip(Out, LayerId + 1, Geometry, *Session, Layout.Header);
            DrawBox(Out, LayerId, Geometry, Layout.InteractionSection, BallsStyle::RoundedBox(0.0f), PanelTint);
            if (Gameplay->GetMode() == GameMode::Normal) {
                PaintLaunchers(Out, LayerId + 1, Geometry, *Session, Layout);
            }
            PaintBrickLabels(Out, LayerId + 1, Geometry, Sim.board(), Layout);
            PaintToast(Out, LayerId + 3, Geometry);
            return LayerId + 3;
        }

        const FSlateRect LeftColumn(Layout.Header.Left, Layout.Header.Top, Layout.Header.Right,
                                    Layout.InteractionSection.Bottom);

        DrawBox(Out, LayerId, Geometry, LeftColumn, BallsStyle::RoundedBox(0.0f), PanelTint);
        DrawText(Out, LayerId + 1, Geometry,
                 Gameplay->GetMode() == GameMode::Normal ? TEXT("Normal Mode") : TEXT("Performance Mode"),
                 FVector2D(Layout.Header.Left + 20.0f, Layout.Header.Top + 12.0f), BallsStyle::Font(20.0f),
                 BallsPalette::Linear(BallsPalette::TextMuted));
        DrawText(Out, LayerId + 1, Geometry, BallsText::FromUtf8(Gameplay->GetFiles().name),
                 FVector2D(Layout.Header.Left + 20.0f, Layout.Header.Top + 42.0f), BallsStyle::Font(28.0f, true),
                 BallsPalette::Linear(BallsPalette::Text));
        DrawBox(Out, LayerId + 1, Geometry,
                FSlateRect(Layout.Header.Left + 20.0f, Layout.Header.Bottom - 1.0f, Layout.Header.Right - 20.0f,
                           Layout.Header.Bottom + 1.0f),
                BallsStyle::RoundedBox(0.0f), BallsPalette::Linear(BallsPalette::Frame));

        PaintBrickLabels(Out, LayerId + 1, Geometry, Sim.board(), Layout);

        if (Gameplay->GetMode() == GameMode::Normal) {
            DrawText(Out, LayerId + 1, Geometry, TEXT("Launchers"),
                     FVector2D(Layout.InteractionSection.Left + 20.0f, Layout.InteractionSection.Top + 16.0f),
                     BallsStyle::Font(20.0f, true), BallsPalette::Linear(BallsPalette::TextMuted));
            PaintLaunchers(Out, LayerId + 1, Geometry, *Session, Layout);
        }

        DrawBox(Out, LayerId, Geometry, Layout.DebugSection, BallsStyle::RoundedBox(0.0f), PanelTint);
        PaintDebugPanel(Out, LayerId + 1, Geometry, *Session, Layout.DebugSection, Layout.bPortrait);

        PaintToast(Out, LayerId + 3, Geometry);
        return LayerId + 3;
    }

private:
    FBallsGameplayLayout LayoutFor(const FGeometry& Geometry) const {
        const Simulation& Sim = Gameplay->GetSession()->simulation();
        return ComputeGameplayLayout(Geometry.GetLocalSize(), Sim.board().columns(), Sim.board().rows(),
                                     Geometry.GetAccumulatedLayoutTransform().GetScale(),
                                     Gameplay->GetMode() == GameMode::Normal);
    }

    float MaximumScroll(const FGeometry& Geometry) const {
        const FBallsGameplayLayout Layout = LayoutFor(Geometry);
        return ComputePortraitLauncherGrid(Gameplay->GetSession()->launchers(), Shrink(Layout.InteractionSection, 12.0f),
                                           0.0f)
            .MaximumScroll;
    }

    bool FindLauncher(const FGeometry& Geometry, FVector2D Point, int32& OutColumn, int32& OutRow) const {
        GameSession* Session = Gameplay->GetSession();
        const FBallsGameplayLayout Layout = LayoutFor(Geometry);
        if (!Layout.InteractionSection.ContainsPoint(Point)) {
            return false;
        }
        const LauncherQueue& Queue = Session->launchers();
        if (Layout.bPortrait) {
            const FPortraitLauncherGrid Portrait =
                ComputePortraitLauncherGrid(Queue, Shrink(Layout.InteractionSection, 12.0f), Scroll);
            for (const FPortraitLauncherSlot& Slot : Portrait.Slots) {
                if (Slot.Rect.ContainsPoint(Point)) {
                    OutColumn = Slot.Column;
                    OutRow = Slot.Row;
                    return true;
                }
            }
            return false;
        }
        const FLauncherGrid Grid = ComputeLauncherGrid(Shrink(Layout.InteractionSection, 12.0f), Queue.columnCount(),
                                                       Queue.visibleRows());
        for (int32 Column = 0; Column < Queue.columnCount(); ++Column) {
            for (int32 Row = 0; Row < Queue.visibleRows(); ++Row) {
                const QueuedLauncher* Queued = Queue.visible(Column, Row);
                if (Queued && Grid.SlotRect(Column, static_cast<float>(Row) + Queued->riseOffset).ContainsPoint(Point)) {
                    OutColumn = Column;
                    OutRow = Row;
                    return true;
                }
            }
        }
        return false;
    }

    void PaintBrickLabels(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const BrickBoard& Board,
                          const FBallsGameplayLayout& Layout) const {
        for (const Brick& Piece : Board.bricks()) {
            if (!Piece.isAlive() || Piece.maxHits <= 1 || Piece.cells.empty()) {
                continue;
            }
            const FSlateRect Bounds = BrickLabelArea(Piece, Layout);
            const FVector2D BoundsSize = Bounds.GetSize();
            const float LabelSize = static_cast<float>(FMath::Min(BoundsSize.X, BoundsSize.Y)) * 0.55f;
            if (LabelSize < MinimumLabelPixels) {
                continue;
            }
            const FString Label = FString::FromInt(Piece.hitsLeft);
            const FSlateFontInfo Font = BallsStyle::Font(LabelSize, true);
            DrawTextInRect(Out, Layer, Geometry, Label, Bounds.OffsetBy(FVector2D(1.5)), Font,
                           BallsPalette::Linear(BallsPalette::Shade(BallsPalette::Brick(Piece.color), 0.4f)));
            DrawTextInRect(Out, Layer + 1, Geometry, Label, Bounds, Font, BallsPalette::Linear(BallsPalette::Text));
        }
    }

    void PaintLaunchers(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const GameSession& Session,
                        const FBallsGameplayLayout& Layout) const {
        const LauncherQueue& Queue = Session.launchers();
        if (Queue.isEmpty()) {
            DrawTextInRect(Out, Layer, Geometry, TEXT("All launchers released"), Layout.InteractionSection,
                           BallsStyle::Font(26.0f), BallsPalette::Linear(BallsPalette::TextMuted));
            return;
        }
        if (Layout.bPortrait) {
            PaintLaunchersPortrait(Out, Layer, Geometry, Session, Layout);
            return;
        }
        const bool bInteractive = !Gameplay->IsPaused() && Session.state() == SessionState::Running;
        const FLauncherGrid Grid = ComputeLauncherGrid(Shrink(Layout.InteractionSection, 12.0f), Queue.columnCount(),
                                                       Queue.visibleRows());
        const FSlateBrush* Rounded = BallsStyle::RoundedBox(0.0f);
        for (int32 Column = 0; Column < Queue.columnCount(); ++Column) {
            for (int32 Row = 0; Row < Queue.visibleRows(); ++Row) {
                const QueuedLauncher* Queued = Queue.visible(Column, Row);
                if (!Queued) {
                    continue;
                }
                const FSlateRect Rect = Grid.SlotRect(Column, static_cast<float>(Row) + Queued->riseOffset);
                FColor Fill = BallsPalette::Ball(Queued->launcher.color);
                if (Row > 0) {
                    Fill = BallsPalette::Shade(Fill, BackRowFade + (1.0f - BackRowFade) / static_cast<float>(Row + 1));
                }
                if (bInteractive && Rect.ContainsPoint(Mouse)) {
                    Fill = BallsPalette::Shade(Fill, 1.2f);
                }
                const float Depth = Grid.SlotSize * 0.1f;
                const FSlateRect Face(Rect.Left, Rect.Top, Rect.Right, Rect.Bottom - Depth);
                DrawBox(Out, Layer, Geometry, Face.OffsetBy(FVector2D(0.0, Depth)), Rounded,
                        BallsPalette::Linear(BallsPalette::Shade(Fill, 0.6f)));
                DrawBox(Out, Layer + 1, Geometry, Face, Rounded, BallsPalette::Linear(Fill));
                const FString Label = BallsText::CompactCount(Queued->launcher.ballCount);
                const FSlateFontInfo Font = BallsStyle::Font(Grid.SlotSize * 0.36f, true);
                DrawTextInRect(Out, Layer + 2, Geometry, Label, Face.OffsetBy(FVector2D(2.0)), Font,
                               BallsPalette::Linear(BallsPalette::Shade(Fill, 0.45f)));
                DrawTextInRect(Out, Layer + 3, Geometry, Label, Face, Font, BallsPalette::Linear(BallsPalette::Text));
            }
        }
    }

    void PaintLaunchersPortrait(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
                                const GameSession& Session, const FBallsGameplayLayout& Layout) const {
        const LauncherQueue& Queue = Session.launchers();
        const bool bInteractive = !Gameplay->IsPaused() && Session.state() == SessionState::Running;
        const FSlateRect Area = Shrink(Layout.InteractionSection, 12.0f);
        const FPortraitLauncherGrid Grid = ComputePortraitLauncherGrid(Queue, Area, Scroll);
        const FSlateBrush* Rounded = BallsStyle::RoundedBox(0.0f);
        // The row scrolls sideways under the panel edges, so cut off what falls outside it.
        Out.PushClip(FSlateClippingZone(RectGeometry(Geometry, Layout.InteractionSection)));
        for (const FPortraitLauncherSlot& Slot : Grid.Slots) {
            const FSlateRect Rect = Slot.Rect;
            FColor Fill = BallsPalette::Ball(Slot.Queued->launcher.color);
            if (Slot.Row > 0) {
                Fill = BallsPalette::Shade(Fill, BackRowFade + (1.0f - BackRowFade) / static_cast<float>(Slot.Row + 1));
            }
            if (bInteractive && Rect.ContainsPoint(Mouse)) {
                Fill = BallsPalette::Shade(Fill, 1.2f);
            }
            const float Depth = Grid.SlotSize * 0.1f;
            const FSlateRect Face(Rect.Left, Rect.Top, Rect.Right, Rect.Bottom - Depth);
            DrawBox(Out, Layer, Geometry, Face.OffsetBy(FVector2D(0.0, Depth)), Rounded,
                    BallsPalette::Linear(BallsPalette::Shade(Fill, 0.6f)));
            DrawBox(Out, Layer + 1, Geometry, Face, Rounded, BallsPalette::Linear(Fill));
            const FString Label = BallsText::CompactCount(Slot.Queued->launcher.ballCount);
            const FSlateFontInfo Font = BallsStyle::Font(Grid.SlotSize * 0.36f, true);
            DrawTextInRect(Out, Layer + 2, Geometry, Label, Face.OffsetBy(FVector2D(2.0)), Font,
                           BallsPalette::Linear(BallsPalette::Shade(Fill, 0.45f)));
            DrawTextInRect(Out, Layer + 3, Geometry, Label, Face, Font, BallsPalette::Linear(BallsPalette::Text));
        }
        Out.PopClip();
    }

    void PaintStatsStrip(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
                         const GameSession& Session, const FSlateRect& Strip) const {
        const SimulationStats& Stats = Session.simulation().stats();
        const FBallsFrameTimings& Frames = App->GetFrameTimings();
        const FBallsFrameSample& Sample = Frames.Average();
        const FSlateFontInfo LabelFont = BallsStyle::Font(15.0f);
        const FSlateFontInfo ValueFont = BallsStyle::Font(18.0f, true);
        float X = Strip.Left + 300.0f;
        const float Y = Strip.Top + (Strip.GetSize().Y - 18.0f) * 0.5f;
        auto Pair = [&](const FString& Label, const FString& Value, const FLinearColor& Color) {
            if (X > Strip.Right - 170.0f) {
                return;
            }
            DrawText(Out, Layer, Geometry, Label, FVector2D(X, Y + 3.0f), LabelFont,
                     BallsPalette::Linear(BallsPalette::TextMuted));
            X += static_cast<float>(BallsStyle::Measure(Label, LabelFont).X) + 6.0f;
            DrawText(Out, Layer, Geometry, Value, FVector2D(X, Y), ValueFont, Color);
            X += static_cast<float>(BallsStyle::Measure(Value, ValueFont).X) + 18.0f;
        };
        Pair(TEXT("FPS"), FString::Printf(TEXT("%.0f"), Frames.FramesPerSecond()), FpsColor(Frames.FramesPerSecond()));
        Pair(TEXT("Min"), FString::Printf(TEXT("%.0f"), Frames.WorstFpsLastSecond()),
             FpsColor(Frames.WorstFpsLastSecond()));
        Pair(TEXT("Frame"), FString::Printf(TEXT("%.1f ms"), Sample.FrameMs),
             BallsPalette::Linear(BallsPalette::Text));
        Pair(TEXT("GPU"), FString::Printf(TEXT("%.1f ms"), Sample.GpuMs), BallsPalette::Linear(BallsPalette::Text));
        Pair(TEXT("Balls"), BallsText::Grouped(Stats.ballCount), BallsPalette::Linear(BallsPalette::Text));
        Pair(TEXT("Bricks"),
             BallsText::Grouped(static_cast<BallCount>(Session.simulation().board().aliveBrickCount())),
             BallsPalette::Linear(BallsPalette::Text));
        Pair(TEXT("Launch"), BallsText::Grouped(static_cast<BallCount>(Session.launchers().launcherCount())),
             BallsPalette::Linear(BallsPalette::Text));
        Pair(TEXT("Backend"), BallsText::FromUtf8(backendKindName(Session.backend())),
             BallsPalette::Linear(BallsPalette::Text));
    }

    void WritePerformance(FPanelWriter& Writer, const GameSession& Session) const {
        const SimulationStats& Stats = Session.simulation().stats();
        const FBallsFrameTimings& Frames = App->GetFrameTimings();
        const FBallsFrameSample& Sample = Frames.Average();

        Writer.Heading(TEXT("Performance"));
        Writer.Row(TEXT("FPS"), FString::Printf(TEXT("%.0f"), Frames.FramesPerSecond()), FpsColor(Frames.FramesPerSecond()));
        Writer.Row(TEXT("Min FPS (1s)"), FString::Printf(TEXT("%.0f"), Frames.WorstFpsLastSecond()),
                   FpsColor(Frames.WorstFpsLastSecond()));
        Writer.Row(TEXT("Frame"), FString::Printf(TEXT("%.2f ms"), Sample.FrameMs));
        Writer.Row(TEXT("Game thread"), FString::Printf(TEXT("%.2f ms"), Sample.GameMs));
        Writer.Row(TEXT("  Physics"), FString::Printf(TEXT("%.2f ms  x%d"), Stats.lastFramePhysicsMs, Stats.ticksLastFrame));
        Writer.Row(TEXT("Render thread"), FString::Printf(TEXT("%.2f ms"), Sample.RenderMs));
        Writer.Row(TEXT("GPU"), FString::Printf(TEXT("%.2f ms"), Sample.GpuMs));
        const CapacityProbe& Probe = Session.capacityProbe();
        if (Probe.phase() != ProbePhase::Idle) {
            Writer.Row(TEXT("Probe"), FString::Printf(TEXT("%s @ %.0f fps"), ANSI_TO_TCHAR(probePhaseName(Probe.phase())),
                                                      Probe.lastFps()));
        }
        Writer.Row(TEXT("Backend"), BallsText::FromUtf8(backendKindName(Session.backend())));
        if (const std::string Detail = backendDetail(Session.backend()); !Detail.empty()) {
            Writer.Row(TEXT("Device"), BallsText::FromUtf8(Detail));
        }
    }

    void WriteObjects(FPanelWriter& Writer, const GameSession& Session) const {
        const Simulation& Sim = Session.simulation();
        const SimulationStats& Stats = Sim.stats();
        Writer.Heading(TEXT("Objects"));
        const BallCount Balls = Stats.ballCount;
        const BallCount Bricks = static_cast<BallCount>(Sim.board().aliveBrickCount());
        const BallCount Launchers = static_cast<BallCount>(Session.launchers().launcherCount());
        Writer.Row(TEXT("Total objects"), BallsText::Grouped(Balls + Bricks + Launchers));
        Writer.Row(TEXT("Balls"), BallsText::Grouped(Balls));
        Writer.Row(TEXT("Bricks"), BallsText::Grouped(Bricks));
        Writer.Row(TEXT("Launchers"), BallsText::Grouped(Launchers));
        Writer.Row(TEXT("Waiting to spawn"), BallsText::Grouped(Session.spawner().pendingBalls()));
        if (Session.mode() == GameMode::Performance) {
            const BallCount Limit = Session.ballLimit();
            Writer.Row(TEXT("Ball limit"), Session.isUsingMachineBallLimit()
                                               ? TEXT("Auto ") + BallsText::Grouped(Limit)
                                               : (Limit == unlimitedBalls ? FString(TEXT("Max (2^64-1)"))
                                                                          : BallsText::Grouped(Limit)));
        }
    }

    void WriteColors(FPanelWriter& Writer, const GameSession& Session) const {
        const SimulationStats& Stats = Session.simulation().stats();
        Writer.Heading(TEXT("Balls per color"));
        for (int32 Color = 0; Color < ballColorCount; ++Color) {
            Writer.ColorRow(Color, BallsText::Grouped(Stats.ballsByColor[Color]));
        }
        if (!Session.lastBackendFault().empty()) {
            Writer.Heading(TEXT("Backend fault"));
            Writer.Row(TEXT("Fell back to CPU"), FString(), BallsPalette::Linear(BallsPalette::Danger));
        }
    }

    void PaintDebugPanelPortrait(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
                                 const GameSession& Session, const FSlateRect& Area) const {
        constexpr float Gap = 8.0f;
        const float Third = Area.GetSize().X / 3.0f;
        FPanelWriter Performance(Out, Layer, Geometry,
                                 FSlateRect(Area.Left, Area.Top, Area.Left + Third - Gap, Area.Bottom), 26.0f);
        FPanelWriter Objects(Out, Layer, Geometry,
                             FSlateRect(Area.Left + Third, Area.Top, Area.Left + Third * 2.0f - Gap, Area.Bottom), 26.0f);
        FPanelWriter Colors(Out, Layer, Geometry,
                            FSlateRect(Area.Left + Third * 2.0f, Area.Top, Area.Right, Area.Bottom), 26.0f);
        WritePerformance(Performance, Session);
        WriteObjects(Objects, Session);
        WriteColors(Colors, Session);
    }

    void PaintDebugPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const GameSession& Session,
                         const FSlateRect& Area, bool bPortrait) const {
        if (bPortrait) {
            PaintDebugPanelPortrait(Out, Layer, Geometry, Session, Area);
            return;
        }
        FPanelWriter Writer(Out, Layer, Geometry, Area);
        WritePerformance(Writer, Session);
        WriteObjects(Writer, Session);
        WriteColors(Writer, Session);
    }

    void PaintToast(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry) const {
        const FBallsGameplay::FToast& Toast = Gameplay->GetToast();
        if (Toast.SecondsLeft <= 0.0f) {
            return;
        }
        const float Alpha = FMath::Min(1.0f, Toast.SecondsLeft);
        const FSlateFontInfo Font = BallsStyle::Font(24.0f, true);
        const FVector2D Extent = BallsStyle::Measure(Toast.Text, Font);
        const float Left = static_cast<float>((Geometry.GetLocalSize().X - Extent.X) * 0.5 - 20.0);
        const float SafeTop = ComputeGameplayLayout(Geometry.GetLocalSize(), 1, 1,
                                                    Geometry.GetAccumulatedLayoutTransform().GetScale(), true)
                                  .Header.Top;
        const FSlateRect Rect(Left, SafeTop + 24.0f, Left + static_cast<float>(Extent.X) + 40.0f, SafeTop + 72.0f);
        DrawBox(Out, Layer, Geometry, Rect, BallsStyle::RoundedBox(0.0f),
                BallsPalette::Linear(Toast.bIsError ? BallsPalette::Danger : BallsPalette::PanelLight, Alpha));
        DrawTextInRect(Out, Layer + 1, Geometry, Toast.Text, Rect, Font, BallsPalette::Linear(BallsPalette::Text, Alpha));
    }

    FBallsApp* App = nullptr;
    TSharedPtr<FBallsGameplay> Gameplay;
    FVector2D Mouse = FVector2D(-1.0);
    // Portrait launcher row: sideways scroll and the press that may become a tap or a drag.
    float Scroll = 0.0f;
    bool bPressed = false;
    bool bDragging = false;
    FVector2D PressPoint = FVector2D::ZeroVector;
    FVector2D LastPoint = FVector2D::ZeroVector;
};

FText LabelText(const FString& Text) { return FText::FromString(Text); }

}

FBallsGameplayLayout SBallsGameplayHud::CurrentLayout() const {
    const FGeometry& Geometry = GetCachedGeometry();
    const FVector2D Size = Geometry.GetLocalSize();
    const float DpiScale = Geometry.GetAccumulatedLayoutTransform().GetScale();
    const GameSession* Session = Gameplay->GetSession();
    const bool bCompact = Gameplay->GetMode() == GameMode::Normal;
    return Session ? ComputeGameplayLayout(Size, Session->simulation().board().columns(),
                                           Session->simulation().board().rows(), DpiScale, bCompact)
                   : ComputeGameplayLayout(Size, 1, 1, DpiScale, bCompact);
}

void SBallsGameplayHud::Construct(const FArguments& InArgs) {
    App = InArgs._App;
    Gameplay = InArgs._Gameplay;
    TSharedPtr<FBallsGameplay> Game = Gameplay;
    auto HasSession = [Game] { return Game->GetSession() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; };

    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBallsGameplayCanvas).App(App).Gameplay(Gameplay)
        ]
        + SOverlay::Slot()
        [
            SNew(SCanvas)
            .Visibility_Lambda(HasSession)
            + SCanvas::Slot()
            .Position_Lambda([this] {
                const FSlateRect Header = CurrentLayout().Header;
                return FVector2D(Header.Right - 130.0f, Header.GetCenter().Y - 20.0f);
            })
            .Size(FVector2D(120.0f, 40.0f))
            [
                BallsWidgets::Button(TAttribute<FText>::CreateLambda([Game] {
                                         return LabelText(Game->IsPaused() ? TEXT("Resume") : TEXT("Pause"));
                                     }),
                                     [Game] { Game->SetPaused(!Game->IsPaused()); }, 20.0f)
            ]
            + SCanvas::Slot()
            .Position_Lambda([this] { return Shrink(CurrentLayout().InteractionSection, 14.0f).GetTopLeft(); })
            .Size_Lambda([this] { return Shrink(CurrentLayout().InteractionSection, 14.0f).GetSize(); })
            [
                Gameplay->GetMode() == GameMode::Performance ? MakePerformanceControls() : SNullWidget::NullWidget
            ]
        ]
        + SOverlay::Slot()
        [
            MakeOverlay()
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0.0f, 340.0f, 0.0f, 0.0f)
        [
            SNew(SBox)
            .WidthOverride(240.0f)
            .HeightOverride(56.0f)
            .Visibility_Lambda([Game] { return Game->GetSession() ? EVisibility::Collapsed : EVisibility::Visible; })
            [
                BallsWidgets::Button(LabelText(TEXT("Back")), [this] { App->ShowLevelSelect(Gameplay->GetMode()); }, 24.0f)
            ]
        ]
    ];
}

TSharedRef<SWidget> SBallsGameplayHud::MakePerformanceControls() {
    TSharedPtr<FBallsGameplay> Game = Gameplay;
    auto Session = [Game]() -> GameSession* { return Game->GetSession(); };

    auto ButtonRow = [](TArray<TSharedRef<SWidget>> Buttons) {
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        for (int32 Index = 0; Index < Buttons.Num(); ++Index) {
            Row->AddSlot().FillWidth(1.0f).Padding(Index == 0 ? 0.0f : ControlGap * 0.5f, 0.0f,
                                                   Index + 1 == Buttons.Num() ? 0.0f : ControlGap * 0.5f, 0.0f)
            [
                Buttons[Index]
            ];
        }
        return Row;
    };

    TArray<TSharedRef<SWidget>> ReleaseButtons;
    ReleaseButtons.Add(BallsWidgets::Button(
        TAttribute<FText>::CreateLambda([Session] {
            const GameSession* Current = Session();
            if (!Current || Current->state() == SessionState::Ready) {
                return LabelText(TEXT("Start (1 click)"));
            }
            return LabelText(FString::Printf(TEXT("Auto releasing - %d launchers left"), Current->launchers().launcherCount()));
        }),
        [Game] { Game->StartAutoRelease(); }, 18.0f,
        TAttribute<bool>::CreateLambda([Session] { return Session() && Session()->state() == SessionState::Ready; })));
    ReleaseButtons.Add(BallsWidgets::Button(
        TAttribute<FText>::CreateLambda([Session] {
            const GameSession* Current = Session();
            if (!Current) {
                return FText::GetEmpty();
            }
            const CapacityProbe& Probe = Current->capacityProbe();
            if (Probe.isActive()) {
                return LabelText(FString::Printf(TEXT("Probing... %s balls, %.0f fps"),
                                                 *BallsText::Grouped(Current->simulation().liveBallCount()), Probe.lastFps()));
            }
            if (Probe.phase() == ProbePhase::Done) {
                return LabelText(TEXT("Machine max @60 FPS: ") + BallsText::Grouped(Probe.bestPassingCount()));
            }
            return LabelText(TEXT("Find max @60 FPS"));
        }),
        [Game] { Game->ToggleCapacityProbe(); }, 18.0f));

    TArray<TSharedRef<SWidget>> RateButtons;
    for (int32 Index = 0; Index < static_cast<int32>(spawnRateOptions.size()); ++Index) {
        const int32 Rate = spawnRateOptions[Index];
        RateButtons.Add(BallsWidgets::Button(LabelText(Rate == 0 ? FString(TEXT("Max")) : FString::FromInt(Rate)),
                                             [Game, Index] { Game->SelectSpawnRate(Index); }, 18.0f, true,
                                             TAttribute<bool>::CreateLambda([Session, Index] {
                                                 return Session() && Session()->spawnRateIndex() == Index;
                                             })));
    }

    TArray<TSharedRef<SWidget>> MultiplierButtons;
    for (int32 Index = 0; Index < static_cast<int32>(ballMultiplierOptions.size()); ++Index) {
        MultiplierButtons.Add(BallsWidgets::Button(
            LabelText(FString::Printf(TEXT("x%llu"), static_cast<unsigned long long>(ballMultiplierOptions[Index]))),
            [Game, Index] { Game->SelectBallMultiplier(Index); }, 18.0f, true,
            TAttribute<bool>::CreateLambda([Session, Index] {
                return Session() && Session()->ballMultiplierIndex() == Index;
            })));
    }

    TArray<TSharedRef<SWidget>> LimitButtons;
    for (int32 Index = 0; Index < static_cast<int32>(ballLimitOptions.size()); ++Index) {
        LimitButtons.Add(BallsWidgets::Button(
            LabelText(BallsText::BallLimitShort(ballLimitOptions[Index])), [Game, Index] { Game->SelectBallLimit(Index); },
            18.0f, true, TAttribute<bool>::CreateLambda([Session, Index] {
                return Session() && !Session()->isUsingMachineBallLimit() && Session()->ballLimitIndex() == Index;
            })));
    }
    LimitButtons.Add(BallsWidgets::Button(
        TAttribute<FText>::CreateLambda([Session] {
            const GameSession* Current = Session();
            const std::optional<BallCount> Measured = Current ? Current->machineBallLimit() : std::nullopt;
            return LabelText(Measured ? TEXT("Auto ") + BallsText::Grouped(*Measured) : FString(TEXT("Auto (probe)")));
        }),
        [Game] { Game->SelectMachineLimit(); }, 18.0f, true,
        TAttribute<bool>::CreateLambda([Session] { return Session() && Session()->isUsingMachineBallLimit(); })));

    TArray<TSharedRef<SWidget>> BackendButtons;
    for (int32 Index = 0; Index < static_cast<int32>(allBackendKinds.size()); ++Index) {
        const BackendKind Kind = allBackendKinds[Index];
        BackendButtons.Add(BallsWidgets::Button(
            LabelText(BallsText::FromUtf8(backendKindName(Kind))),
            [Game, Kind] { Game->SwitchBackend(Kind); }, 18.0f, isBackendAvailable(Kind),
            TAttribute<bool>::CreateLambda([Session, Kind] { return Session() && Session()->backend() == Kind; })));
    }

    // The controls column is narrow, so each group gets a label line and at most three buttons per row.
    TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
    auto AddGroup = [&Column, &ButtonRow](const TCHAR* Label, const TArray<TSharedRef<SWidget>>& Buttons, int32 PerRow) {
        Column->AddSlot().AutoHeight().Padding(0.0f, ControlGap, 0.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Label))
            .Font(BallsStyle::Font(20.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
        ];
        for (int32 First = 0; First < Buttons.Num(); First += PerRow) {
            TArray<TSharedRef<SWidget>> RowButtons;
            for (int32 Index = First; Index < FMath::Min(First + PerRow, Buttons.Num()); ++Index) {
                RowButtons.Add(Buttons[Index]);
            }
            Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap * 0.6f)
            [
                SNew(SBox).HeightOverride(ControlRowHeight)[ButtonRow(RowButtons)]
            ];
        }
    };
    AddGroup(TEXT("Release"), ReleaseButtons, 1);
    AddGroup(TEXT("Spawn / sec"), RateButtons, 3);
    AddGroup(TEXT("Ball count"), MultiplierButtons, 3);
    AddGroup(TEXT("Ball limit"), LimitButtons, 3);
    AddGroup(TEXT("Physics"), BackendButtons, 3);
    return SNew(SScrollBox) + SScrollBox::Slot()[Column];
}

TSharedRef<SWidget> SBallsGameplayHud::MakeOverlay() {
    TSharedPtr<FBallsGameplay> Game = Gameplay;
    FBallsApp* Owner = App;
    auto OverlayButton = [](TAttribute<FText> Label, TFunction<void()> OnClicked) {
        return SNew(SBox).HeightOverride(OverlayButtonHeight).Padding(0.0f, 0.0f, 0.0f, 14.0f)
            [BallsWidgets::Button(MoveTemp(Label), MoveTemp(OnClicked), 26.0f)];
    };
    return SNew(SBorder)
        .BorderImage(BallsStyle::White())
        .BorderBackgroundColor(BallsPalette::Linear(BallsPalette::BackgroundDeep, 0.7f))
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .Visibility_Lambda([Game] {
            return Game->GetSession() && (Game->IsPaused() || Game->IsWon()) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SBox)
            .WidthOverride(OverlayWidth)
            [
                SNew(SBorder)
                .BorderImage(BallsStyle::Panel())
                .Padding(30.0f, 24.0f)
                [
                    SNew(SWidgetSwitcher)
                    .WidgetIndex_Lambda([Game] { return Game->IsSettingsOpen() ? 1 : 0; })
                    + SWidgetSwitcher::Slot()
                    [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 24.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([Game] { return LabelText(Game->IsWon() ? TEXT("Level Complete!") : TEXT("Paused")); })
                        .Font(BallsStyle::Font(44.0f, true))
                        .ColorAndOpacity_Lambda([Game] {
                            return FSlateColor(BallsPalette::Linear(Game->IsWon() ? BallsPalette::Accent : BallsPalette::Text));
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        OverlayButton(TAttribute<FText>::CreateLambda([Game] {
                                          return LabelText(Game->IsWon() ? TEXT("Next Level") : TEXT("Resume"));
                                      }),
                                      [Game] {
                                          if (Game->IsWon()) {
                                              Game->OpenNextLevel();
                                          } else {
                                              Game->SetPaused(false);
                                          }
                                      })
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        OverlayButton(LabelText(TEXT("Restart")), [Game] { Game->Restart(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        // Settings belong to the pause overlay; the win overlay only offers the next steps.
                        SNew(SBox)
                        .Visibility_Lambda([Game] { return Game->IsWon() ? EVisibility::Collapsed : EVisibility::Visible; })
                        [
                            OverlayButton(LabelText(TEXT("Settings")), [Game] { Game->SetSettingsOpen(true); })
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        OverlayButton(LabelText(TEXT("Main Menu")), [Owner] { Owner->ShowMainMenu(); })
                    ]
                    ]
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 12.0f)
                        [
                            SNew(STextBlock)
                            .Text(LabelText(TEXT("Settings")))
                            .Font(BallsStyle::Font(44.0f, true))
                            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
                        ]
                        + SVerticalBox::Slot().FillHeight(1.0f)
                        [
                            SNew(SBox).MaxDesiredHeight(520.0f)[SNew(SBallsSettingsPanel).App(Owner)]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
                        [
                            SNew(SBox).HeightOverride(OverlayButtonHeight)
                            [
                                BallsWidgets::Button(LabelText(TEXT("Back")), [Game] { Game->SetSettingsOpen(false); }, 26.0f)
                            ]
                        ]
                    ]
                ]
            ]
        ];
}
