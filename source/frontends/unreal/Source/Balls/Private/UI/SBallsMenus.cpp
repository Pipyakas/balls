#include "UI/SBallsMenus.h"

#include "BallsApp.h"
#include "BallsPalette.h"
#include "BallsText.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "UI/BallsStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include <algorithm>

namespace {

float OptionIndexGap(size_t Index, size_t Count, float Gap) { return Index + 1 < Count ? Gap : 0.0f; }

constexpr float MenuButtonWidth = 380.0f;
constexpr float MenuButtonHeight = 64.0f;
constexpr float MenuButtonGap = 18.0f;
constexpr float TitleSize = 72.0f;
constexpr int32 DecorativeBallCount = 28;
constexpr float ListWidth = 560.0f;
constexpr float RowHeight = 58.0f;
constexpr float RowGap = 12.0f;

TSharedRef<SWidget> SizedButton(float Width, float Height, TSharedRef<SWidget> Content) {
    return SNew(SBox).WidthOverride(Width).HeightOverride(Height)[Content];
}

}

namespace BallsWidgets {

TSharedRef<SWidget> Button(TAttribute<FText> Label, TFunction<void()> OnClicked, float FontSize,
                           TAttribute<bool> bEnabled, TAttribute<bool> bSelected) {
    auto Make = [&](const FButtonStyle* Style, FSlateColor TextColor) {
        return SNew(SButton)
            .ButtonStyle(Style)
            .IsFocusable(false)
            .IsEnabled(bEnabled)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .OnClicked_Lambda([OnClicked] {
                OnClicked();
                return FReply::Handled();
            })
            [
                SNew(SScaleBox)
                .Stretch(EStretch::ScaleToFit)
                .StretchDirection(EStretchDirection::DownOnly)
                [
                    SNew(STextBlock)
                    .Text(Label)
                    .Font(BallsStyle::Font(FontSize, true))
                    .ColorAndOpacity(TextColor)
                    .Justification(ETextJustify::Center)
                ]
            ];
    };
    TSharedRef<SWidget> Normal = Make(&BallsStyle::Button(), FSlateColor::UseForeground());
    if (!bSelected.IsBound() && !bSelected.Get()) {
        return Normal;
    }
    // A selected option is an accent fill with background-colored text.
    TSharedRef<SWidget> Selected = Make(&BallsStyle::SelectedButton(), BallsPalette::Linear(BallsPalette::Background));
    return SNew(SWidgetSwitcher)
        .WidgetIndex_Lambda([bSelected] { return bSelected.Get() ? 1 : 0; })
        + SWidgetSwitcher::Slot()[Normal]
        + SWidgetSwitcher::Slot()[Selected];
}

}

