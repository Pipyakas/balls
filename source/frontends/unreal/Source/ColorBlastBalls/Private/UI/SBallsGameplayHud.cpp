#include "UI/SBallsGameplayHud.h"

#include "BallsApp.h"
#include "BallsGameplay.h"
#include "BallsPalette.h"
#include "BallsText.h"
#include "DynamicRHI.h"
#include "RHI.h"
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
// The Physics group under the launchers in Normal mode: a label and one 36-high row per backend.
constexpr float PhysicsRowHeight = 36.0f;
constexpr float PhysicsRowGap = 6.0f;
constexpr float PhysicsLabelHeight = 28.0f;
constexpr float PhysicsGroupPadding = 14.0f;
float PhysicsGroupHeight() {
    return PhysicsLabelHeight + static_cast<float>(allBackendKinds.size()) * (PhysicsRowHeight + PhysicsRowGap) +
           PhysicsGroupPadding;
}
constexpr float OverlayWidth = 420.0f;
constexpr float OverlayButtonHeight = 60.0f;
constexpr float LauncherGapShare = 0.18f;
constexpr float MaximumLauncherSize = 110.0f;
constexpr float BackRowFade = 0.55f;
constexpr float MinimumLabelPixels = 9.0f;
// A touch that moves further than this (Slate units) scrolls the launcher row instead of picking a launcher.
constexpr float DragThreshold = 8.0f;

// Profiling switch: 0 paints the gameplay gadgets away so the frame can be measured without them.
static TAutoConsoleVariable<int32> CVarBallsHud(TEXT("r.Balls.Hud"), 1,
                                                TEXT("Paint the gameplay gadgets (0 hides them, profiling)."),
                                                ECVF_RenderThreadSafe);

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
    float GridWidth = 0.0f;
    // How far the grid can scroll sideways; 0 when every column fits.
    float MaximumScroll = 0.0f;
};