namespace BallsWidgets {

TSharedRef<SWidget> Dropdown(TArray<FString> Options, TFunction<int32()> GetIndex, TFunction<void(int32)> OnSelect,
                             float ControlHeight, float FontSize) {
    TSharedRef<bool> bOpen = MakeShared<bool>(false);
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox).Visibility_Lambda([bOpen] {
        return *bOpen ? EVisibility::Visible : EVisibility::Collapsed;
    });
    for (int32 Index = 0; Index < Options.Num(); ++Index) {
        List->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SBox).HeightOverride(ControlHeight)
            [
                Button(FText::FromString(Options[Index]), [bOpen, OnSelect, Index] {
                    OnSelect(Index);
                    *bOpen = false;
                }, FontSize, true, TAttribute<bool>::CreateLambda([GetIndex, Index] { return GetIndex() == Index; }))
            ]
        ];
    }
    constexpr float ValueInset = 12.0f;
    TSharedPtr<SButton> Head;
    auto Lit = [bOpen](const TSharedPtr<SButton>& Button) { return *bOpen || (Button.IsValid() && Button->IsHovered()); };
    SAssignNew(Head, SButton)
        .ButtonStyle(&BallsStyle::DropdownButton())
        .IsFocusable(false)
        .ContentPadding(FMargin(0.0f))
        .OnClicked_Lambda([bOpen] {
            *bOpen = !*bOpen;
            return FReply::Handled();
        });
    TWeakPtr<SButton> WeakHead = Head;
    Head->SetContent(
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(BallsStyle::White())
            .BorderBackgroundColor_Lambda([WeakHead, bOpen] {
                return BallsPalette::Linear(*bOpen || (WeakHead.IsValid() && WeakHead.Pin()->IsHovered()) ? BallsPalette::Accent
                                                                                                         : BallsPalette::Border);
            })
            .Padding(1.0f)
            [
                SNew(SBorder).BorderImage(BallsStyle::White()).BorderBackgroundColor_Lambda([WeakHead] {
                    const TSharedPtr<SButton> Button = WeakHead.Pin();
                    return BallsPalette::Linear(Button && Button->IsPressed()   ? BallsPalette::ControlPressed
                                                : Button && Button->IsHovered() ? BallsPalette::ControlHover
                                                                                : BallsPalette::BoardBackdrop);
                })
            ]
        ]
        + SOverlay::Slot()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(ValueInset, 0.0f)
            [
                SNew(STextBlock)
                .Text_Lambda([Options, GetIndex] {
                    return FText::FromString(Options[FMath::Clamp(GetIndex(), 0, Options.Num() - 1)]);
                })
                .Font(BallsStyle::Font(FontSize))
                .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 6.0f)
            [
                SNew(SBox).WidthOverride(1.0f)
                [
                    SNew(SBorder).BorderImage(BallsStyle::White()).BorderBackgroundColor(BallsPalette::Linear(BallsPalette::Border))
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(ControlHeight).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [
                    Chevron(TAttribute<EChevron>::CreateLambda([bOpen] { return *bOpen ? EChevron::Up : EChevron::Down; }),
                            TAttribute<FLinearColor>::CreateLambda([WeakHead, Lit] {
                                return BallsPalette::Linear(Lit(WeakHead.Pin()) ? BallsPalette::Accent : BallsPalette::TextMuted);
                            }))
                ]
            ]
        ]);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBox).HeightOverride(ControlHeight)[Head.ToSharedRef()]
        ]
        + SVerticalBox::Slot().AutoHeight()[List];
}

namespace {

class SBallsChevron final : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SBallsChevron) {}
        SLATE_ATTRIBUTE(EChevron, Direction)
        SLATE_ATTRIBUTE(FLinearColor, Color)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs) {
        Direction = InArgs._Direction;
        Color = InArgs._Color;
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D(HalfWidth * 2.0f); }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out,
                  int32 LayerId, const FWidgetStyle&, bool) const override {
        // Strips, as the launch point marker: Slate has no filled polygon here.
        constexpr int32 Strips = 8;
        const FVector2f Center = FVector2f(Geometry.GetLocalSize()) * 0.5f;
        const EChevron Pointing = Direction.Get();
        for (int32 Strip = 0; Strip < Strips; ++Strip) {
            // From the wide base to the tip.
            const float From = static_cast<float>(Strip) / Strips;
            const float To = static_cast<float>(Strip + 1) / Strips;
            const float Half = HalfWidth * (1.0f - From);
            FVector2f Position;
            FVector2f Size;
            if (Pointing == EChevron::Right) {
                Position = FVector2f(Center.X - HalfHeight + 2.0f * HalfHeight * From, Center.Y - Half);
                Size = FVector2f(2.0f * HalfHeight * (To - From), 2.0f * Half);
            } else {
                const float Base = Pointing == EChevron::Down ? Center.Y - HalfHeight + 2.0f * HalfHeight * From
                                                              : Center.Y + HalfHeight - 2.0f * HalfHeight * To;
                Position = FVector2f(Center.X - Half, Base);
                Size = FVector2f(2.0f * Half, 2.0f * HalfHeight * (To - From));
            }
            FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)),
                                       BallsStyle::White(), ESlateDrawEffect::None, Color.Get());
        }
        return LayerId + 1;
    }

private:
    static constexpr float HalfWidth = 6.0f;
    static constexpr float HalfHeight = 3.5f;
    TAttribute<EChevron> Direction;
    TAttribute<FLinearColor> Color;
};

}

TSharedRef<SWidget> Chevron(TAttribute<EChevron> Direction, TAttribute<FLinearColor> Color) {
    return SNew(SBallsChevron).Direction(MoveTemp(Direction)).Color(MoveTemp(Color));
}

TSharedRef<SWidget> FrameTimingsStrip(const FrameTimings& Timings) {
    const FrameTimings* Source = &Timings;
    return SNew(SBorder)
        .Visibility(EVisibility::HitTestInvisible)
        .BorderImage(BallsStyle::White())
        .BorderBackgroundColor(BallsPalette::Linear(BallsPalette::BoardBackdrop, 0.85f))
        .Padding(FMargin(10.0f, 4.0f))
        [
            SNew(STextBlock)
            .Text_Lambda([Source] {
                const FrameSample& Sample = Source->average();
                return FText::FromString(FString::Printf(TEXT("%.0f fps (%.0f shown)   cpu %.2f   gpu %.2f   present %.2f ms"),
                                                         Source->framesPerSecond(), Source->shownFramesPerSecond(),
                                                         Sample.cpuMs, Sample.gpuMs, Sample.presentMs));
            })
            .Font(BallsStyle::Font(18.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
        ];
}

TSharedRef<SWidget> Stepper(TFunction<int32()> GetValue, TFunction<void(int32)> OnChange, int32 Minimum, int32 Maximum,
                            float ControlHeight, float FontSize) {
    auto Step = [GetValue, OnChange, Minimum, Maximum](int32 Direction) {
        const int32 Amount = FSlateApplication::Get().GetModifierKeys().IsShiftDown() ? 10 : 1;
        OnChange(FMath::Clamp(GetValue() + Direction * Amount, Minimum, Maximum));
    };
    return SNew(SBox).HeightOverride(ControlHeight)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(ControlHeight)[Button(FText::FromString(TEXT("-")), [Step] { Step(-1); }, FontSize)]
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f)
            [
                SNew(SBorder)
                .BorderImage(BallsStyle::Panel())
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([GetValue] { return FText::FromString(FString::FromInt(GetValue())); })
                    .Font(BallsStyle::Font(FontSize))
                    .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(ControlHeight)[Button(FText::FromString(TEXT("+")), [Step] { Step(1); }, FontSize)]
            ]
        ];
}

}

void SBallsMainMenu::Construct(const FArguments& InArgs) {
    App = InArgs._App;
    FBallsApp* Owner = App;
    auto MenuButton = [](const TCHAR* Label, TFunction<void()> OnClicked, bool bEnabled = true) {
        return SizedButton(MenuButtonWidth, MenuButtonHeight,
                           BallsWidgets::Button(FText::FromString(Label), MoveTemp(OnClicked), 26.0f, bEnabled));
    };
    // The title and the buttons, in the game's order, come from the core's screen flow, as in the SDL front end.
    const ScreenFlow& Flow = Owner->GetScreenFlow();
    TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, TitleSize * 0.7f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(BallsText::FromUtf8(Flow.title())))
            .Font(BallsStyle::Font(TitleSize, true))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Accent))
        ];
    for (const FlowButton& Entry : Flow.buttons()) {
        const FlowAction Action = Entry.action;
        Column->AddSlot().AutoHeight().Padding(0.0f, MenuButtonGap * 0.5f)
        [
            MenuButton(*BallsText::FromUtf8(Entry.label), [Owner, Action] { Owner->ActivateFlow(Action); }, Entry.enabled)
        ];
    }
    ChildSlot
    [
        SNew(SBox)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        [
            Column
        ]
    ];
}

int32 SBallsMainMenu::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                              const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                              int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const {
    FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), BallsStyle::White(),
                               ESlateDrawEffect::None, BallsPalette::Linear(BallsPalette::Background));
    const FVector2D Size = AllottedGeometry.GetLocalSize();
    const float Time = static_cast<float>(FPlatformTime::Seconds());
    for (int32 Index = 0; Index < DecorativeBallCount; ++Index) {
        const float Phase = static_cast<float>(Index) * 1.618f;
        const float X = Size.X * (0.5f + 0.45f * FMath::Sin(Time * 0.3f + Phase * 2.0f));
        const float Y = Size.Y * (0.5f + 0.45f * FMath::Cos(Time * 0.23f + Phase * 3.0f));
        const float Radius = 10.0f + static_cast<float>(Index % 4) * 4.0f;
        FSlateDrawElement::MakeBox(
            OutDrawElements, LayerId + 1,
            AllottedGeometry.ToPaintGeometry(FVector2f(Radius * 2.0f), FSlateLayoutTransform(FVector2f(X - Radius, Y - Radius))),
            BallsStyle::RoundedBox(Radius), ESlateDrawEffect::None,
            BallsPalette::Linear(BallsPalette::Ball(Index % ballColorCount), 0.35f));
    }
    return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2,
                                    InWidgetStyle, bParentEnabled);
}