// Portrait keeps the queue's own columns and rows: every visible row of every column, slot size
// clamp(width * 0.19, 120, 240) shrunk until the rows fit, scrolling sideways when the columns don't.
FPortraitLauncherGrid ComputePortraitLauncherGrid(const LauncherQueue& Queue, const FSlateRect& Area, float Scroll) {
    FPortraitLauncherGrid Grid;
    const float Columns = static_cast<float>(FMath::Max(Queue.columnCount(), 1));
    const float Rows = static_cast<float>(FMath::Max(Queue.visibleRows(), 1));
    const FVector2D Size = Area.GetSize();
    // Area is the panel shrunk by the 12-unit margin on each side.
    const float ByWidth = PortraitLauncherSize(static_cast<float>(Size.X) + 24.0f);
    const float ByHeight =
        FMath::Max(static_cast<float>(Size.Y) - 24.0f, 1.0f) / (Rows + (Rows - 1.0f) * LauncherGapShare);
    Grid.SlotSize = FMath::Max(1.0f, FMath::Min(ByWidth, ByHeight));
    Grid.Gap = Grid.SlotSize * LauncherGapShare;
    Grid.GridWidth = Columns * Grid.SlotSize + (Columns - 1.0f) * Grid.Gap;
    const float GridHeight = Rows * Grid.SlotSize + (Rows - 1.0f) * Grid.Gap;
    Grid.MaximumScroll = FMath::Max(0.0f, Grid.GridWidth - static_cast<float>(Size.X));
    // Centered when the columns fit; otherwise the row starts at the panel edge and scrolls.
    const float Offset = Grid.MaximumScroll > 0.0f
                             ? -FMath::Clamp(Scroll, 0.0f, Grid.MaximumScroll)
                             : (static_cast<float>(Size.X) - Grid.GridWidth) * 0.5f;
    const float OriginX = static_cast<float>(Area.Left) + Offset;
    const float OriginY = static_cast<float>(Area.Top) + (static_cast<float>(Size.Y) - GridHeight) * 0.5f;
    for (int32 Column = 0; Column < Queue.columnCount(); ++Column) {
        for (int32 Row = 0; Row < Queue.visibleRows(); ++Row) {
            if (const QueuedLauncher* Queued = Queue.visible(Column, Row)) {
                FPortraitLauncherSlot Slot;
                Slot.Queued = Queued;
                Slot.Column = Column;
                Slot.Row = Row;
                const float Left = OriginX + static_cast<float>(Column) * (Grid.SlotSize + Grid.Gap);
                const float Top = OriginY + static_cast<float>(Row) * (Grid.SlotSize + Grid.Gap);
                Slot.Rect = FSlateRect(Left, Top, Left + Grid.SlotSize, Top + Grid.SlotSize);
                Grid.Slots.Add(Slot);
            }
        }
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

// backendDetail takes the plugin lock and may build adapter text, so the panel asks for it twice a second at most.
FString CachedBackendDetail(BackendKind Kind) {
    constexpr double RefreshSeconds = 0.5;
    static BackendKind CachedKind = BackendKind::Scalar;
    static double CachedAt = -1.0e9;
    static FString Cached;
    const double Now = FPlatformTime::Seconds();
    if (Kind != CachedKind || Now - CachedAt >= RefreshSeconds) {
        Cached = BallsText::FromUtf8(backendDetail(Kind));
        CachedKind = Kind;
        CachedAt = Now;
    }
    return Cached;
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

    // Each value sits under its label so the 200-unit gadget fits; headings and color rows stay one line.
    void Row(const FString& Label, const FString& Value, FLinearColor ValueColor = BallsPalette::Linear(BallsPalette::Text)) {
        DrawText(Out, Layer, Geometry, Label, FVector2D(Area.Left + Padding, Cursor), BallsStyle::Font(LabelSize),
                 BallsPalette::Linear(BallsPalette::TextMuted));
        // Long values (the GPU name) are cut to fit the gadget, with a "~" like the header title.
        FString Shown = Value;
        const FSlateFontInfo ValueFont = BallsStyle::Font(ValueSize);
        const float Room = static_cast<float>(Area.GetSize().X) - 2.0f * Padding;
        while (Shown.Len() > 4 && BallsStyle::Measure(Shown, ValueFont).X > Room) {
            Shown = Shown.Left(Shown.Len() - 2) + TEXT("~");
        }
        DrawText(Out, Layer, Geometry, Shown, FVector2D(Area.Left + Padding, Cursor + LabelSize + 1.0f), ValueFont,
                 ValueColor);
        Cursor += LabelSize + ValueSize + 8.0f;
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
    static constexpr float LabelSize = 15.0f;
    static constexpr float ValueSize = 18.0f;
    static constexpr float Padding = 12.0f;

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
        // Profiling: painting the gadgets is the game thread's largest fixed cost, so r.Balls.Hud 0 drops them to
        // measure what the frame costs without them.
        if (CVarBallsHud.GetValueOnGameThread() == 0) {
            DrawBox(Out, LayerId, Geometry, FSlateRect(FVector2D::ZeroVector, Size), BallsStyle::White(),
                    BallsPalette::Linear(BallsPalette::Background));
            return LayerId + 1;
        }
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
        const FLinearColor PanelTint = BallsPalette::Linear(BallsPalette::Panel);
        PaintBoardFrame(Out, LayerId, Geometry, Layout);
        PaintLaunchPoint(Out, LayerId + 1, Geometry, Sim, Layout);

        if (Layout.bPortrait) {
            // Portrait: one top gadget holding the 44 header over the 48 debug strip, then the board, then the
            // bottom interaction gadget.
            DrawBox(Out, LayerId, Geometry, Layout.TopGadget, BallsStyle::RoundedBox(0.0f), PanelTint);
            PaintHeaderTitle(Out, LayerId + 1, Geometry, Layout);
            PaintStatsStrip(Out, LayerId + 1, Geometry, *Session, Layout.DebugSection);
            DrawBox(Out, LayerId, Geometry, Layout.BottomGadget, BallsStyle::RoundedBox(0.0f), PanelTint);
            if (Gameplay->GetMode() == GameMode::Normal) {
                PaintLaunchers(Out, LayerId + 1, Geometry, *Session, Layout);
            }
            PaintBrickLabels(Out, LayerId + 1, Geometry, Sim.board(), Layout);
            PaintToast(Out, LayerId + 3, Geometry);
            return LayerId + 3;
        }

        // Landscape: the left gadget is the header over the user interaction, one panel, and the debug gadget sits
        // at the right edge of the board frame.
        DrawBox(Out, LayerId, Geometry, Layout.LeftGadget, BallsStyle::RoundedBox(0.0f), PanelTint);
        PaintHeaderTitle(Out, LayerId + 1, Geometry, Layout);

        PaintBrickLabels(Out, LayerId + 1, Geometry, Sim.board(), Layout);

        if (Gameplay->GetMode() == GameMode::Normal) {
            PaintLaunchers(Out, LayerId + 1, Geometry, *Session, Layout);
        }

        DrawBox(Out, LayerId, Geometry, Layout.RightGadget, BallsStyle::RoundedBox(0.0f), PanelTint);
        PaintDebugPanel(Out, LayerId + 1, Geometry, *Session, Layout.RightGadget);

        PaintToast(Out, LayerId + 3, Geometry);
        return LayerId + 3;
    }

private:
    // "<Normal|Performance> - <level name>" (22) with the Pause button at the right edge, and the map notice in
    // accent when the file changed on disk. The same header sits at the top of the left gadget in landscape and of
    // the top gadget in portrait; only its height differs (56 and 44).
    void PaintHeaderTitle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
                          const FBallsGameplayLayout& Layout) const {
        FString Title = FString::Printf(TEXT("%s - %s"),
                                        Gameplay->GetMode() == GameMode::Normal ? TEXT("Normal") : TEXT("Performance"),
                                        *BallsText::FromUtf8(Gameplay->GetFiles().name));
        const FSlateFontInfo TitleFont = BallsStyle::Font(22.0f);
        const float TitleRoom = Layout.Header.GetSize().X - 16.0f - 96.0f - 20.0f;
        while (Title.Len() > 4 && BallsStyle::Measure(Title, TitleFont).X > TitleRoom) {
            Title = Title.Left(Title.Len() - 2) + TEXT("~");
        }
        const FString Notice = MapNoticeText();
        DrawText(Out, Layer, Geometry, Title,
                 FVector2D(Layout.Header.Left + 16.0f,
                           Notice.IsEmpty() ? Layout.Header.GetCenter().Y - 13.0f : Layout.Header.Top + 4.0f),
                 TitleFont, BallsPalette::Linear(BallsPalette::Text));
        if (!Notice.IsEmpty()) {
            DrawText(Out, Layer, Geometry, Notice, FVector2D(Layout.Header.Left + 16.0f, Layout.Header.Top + 31.0f),
                     BallsStyle::Font(15.0f), BallsPalette::Linear(BallsPalette::Accent));
        }
        DrawBox(Out, Layer, Geometry,
                FSlateRect(Layout.Header.Left, Layout.Header.Bottom - 1.0f, Layout.Header.Right, Layout.Header.Bottom),
                BallsStyle::RoundedBox(0.0f), BallsPalette::Linear(BallsPalette::Frame));
    }

    // Landscape Normal mode keeps the bottom of the gadget for the Physics group.
    FSlateRect LauncherArea(const FBallsGameplayLayout& Layout) const {
        FSlateRect Area = Shrink(Layout.InteractionSection, 12.0f);
        if (!Layout.bPortrait) {
            Area.Bottom = FMath::Max(Area.Top + 1.0f, Area.Bottom - PhysicsGroupHeight());
        }
        return Area;
    }

    FString MapNoticeText() const { return BallsText::FromUtf8(levelDiskNotice(Gameplay->GetDiskState())); }

    // The board frame is the gadget color around the 3D board: four strips, so the board stays visible.
    void PaintBoardFrame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
                         const FBallsGameplayLayout& Layout) const {
        const FSlateRect& Frame = Layout.MainSection;
        const FSlateRect& Board = Layout.Board;
        const FLinearColor Fill = BallsPalette::Linear(BallsPalette::Panel);
        const FLinearColor Border = BallsPalette::Linear(BallsPalette::Frame);
        const FSlateBrush* Square = BallsStyle::RoundedBox(0.0f);
        DrawBox(Out, Layer, Geometry, FSlateRect(Frame.Left, Frame.Top, Frame.Right, Board.Top), Square, Fill);
        DrawBox(Out, Layer, Geometry, FSlateRect(Frame.Left, Board.Bottom, Frame.Right, Frame.Bottom), Square, Fill);
        DrawBox(Out, Layer, Geometry, FSlateRect(Frame.Left, Board.Top, Board.Left, Board.Bottom), Square, Fill);
        DrawBox(Out, Layer, Geometry, FSlateRect(Board.Right, Board.Top, Frame.Right, Board.Bottom), Square, Fill);
        const FSlateBrush* Outline = BallsStyle::White();
        DrawBox(Out, Layer + 1, Geometry, FSlateRect(Frame.Left, Frame.Top, Frame.Right, Frame.Top + 1.0f), Outline, Border);
        DrawBox(Out, Layer + 1, Geometry, FSlateRect(Frame.Left, Frame.Bottom - 1.0f, Frame.Right, Frame.Bottom), Outline, Border);
        DrawBox(Out, Layer + 1, Geometry, FSlateRect(Frame.Left, Frame.Top, Frame.Left + 1.0f, Frame.Bottom), Outline, Border);
        DrawBox(Out, Layer + 1, Geometry, FSlateRect(Frame.Right - 1.0f, Frame.Top, Frame.Right, Frame.Bottom), Outline, Border);
    }

    // The launch point marker: a dim triangle, 0.6 cell, where balls enter. Built from thin strips since Slate has
    // no filled polygon primitive here.
    void PaintLaunchPoint(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const Simulation& Sim,
                          const FBallsGameplayLayout& Layout) const {
        const FVector2D Center = Layout.BoardToScreen(Sim.launchX(), Sim.launchY());
        const float Size = Layout.CellSize * 0.6f;
        const float Top = static_cast<float>(Center.Y) - Size * 0.6f;
        const float Height = Size;
        constexpr int32 Strips = 16;
        const FLinearColor Tint = BallsPalette::Linear(BallsPalette::TextDim);
        for (int32 Strip = 0; Strip < Strips; ++Strip) {
            const float From = static_cast<float>(Strip) / Strips;
            const float To = static_cast<float>(Strip + 1) / Strips;
            const float HalfWidth = Size * To;
            DrawBox(Out, Layer, Geometry,
                    FSlateRect(static_cast<float>(Center.X) - HalfWidth, Top + Height * From,
                               static_cast<float>(Center.X) + HalfWidth, Top + Height * To),
                    BallsStyle::White(), Tint);
        }
    }

    FBallsGameplayLayout LayoutFor(const FGeometry& Geometry) const {
        const GameSession& Session = *Gameplay->GetSession();
        const Simulation& Sim = Session.simulation();
        return ComputeGameplayLayout(Geometry.GetLocalSize(), Sim.board().columns(), Sim.board().rows(),
                                     Geometry.GetAccumulatedLayoutTransform().GetScale(),
                                     Gameplay->GetMode() == GameMode::Normal ? Session.launchers().visibleRows() : 0);
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
        const FLauncherGrid Grid = ComputeLauncherGrid(LauncherArea(Layout), Queue.columnCount(),
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
        const FLauncherGrid Grid = ComputeLauncherGrid(LauncherArea(Layout), Queue.columnCount(),
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
        const FrameTimings& Frames = App->GetFrameTimings();
        const FrameSample& Sample = Frames.average();
        const FSlateFontInfo LabelFont = BallsStyle::Font(15.0f);
        const FSlateFontInfo ValueFont = BallsStyle::Font(18.0f, true);
        // The strip has its own row under the portrait header, so it starts at the panel edge.
        float X = Strip.Left + 20.0f;
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
        Pair(TEXT("FPS"), FString::Printf(TEXT("%.0f"), Frames.framesPerSecond()), FpsColor(Frames.framesPerSecond()));
        Pair(TEXT("Work"), FString::Printf(TEXT("%.0f"), Frames.workFramesPerSecond()),
             FpsColor(Frames.workFramesPerSecond()));
        Pair(TEXT("Frame"), FString::Printf(TEXT("%.1f ms"), Sample.frameMs),
             BallsPalette::Linear(BallsPalette::Text));
        Pair(TEXT("GPU"), FString::Printf(TEXT("%.1f ms"), Sample.gpuMs), BallsPalette::Linear(BallsPalette::Text));
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
        const FrameTimings& Frames = App->GetFrameTimings();
        const FrameSample& Sample = Frames.average();

        Writer.Heading(TEXT("Performance"));
        Writer.Row(TEXT("FPS"), FString::Printf(TEXT("%.0f"), Frames.framesPerSecond()), FpsColor(Frames.framesPerSecond()));
        Writer.Row(TEXT("Min FPS (1s)"), FString::Printf(TEXT("%.0f"), Frames.worstFpsLastSecond()),
                   FpsColor(Frames.worstFpsLastSecond()));
        Writer.Row(TEXT("Work FPS"), FString::Printf(TEXT("%.0f"), Frames.workFramesPerSecond()),
                   FpsColor(Frames.workFramesPerSecond()));
        Writer.Row(TEXT("Min work FPS (1s)"), FString::Printf(TEXT("%.0f"), Frames.worstWorkFpsLastSecond()),
                   FpsColor(Frames.worstWorkFpsLastSecond()));
        Writer.Row(TEXT("Frame ms"), FString::Printf(TEXT("%.2f"), Sample.frameMs));
        Writer.Row(TEXT("Physics ms"), FString::Printf(TEXT("%.2f"), Stats.lastFramePhysicsMs));
        Writer.Row(TEXT("GPU ms"), FString::Printf(TEXT("%.2f"), Sample.gpuMs));
        const CapacityProbe& Probe = Session.capacityProbe();
        if (Probe.phase() != ProbePhase::Idle) {
            Writer.Row(TEXT("Probe"), FString::Printf(TEXT("%s %.0f/%.0f fps"), ANSI_TO_TCHAR(probePhaseName(Probe.phase())),
                                                      Probe.lastFps(), Probe.targetFps()));
        }
        Writer.Row(TEXT("Backend"), BallsText::FromUtf8(backendKindName(Session.backend())));
        Writer.Row(TEXT("Renderer"), FString::Printf(TEXT("Unreal / %s / %s"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("RHI"),
                                                     *GRHIAdapterName));
        if (const FString Detail = CachedBackendDetail(Session.backend()); !Detail.IsEmpty()) {
            Writer.Row(TEXT("Device"), Detail);
        }
        const Simulation& Sim = Session.simulation();
        Writer.Row(TEXT("Ball collisions"), !Sim.ballCollisionsRequested()  ? FString(TEXT("Off"))
                                            : Sim.ballCollisionsActive()    ? FString(TEXT("On"))
                                                                            : FString(TEXT("On (unsupported)")));
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
            const std::optional<BallCount> Measured = Session.machineBallLimit();
            const FString Resolved = Measured ? TEXT("Auto ") + BallsText::Grouped(*Measured)
                                              : FString(TEXT("Auto (probe needed)"));
            Writer.Row(TEXT("Ball limit"), Session.isUsingMachineBallLimit() ? Resolved
                                           : Limit == unlimitedBalls          ? FString(TEXT("Max (2^64-1)"))
                                                                              : BallsText::Grouped(Limit));
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

    // Portrait has no room for the three groups, so it shows the 48-unit stats strip instead (see PaintStatsStrip).
    void PaintDebugPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
                         const GameSession& Session, const FSlateRect& Area) const {
        FPanelWriter Writer(Out, Layer, Geometry, Area);
        WritePerformance(Writer, Session);
        WriteObjects(Writer, Session);
        WriteColors(Writer, Session);
    }

    void PaintToast(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry) const {
        const FBallsToast& Toast = Gameplay->GetToast();
        if (Toast.SecondsLeft <= 0.0f) {
            return;
        }
        const float Alpha = FMath::Min(1.0f, Toast.SecondsLeft);
        const FSlateFontInfo Font = BallsStyle::Font(24.0f, true);
        const FVector2D Extent = BallsStyle::Measure(Toast.Text, Font);
        const float Left = static_cast<float>((Geometry.GetLocalSize().X - Extent.X) * 0.5 - 20.0);
        const float SafeTop = ComputeGameplayLayout(Geometry.GetLocalSize(), 1, 1,
                                                    Geometry.GetAccumulatedLayoutTransform().GetScale(), 0)
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
    if (LayoutFrame == GFrameCounter) {
        return CachedLayout;
    }
    LayoutFrame = GFrameCounter;
    const FGeometry& Geometry = GetCachedGeometry();
    const FVector2D Size = Geometry.GetLocalSize();
    const float DpiScale = Geometry.GetAccumulatedLayoutTransform().GetScale();
    const GameSession* Session = Gameplay->GetSession();
    const int32 LauncherRows =
        Gameplay->GetMode() == GameMode::Normal && Session ? Session->launchers().visibleRows() : 0;
    CachedLayout = Session ? ComputeGameplayLayout(Size, Session->simulation().board().columns(),
                                                   Session->simulation().board().rows(), DpiScale, LauncherRows)
                           : ComputeGameplayLayout(Size, 1, 1, DpiScale, LauncherRows);
    return CachedLayout;
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
            .Visibility_Lambda([HasSession] {
                return HasSession() != EVisibility::Collapsed && CVarBallsHud.GetValueOnGameThread() != 0
                           ? EVisibility::Visible
                           : EVisibility::Collapsed;
            })
            + SCanvas::Slot()
            .Position_Lambda([this] {
                const FSlateRect Header = CurrentLayout().Header;
                return FVector2D(Header.Right - 108.0f, Header.GetCenter().Y - 20.0f);
            })
            .Size(FVector2D(96.0f, 40.0f))
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
            + SCanvas::Slot()
            .Position_Lambda([this] {
                const FSlateRect Area = CurrentLayout().InteractionSection;
                return FVector2D(Area.Left + PhysicsGroupPadding, Area.Bottom - PhysicsGroupHeight());
            })
            .Size_Lambda([this] {
                const FSlateRect Area = CurrentLayout().InteractionSection;
                return FVector2D(Area.GetSize().X - 2.0f * PhysicsGroupPadding, PhysicsGroupHeight() - PhysicsGroupPadding);
            })
            [
                MakeNormalPhysicsGroup()
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

// Normal mode: under the launchers, the same backend list as Performance mode. It starts on the Settings default
// and switches this game only.
TSharedRef<SWidget> SBallsGameplayHud::MakeNormalPhysicsGroup() {
    TSharedPtr<FBallsGameplay> Game = Gameplay;
    TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
    Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
    [
        SNew(STextBlock)
        .Text(LabelText(TEXT("Physics")))
        .Font(BallsStyle::Font(20.0f))
        .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
    ];
    for (const BackendKind Kind : allBackendKinds) {
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, PhysicsRowGap)
        [
            SNew(SBox).HeightOverride(PhysicsRowHeight)
            [
                BallsWidgets::Button(LabelText(BallsText::FromUtf8(backendKindName(Kind))),
                                     [Game, Kind] { Game->SwitchBackend(Kind); }, 18.0f, isBackendAvailable(Kind),
                                     TAttribute<bool>::CreateLambda([Game, Kind] {
                                         return Game->GetSession() && Game->GetSession()->backend() == Kind;
                                     }))
            ]
        ];
    }
    // Landscape Normal mode only; portrait keeps the compact launcher row and Performance has its own column.
    return SNew(SBox).Visibility_Lambda([this, Game] {
        return Game->GetMode() == GameMode::Normal && !CurrentLayout().bPortrait ? EVisibility::SelfHitTestInvisible
                                                                                  : EVisibility::Collapsed;
    })[Column];
}

TSharedRef<SWidget> SBallsGameplayHud::MakePerformanceControls() {
    TSharedPtr<FBallsGameplay> Game = Gameplay;
    FBallsApp* Owner = App;
    auto Session = [Game]() -> GameSession* { return Game->GetSession(); };

    // One vertical column that scrolls; rows are ControlRowHeight high and ControlGap apart.
    TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
    auto AddLabel = [&Column](const TCHAR* Label) {
        Column->AddSlot().AutoHeight().Padding(0.0f, ControlGap, 0.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Label))
            .Font(BallsStyle::Font(20.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
        ];
    };
    auto AddRow = [&Column](TSharedRef<SWidget> Content, bool bSized = true) {
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap * 0.6f)
        [
            bSized ? StaticCastSharedRef<SWidget>(SNew(SBox).HeightOverride(ControlRowHeight)[Content]) : Content
        ];
    };

    AddLabel(TEXT("Release"));
    AddRow(BallsWidgets::Button(
        TAttribute<FText>::CreateLambda([Session] {
            const GameSession* Current = Session();
            if (!Current || Current->state() == SessionState::Ready) {
                return LabelText(TEXT("Start (1 click)"));
            }
            return LabelText(FString::Printf(TEXT("Auto releasing - %d launchers left"), Current->launchers().launcherCount()));
        }),
        // The full design label is the longest string in the gadget; 16 keeps it inside the 310-wide panel.
        [Game] { Game->StartAutoRelease(); }, 16.0f,
        TAttribute<bool>::CreateLambda([Session] { return Session() && Session()->state() == SessionState::Ready; })));

    AddLabel(TEXT("Spawn / sec"));
    TArray<FString> RateNames;
    for (const int32 Rate : spawnRateOptions) {
        RateNames.Add(Rate == 0 ? FString(TEXT("Max")) : FString::FromInt(Rate));
    }
    AddRow(BallsWidgets::Dropdown(RateNames, [Session] { return Session() ? Session()->spawnRateIndex() : 0; },
                                  [Game](int32 Index) { Game->SelectSpawnRate(Index); }),
           false);

    AddLabel(TEXT("Ball count"));
    TArray<FString> MultiplierNames;
    for (const BallCount Multiplier : ballMultiplierOptions) {
        MultiplierNames.Add(FString::Printf(TEXT("x%llu"), static_cast<unsigned long long>(Multiplier)));
    }
    AddRow(BallsWidgets::Dropdown(MultiplierNames, [Session] { return Session() ? Session()->ballMultiplierIndex() : 0; },
                                  [Game](int32 Index) { Game->SelectBallMultiplier(Index); }),
           false);

    // The first entry is Auto (the probe result for this machine); the rest are the fixed limits. Every Performance
    // game starts on Auto. While Auto is chosen, a Target FPS stepper and a muted status line sit under the dropdown;
    // there is no separate probe button.
    AddLabel(TEXT("Ball limit"));
    TArray<FString> LimitNames;
    LimitNames.Add(TEXT("Auto"));
    for (const BallCount Limit : ballLimitOptions) {
        LimitNames.Add(BallsText::BallLimitShort(Limit));
    }
    AddRow(BallsWidgets::Dropdown(
               LimitNames,
               [Game] { return !Game->GetSession() ? 0 : Game->IsAutoLimit() ? 0 : Game->GetSession()->ballLimitIndex() + 1; },
               [Game](int32 Index) {
                   if (Index == 0) {
                       Game->UseAutoBallLimit();
                   } else {
                       Game->SelectBallLimit(Index - 1);
                   }
               }),
           false);
    Column->AddSlot().AutoHeight()
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([Game] { return Game->IsAutoLimit() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(LabelText(TEXT("Target FPS")))
            .Font(BallsStyle::Font(15.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBox).HeightOverride(ControlRowHeight)
            [
                BallsWidgets::Stepper([Game] { return Game->GetProbeTargetFps(); },
                                      [Game](int32 Fps) { Game->SetProbeTargetFps(Fps); },
                                      FBallsGameplay::MinimumProbeTargetFps, FBallsGameplay::MaximumProbeTargetFps)
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, ControlGap * 0.6f)
        [
            SNew(STextBlock)
            .Text_Lambda([Game] { return LabelText(Game->GetProbeStatus()); })
            .Font(BallsStyle::Font(15.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
        ]
    ];

    AddLabel(TEXT("Physics"));
    for (const BackendKind Kind : allBackendKinds) {
        AddRow(BallsWidgets::Button(LabelText(BallsText::FromUtf8(backendKindName(Kind))),
                                    [Game, Kind] { Game->SwitchBackend(Kind); }, 18.0f, isBackendAvailable(Kind),
                                    TAttribute<bool>::CreateLambda([Session, Kind] {
                                        return Session() && Session()->backend() == Kind;
                                    })));
        if (!isBackendAvailable(Kind)) {
            Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ControlGap * 0.6f)
            [
                SNew(STextBlock)
                .Text(LabelText(BallsText::FromUtf8(backendUnavailableReason(Kind))))
                .Font(BallsStyle::Font(15.0f))
                .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
                .AutoWrapText(true)
            ];
        }
    }

    // The Unreal port's own lever: the post chain (bloom, TAA, auto-exposure, screen percentage) is what keeps this
    // frontend off the SDL frame time, so the level can be changed while watching the debug gadget.
    AddLabel(TEXT("Graphics"));
    for (const EBallsGraphics Level : BallsGraphics::All()) {
        AddRow(BallsWidgets::Button(
            LabelText(BallsGraphics::Name(Level)), [Owner, Level] { Owner->SetGraphicsQuality(Level); }, 18.0f, true,
            TAttribute<bool>::CreateLambda([Owner, Level] { return Owner->GetGraphicsQuality() == Level; })));
    }
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
                            SNew(SBox).MaxDesiredHeight(520.0f)[SNew(SBallsSettingsPanel).App(Owner).bAllowBenchmark(false)]
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