// The SDL front end's Settings panel (SettingsPanel.cpp): a bordered panel, 560 wide and at most 940 high, that
// scrolls; a section heading, a row of options side by side and a hint per block; the Default backend list with each
// backend's benchmark score at the right; the Hybrid device picks; Back at the bottom. Unreal's Graphics page
// button stands where SDL lists its Renderer and Graphics API.
void SBallsSettingsPanel::Construct(const FArguments& InArgs) {
    FBallsApp* Owner = InArgs._App;
    static constexpr float PanelWidth = 560.0f;
    static constexpr float PanelHeight = 940.0f;
    static constexpr float PanelPadding = 24.0f;
    static constexpr float PanelTitleSize = 40.0f;
    static constexpr float TitleHeight = 56.0f;
    static constexpr float SectionSize = 20.0f;
    static constexpr float SectionHeight = 30.0f;
    static constexpr float OptionHeight = 48.0f;
    static constexpr float OptionGap = 10.0f;
    static constexpr float HintSize = 18.0f;
    static constexpr float HintGap = 8.0f;
    static constexpr float BlockGap = 16.0f;
    static constexpr float BackHeight = 56.0f;
    static constexpr float ScoreInset = 16.0f;
    static constexpr float RunBenchmarkWidth = 240.0f;
    static constexpr float DeviceLabelShare = 0.34f;
    // SDL's button label: 0.45 of the row height, at most 26.
    static constexpr float OptionLabelSize = OptionHeight * 0.45f;
    TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

    auto AddSection = [&Column](const FString& Text, bool bMuted = true) {
        Column->AddSlot().AutoHeight()
        [
            SNew(SBox).HeightOverride(SectionHeight).VAlign(VAlign_Top)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Text))
                .Font(BallsStyle::Font(SectionSize))
                .ColorAndOpacity(BallsPalette::Linear(bMuted ? BallsPalette::TextMuted : BallsPalette::Text))
            ]
        ];
    };
    auto AddHint = [&Column](const FString& Text) {
        Column->AddSlot().AutoHeight().Padding(0.0f, HintGap, 0.0f, BlockGap)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Text))
            .Font(BallsStyle::Font(HintSize))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
            .AutoWrapText(true)
        ];
    };
    struct FOption {
        FString Label;
        TAttribute<bool> bSelected;
        TFunction<void()> OnClicked;
    };
    // One row of options side by side, OptionGap apart, like SDL's columnSlice.
    auto AddOptionRow = [&Column](TArray<FOption> Options) {
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        for (int32 Index = 0; Index < Options.Num(); ++Index) {
            Row->AddSlot().FillWidth(1.0f).Padding(Index == 0 ? 0.0f : OptionGap * 0.5f, 0.0f,
                                                   Index + 1 == Options.Num() ? 0.0f : OptionGap * 0.5f, 0.0f)
            [
                BallsWidgets::Button(FText::FromString(Options[Index].Label), MoveTemp(Options[Index].OnClicked),
                                     OptionLabelSize, true, Options[Index].bSelected)
            ];
        }
        Column->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(OptionHeight)[Row]];
    };

    Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, BlockGap)
    [
        SNew(SBox).HeightOverride(TitleHeight).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Settings")))
            .Font(BallsStyle::Font(PanelTitleSize, true))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
        ]
    ];

    // Layout: the core's orientation options, which the scoring build leaves out (always landscape).
    if (layoutChoiceOffered) {
        AddSection(BallsText::FromUtf8(menuText::layoutHeading));
        TArray<FOption> Options;
        const LayoutOrientation Values[3] = {LayoutOrientation::Auto, LayoutOrientation::Landscape, LayoutOrientation::Portrait};
        const std::vector<UiOption> Labels = orientationOptions(Owner->GetSettings().layout);
        for (int32 Index = 0; Index < 3; ++Index) {
            const LayoutOrientation Orientation = Values[Index];
            Options.Add({BallsText::FromUtf8(Labels[Index].label),
                         TAttribute<bool>::CreateLambda([Owner, Orientation] { return Owner->GetSettings().layout == Orientation; }),
                         [Owner, Orientation] {
                             Settings Next = Owner->GetSettings();
                             Next.layout = Orientation;
                             Owner->UpdateSettings(Next);
                         }});
        }
        AddOptionRow(MoveTemp(Options));
        AddHint(BallsText::FromUtf8(menuText::layoutHint));
    }

    // Display: VSync.
    AddSection(BallsText::FromUtf8(menuText::displayHeading));
    AddSection(BallsText::FromUtf8(menuText::vsyncLabel), false);
    AddOptionRow({{TEXT("Off"), TAttribute<bool>::CreateLambda([Owner] { return !Owner->IsVSyncEnabled(); }),
                   [Owner] { Owner->SetVSyncEnabled(false); }},
                  {TEXT("On"), TAttribute<bool>::CreateLambda([Owner] { return Owner->IsVSyncEnabled(); }),
                   [Owner] { Owner->SetVSyncEnabled(true); }}});
    AddHint(TEXT("Replaces the old V shortcut."));

    // Unreal's own Graphics page, where SDL lists its renderer and graphics API.
    AddSection(TEXT("Graphics"), false);
    AddOptionRow({{TEXT("Graphics settings..."), false, [Owner] { Owner->ShowGraphicsPage(true); }}});
    Column->AddSlot().AutoHeight().Padding(0.0f, HintGap, 0.0f, BlockGap)
    [
        SNew(STextBlock)
        .Text_Lambda([Owner] { return FText::FromString(BallsGraphics::Describe(Owner->GetGraphics())); })
        .Font(BallsStyle::Font(HintSize))
        .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
    ];

    // Physics: the Default backend list, with the machine benchmark's score at the right of each row.
    if (backendChoiceOffered()) {
        AddSection(BallsText::FromUtf8(menuText::physicsHeading));
        {
            TSharedRef<SHorizontalBox> Heading = SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Top)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(BallsText::FromUtf8(menuText::defaultBackendLabel)))
                    .Font(BallsStyle::Font(SectionSize))
                    .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
                ];
            if (InArgs._bAllowBenchmark && Owner->GetScreenFlow().canRunMachineBenchmark()) {
                Heading->AddSlot().AutoWidth().Padding(0.0f, -OptionGap, 0.0f, 0.0f)
                [
                    SNew(SBox).WidthOverride(RunBenchmarkWidth).HeightOverride(SectionHeight + OptionGap * 0.5f)
                    [
                        BallsWidgets::Button(FText::FromString(BallsText::FromUtf8(menuText::runBenchmarkLabel)),
                                             [Owner] { Owner->ActivateFlow(FlowAction::RunMachineBenchmark); },
                                             (SectionHeight + OptionGap * 0.5f) * 0.45f)
                    ]
                ];
            }
            Column->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(SectionHeight)[Heading]];
        }
        auto BackendRowsNow = [Owner]() {
            const std::optional<BenchmarkRecord>& Record = Owner->GetBenchmark().GetRecord();
            return defaultBackendRows(Owner->GetPreferredBackend(),
                                      Record ? std::span<const CandidateResult>(Record->results) : std::span<const CandidateResult>());
        };
        const std::vector<BackendRow> InitialRows = BackendRowsNow();
        for (size_t RowIndex = 0; RowIndex < InitialRows.size(); ++RowIndex) {
            const BackendKind Kind = InitialRows[RowIndex].kind;
            auto Option = [BackendRowsNow, Kind]() {
                for (const BackendRow& Row : BackendRowsNow()) {
                    if (Row.kind == Kind) {
                        return Row.option;
                    }
                }
                return UiOption{};
            };
            // A disabled row says why in its label; an enabled one shows its score at the right.
            const UiOption Initial = InitialRows[RowIndex].option;
            const FString Label = BallsText::FromUtf8(
                !Initial.enabled && !Initial.detail.empty() ? Initial.label + " - " + Initial.detail : Initial.label);
            Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, OptionGap)
            [
                SNew(SBox).HeightOverride(OptionHeight)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        BallsWidgets::Button(FText::FromString(Label), [Owner, Kind] { Owner->SetPreferredBackend(Kind); },
                                             OptionLabelSize, Initial.enabled,
                                             TAttribute<bool>::CreateLambda([Option] { return Option().selected; }))
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.0f, 0.0f, ScoreInset, 0.0f)
                    [
                        SNew(STextBlock)
                        .Visibility(EVisibility::HitTestInvisible)
                        .Text_Lambda([Option] {
                            const UiOption Now = Option();
                            return FText::FromString(Now.enabled ? BallsText::FromUtf8(Now.detail) : FString());
                        })
                        .Font(BallsStyle::Font(HintSize))
                        .ColorAndOpacity_Lambda([Option] {
                            return FSlateColor(BallsPalette::Linear(Option().selected ? BallsPalette::Panel : BallsPalette::TextMuted));
                        })
                    ]
                ]
            ];
        }
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, BlockGap - OptionGap)[SNullWidget::NullWidget];
    }

    // Devices: one row per physical device, via the core's device pick rows. They belong to Hybrid, so they go in a
    // box of their own that collapses unless Hybrid is the default (showsDevicePicks); the Add* helpers fill it while
    // Column points at it.
    TSharedRef<SVerticalBox> DevicesBox = SNew(SVerticalBox);
    DevicesBox->SetVisibility(TAttribute<EVisibility>::CreateLambda([Owner] {
        return showsDevicePicks(Owner->GetPreferredBackend()) ? EVisibility::Visible : EVisibility::Collapsed;
    }));
    Column->AddSlot().AutoHeight()[DevicesBox];
    const TSharedRef<SVerticalBox> SettingsColumn = Column;
    Column = DevicesBox;
    AddSection(BallsText::FromUtf8(menuText::devicesLabel), false);
    auto DeviceRowsNow = [Owner]() { return devicePickRows(displayedPicks(Owner->GetPreferredBackend())); };
    const std::vector<DevicePickRow> InitialDeviceRows = DeviceRowsNow();
    for (size_t DeviceIndex = 0; DeviceIndex < InitialDeviceRows.size(); ++DeviceIndex) {
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(DeviceLabelShare).VAlign(VAlign_Center).Padding(0.0f, 0.0f, OptionGap, 0.0f)
        [
            SNew(STextBlock)
            .Text_Lambda([DeviceRowsNow, DeviceIndex] {
                const std::vector<DevicePickRow> Rows = DeviceRowsNow();
                if (DeviceIndex >= Rows.size()) {
                    return FText::GetEmpty();
                }
                return FText::FromString(BallsText::FromUtf8(Rows[DeviceIndex].label));
            })
            .Font(BallsStyle::Font(HintSize))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
        ];
        const size_t OptionCount = InitialDeviceRows[DeviceIndex].options.size();
        const float ButtonShare = (1.0f - DeviceLabelShare) / static_cast<float>(OptionCount);
        for (size_t OptionIndex = 0; OptionIndex < OptionCount; ++OptionIndex) {
            Row->AddSlot().FillWidth(ButtonShare).Padding(OptionIndex == 0 ? 0.0f : OptionGap * 0.5f, 0.0f,
                                                         OptionIndex + 1 == OptionCount ? 0.0f : OptionGap * 0.5f, 0.0f)
            [
                BallsWidgets::Button(
                    TAttribute<FText>::CreateLambda([DeviceRowsNow, DeviceIndex, OptionIndex] {
                        const std::vector<DevicePickRow> Rows = DeviceRowsNow();
                        if (DeviceIndex >= Rows.size() || OptionIndex >= Rows[DeviceIndex].options.size()) {
                            return FText::GetEmpty();
                        }
                        return FText::FromString(BallsText::FromUtf8(Rows[DeviceIndex].options[OptionIndex].label));
                    }),
                    [Owner, DeviceRowsNow, DeviceIndex, OptionIndex] {
                        const std::vector<DevicePickRow> Rows = DeviceRowsNow();
                        if (DeviceIndex >= Rows.size() || OptionIndex >= Rows[DeviceIndex].options.size()) {
                            return;
                        }
                        const std::optional<std::vector<BackendPick>> Next =
                            pickDeviceOption(displayedPicks(Owner->GetPreferredBackend()), Rows, DeviceIndex, OptionIndex);
                        if (!Next) {
                            return;
                        }
                        setBackendPicks(*Next);
                        refreshBackendStatuses();
                        Owner->SetPreferredBackend(kindForPicks(*Next));
                    },
                    HintSize,
                    TAttribute<bool>::CreateLambda([DeviceRowsNow, DeviceIndex, OptionIndex] {
                        const std::vector<DevicePickRow> Rows = DeviceRowsNow();
                        if (DeviceIndex >= Rows.size() || OptionIndex >= Rows[DeviceIndex].options.size()) {
                            return false;
                        }
                        return Rows[DeviceIndex].options[OptionIndex].enabled;
                    }),
                    TAttribute<bool>::CreateLambda([DeviceRowsNow, DeviceIndex, OptionIndex] {
                        const std::vector<DevicePickRow> Rows = DeviceRowsNow();
                        if (DeviceIndex >= Rows.size() || OptionIndex >= Rows[DeviceIndex].options.size()) {
                            return false;
                        }
                        return Rows[DeviceIndex].options[OptionIndex].selected;
                    }))
            ];
        }
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, OptionIndexGap(DeviceIndex, InitialDeviceRows.size(), OptionGap))
        [
            SNew(SBox).HeightOverride(OptionHeight)[Row]
        ];
    }
    AddHint(BallsText::FromUtf8(menuText::devicesHint));
    Column = SettingsColumn;

    Column->AddSlot().AutoHeight()
    [
        SNew(SBox).HeightOverride(BackHeight)
        [
            BallsWidgets::Button(FText::FromString(TEXT("Back")), [Owner] { Owner->ActivateFlow(FlowAction::Back); },
                                 BackHeight * 0.45f)
        ]
    ];

    ChildSlot
    [
        SNew(SBox)
        .WidthOverride(PanelWidth)
        .MaxDesiredHeight(PanelHeight)
        [
            SNew(SBorder)
            .BorderImage(BallsStyle::Panel())
            .Padding(0.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot().Padding(PanelPadding)[Column]
            ]
        ]
    ];
}

void SBallsSettingsPage::Construct(const FArguments& InArgs) {
    FBallsApp* Owner = InArgs._App;
    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(BallsStyle::White()).BorderBackgroundColor(BallsPalette::Linear(BallsPalette::Background))
        ]
        + SOverlay::Slot()
        [
            // The panel is centered and keeps 16 clear of the window edges, scrolling when the window is shorter.
            SNew(SSafeZone)
            .IsTitleSafe(true)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .Padding(FMargin(16.0f))
            [
                SNew(SBallsSettingsPanel).App(Owner)
            ]
        ]
    ];
}

void SBallsLevelSelect::RebuildList() {
    const float Offset = List->GetScrollOffset();
    List->ClearChildren();
    FBallsApp* Owner = App;
    const auto& Rows = Owner->GetScreenFlow().levelRows();
    BuiltRows.clear();
    for (const FlowLevelRow& Row : Rows) {
        BuiltRows += Row.name + (Row.highlighted ? "*\n" : "\n");
    }
    for (int32 Index = 0; Index < static_cast<int32>(Rows.size()); ++Index) {
        const FlowLevelRow& Row = Rows[static_cast<size_t>(Index)];
        const FString Name = BallsText::FromUtf8(Row.name);
        List->AddSlot().Padding(0.0f, RowGap * 0.5f)
        [
            SNew(SBox).HeightOverride(RowHeight)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    BallsWidgets::Button(FText::FromString(Name), [Owner, Index] { Owner->ActivateFlow(FlowAction::PlayLevel, Index); },
                                         26.0f, true,
                                         TAttribute<bool>::CreateLambda([Owner, Index] {
                                             const auto& R = Owner->GetScreenFlow().levelRows();
                                             if (Index < 0 || Index >= static_cast<int32>(R.size())) {
                                                 return false;
                                             }
                                             return R[static_cast<size_t>(Index)].highlighted;
                                         }))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
                [
                    SizedButton(110.0f, RowHeight,
                                BallsWidgets::Button(FText::FromString(TEXT("Edit")),
                                                     [Owner, Index] { Owner->ActivateFlow(FlowAction::EditLevel, Index); }, 26.0f))
                ]
            ]
        ];
    }
    if (Rows.empty()) {
        List->AddSlot()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No levels found")))
            .Font(BallsStyle::Font(28.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
            .Justification(ETextJustify::Center)
        ];
    }
    if (const std::optional<int> ScrollRow = Owner->GetScreenFlow().takeScrollToRow()) {
        List->SetScrollOffset(FMath::Max(0.0f, static_cast<float>(*ScrollRow) * (RowHeight + RowGap) - RowHeight));
    } else {
        List->SetScrollOffset(Offset);
    }
}

void SBallsLevelSelect::Refresh() {
    RebuildList();
}

void SBallsLevelSelect::Highlight(const FString& LevelName) {
    (void)LevelName;
}

EActiveTimerReturnType SBallsLevelSelect::Poll(double, float) {
    std::string Rows;
    for (const FlowLevelRow& Row : App->GetScreenFlow().levelRows()) {
        Rows += Row.name + (Row.highlighted ? "*\n" : "\n");
    }
    if (Rows != BuiltRows) {
        RebuildList();
    }
    return EActiveTimerReturnType::Continue;
}

void SBallsLevelSelect::Construct(const FArguments& InArgs) {
    App = InArgs._App;
    Mode = InArgs._Mode;
    FBallsApp* Owner = App;
    List = SNew(SScrollBox);
    RebuildList();
    RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SBallsLevelSelect::Poll));

    auto Action = [](const TCHAR* Label, TFunction<void()> OnClicked) {
        return SNew(SBox).WidthOverride(200.0f).HeightOverride(56.0f)
            [BallsWidgets::Button(FText::FromString(Label), MoveTemp(OnClicked), 25.0f)];
    };

    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(BallsStyle::White()).BorderBackgroundColor(BallsPalette::Linear(BallsPalette::Background))
        ]
        + SOverlay::Slot()
        [
            // The background above runs under the display cutout; the content stays inside the safe area.
            SNew(SSafeZone)
            .IsTitleSafe(true)
            .HAlign(HAlign_Center)
            .Padding(FMargin(0.0f, 40.0f))
            [
                SNew(SBox).WidthOverride(ListWidth)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 30.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([Owner] { return FText::FromString(BallsText::FromUtf8(Owner->GetScreenFlow().title())); })
                        .Font(BallsStyle::Font(52.0f, true))
                        .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Accent))
                    ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        List.ToSharedRef()
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
                    [
                        // So a tester knows where to drop files.
                        SNew(STextBlock)
                        .Text_Lambda([Owner] { return FText::FromString(BallsText::FromUtf8(Owner->GetScreenFlow().footnote())); })
                        .Font(BallsStyle::Font(16.0f))
                        .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
                        .AutoWrapText(true)
                    ]
                    // As in the SDL front end: Import and Open Folder under the folder line, then Back and New Level,
                    // each pair at the two edges of the list.
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            Action(TEXT("Import"), [Owner] { Owner->OpenLevelImportDialog(); })
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            Action(TEXT("Open Folder"), [Owner] { Owner->OpenLevelFolder(Owner->GetScreenFlow().mode()); })
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 24.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            Action(TEXT("Back"), [Owner] { Owner->ActivateFlow(FlowAction::Back); })
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            Action(TEXT("New Level"), [Owner] { Owner->ActivateFlow(FlowAction::NewLevel); })
                        ]
                    ]
                ]
            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 24.0f)
        [
            SNew(SBorder)
            .BorderImage(BallsStyle::White())
            .BorderBackgroundColor_Lambda([Owner] {
                const FBallsToast& Toast = Owner->GetToast();
                return BallsPalette::Linear(Toast.bIsError ? BallsPalette::Danger : BallsPalette::Control,
                                            FMath::Min(1.0f, Toast.SecondsLeft));
            })
            .Padding(20.0f, 10.0f)
            .Visibility_Lambda([Owner] { return Owner->GetToast().SecondsLeft > 0.0f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [
                SNew(STextBlock)
                .Text_Lambda([Owner] { return FText::FromString(Owner->GetToast().Text); })
                .Font(BallsStyle::Font(24.0f, true))
                .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
            ]
        ]
    ];
}
