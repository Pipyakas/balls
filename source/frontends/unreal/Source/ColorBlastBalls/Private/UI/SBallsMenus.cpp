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
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include <algorithm>

namespace {

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
                SNew(STextBlock)
                .Text(Label)
                .Font(BallsStyle::Font(FontSize, true))
                .ColorAndOpacity(TextColor)
                .Justification(ETextJustify::Center)
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
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBox).HeightOverride(ControlHeight)
            [
                Button(TAttribute<FText>::CreateLambda([Options, GetIndex, bOpen] {
                           const int32 Index = FMath::Clamp(GetIndex(), 0, Options.Num() - 1);
                           return FText::FromString(Options[Index] + (*bOpen ? TEXT("  ^") : TEXT("  v")));
                       }),
                       [bOpen] { *bOpen = !*bOpen; }, FontSize)
            ]
        ]
        + SVerticalBox::Slot().AutoHeight()[List];
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
    ChildSlot
    [
        SNew(SBox)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("COLOR BLAST")))
                .Font(BallsStyle::Font(TitleSize, true))
                .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Accent))
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, TitleSize * 0.5f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("BALLS")))
                .Font(BallsStyle::Font(TitleSize * 0.7f, true))
                .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, MenuButtonGap * 0.5f)
            [
                MenuButton(TEXT("Normal Mode"), [Owner] { Owner->ShowLevelSelect(GameMode::Normal); })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, MenuButtonGap * 0.5f)
            [
                MenuButton(TEXT("Performance Mode"), [Owner] { Owner->ShowLevelSelect(GameMode::Performance); })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, MenuButtonGap * 0.5f)
            [
                MenuButton(TEXT("Map Editor"), [Owner] { Owner->ShowMapEditor(GameMode::Normal, std::nullopt); })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, MenuButtonGap * 0.5f)
            [
                MenuButton(TEXT("Settings"), [Owner] { Owner->ShowSettings(); })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, MenuButtonGap * 0.5f)
            [
                MenuButton(TEXT("Quit"), [Owner] { Owner->Quit(); })
            ]
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

void SBallsSettingsPanel::Construct(const FArguments& InArgs) {
    FBallsApp* Owner = InArgs._App;
    static constexpr float OptionHeight = 48.0f;
    static constexpr float OptionGap = 8.0f;
    TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

    auto AddHeading = [&Column](const TCHAR* Text) {
        Column->AddSlot().AutoHeight().Padding(0.0f, 22.0f, 0.0f, 8.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Text))
            .Font(BallsStyle::Font(20.0f, true))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Accent))
        ];
    };
    auto AddLabel = [&Column](const TCHAR* Text) {
        Column->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 6.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Text))
            .Font(BallsStyle::Font(18.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
        ];
    };
    auto AddHint = [&Column](const TCHAR* Text) {
        Column->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 8.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Text))
            .Font(BallsStyle::Font(15.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
            .AutoWrapText(true)
        ];
    };
    // A choice among a few values: a vertical list of full-width rows, the current one in accent.
    auto AddOption = [&Column](const FString& Text, TAttribute<bool> bSelected, TFunction<void()> OnClicked,
                               bool bEnabled = true, const FString& Reason = FString()) {
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, OptionGap)
        [
            SNew(SBox).HeightOverride(OptionHeight)
            [
                BallsWidgets::Button(FText::FromString(Text), MoveTemp(OnClicked), 20.0f, bEnabled, bSelected)
            ]
        ];
        if (!Reason.IsEmpty()) {
            Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, OptionGap)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Reason))
                .Font(BallsStyle::Font(15.0f))
                .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
                .AutoWrapText(true)
            ];
        }
    };

    AddHeading(TEXT("Gameplay"));
    AddLabel(TEXT("Ball collisions"));
    AddOption(TEXT("Off"), TAttribute<bool>::CreateLambda([Owner] { return !Owner->GetSettings().ballCollisions; }),
              [Owner] {
                  Settings Next = Owner->GetSettings();
                  Next.ballCollisions = false;
                  Owner->UpdateSettings(Next);
              });
    AddOption(TEXT("On"), TAttribute<bool>::CreateLambda([Owner] { return Owner->GetSettings().ballCollisions; }),
              [Owner] {
                  Settings Next = Owner->GetSettings();
                  Next.ballCollisions = true;
                  Owner->UpdateSettings(Next);
              });
    AddHint(TEXT("Testing only - the game rules have balls pass through each other. Balls bounce off each other "
                 "elastically and keep their constant speed; collisions with balls are not bounces for the 3-reflection "
                 "homing rule. Only the Scalar backend supports them. Turning them on with another backend switches to "
                 "Scalar."));

    AddHeading(TEXT("Layout"));
    AddLabel(TEXT("Orientation"));
    for (const LayoutOrientation Orientation :
         {LayoutOrientation::Auto, LayoutOrientation::Landscape, LayoutOrientation::Portrait}) {
        FString Name = BallsText::FromUtf8(orientationName(Orientation));
        Name[0] = FChar::ToUpper(Name[0]);
        AddOption(Name,
                  TAttribute<bool>::CreateLambda([Owner, Orientation] { return Owner->GetSettings().layout == Orientation; }),
                  [Owner, Orientation] {
                      Settings Next = Owner->GetSettings();
                      Next.layout = Orientation;
                      Owner->UpdateSettings(Next);
                  });
    }
    AddHint(TEXT("Auto follows the window shape. On desktop a forced orientation resizes the window (portrait: 90% of "
                 "the display height at 9:16; landscape: 1440 x 900). The map editor always uses a landscape frame."));

    AddHeading(TEXT("Display"));
    AddLabel(TEXT("VSync"));
    AddOption(TEXT("Off"), TAttribute<bool>::CreateLambda([Owner] { return !Owner->IsVSyncEnabled(); }),
              [Owner] { Owner->SetVSyncEnabled(false); });
    AddOption(TEXT("On"), TAttribute<bool>::CreateLambda([Owner] { return Owner->IsVSyncEnabled(); }),
              [Owner] { Owner->SetVSyncEnabled(true); });

    // The Unreal port's own setting: the post chain costs more than the scene, so it starts at Minimum.
    AddHeading(TEXT("Graphics"));
    AddLabel(TEXT("Quality"));
    for (const EBallsGraphics Level : BallsGraphics::All()) {
        AddOption(BallsGraphics::Name(Level),
                  TAttribute<bool>::CreateLambda([Owner, Level] { return Owner->GetGraphicsQuality() == Level; }),
                  [Owner, Level] { Owner->SetGraphicsQuality(Level); });
    }
    AddHint(TEXT("Minimum turns bloom and temporal anti-aliasing off, fixes the exposure and draws at 70% screen "
                 "resolution; Balanced adds FXAA and bloom at 85%; High is the engine default look at 100%. The same "
                 "list is in the Performance gadget's control column."));

    AddHeading(TEXT("Physics"));
    AddLabel(TEXT("Default backend"));
    // Each backend shows its last machine benchmark score, muted ("First launch: machine benchmark").
    const std::optional<BenchmarkRecord>& Record = Owner->GetBenchmark().GetRecord();
    // Scalar is listed only where nothing faster loads (design Settings, Physics).
    for (const BackendKind Kind : selectableBackends()) {
        const bool bAvailable = isBackendAvailable(Kind);
        AddOption(BallsText::FromUtf8(backendKindName(Kind)),
                  TAttribute<bool>::CreateLambda([Owner, Kind] { return Owner->GetPreferredBackend() == Kind; }),
                  [Owner, Kind] { Owner->SetPreferredBackend(Kind); }, bAvailable,
                  bAvailable ? FString() : BallsText::FromUtf8(backendUnavailableReason(Kind)));
        if (!Record) {
            continue;
        }
        for (const CandidateResult& Result : Record->results) {
            if (Result.candidate.kind != Kind) {
                continue;
            }
            FString Score = TEXT("not measured");
            if (Result.status == CandidateStatus::Done) {
                Score = Result.estimatedBalls > 0.0
                            ? FString::Printf(TEXT("%s balls"), *BallsText::FromUtf8(shortBallCount(Result.estimatedBalls)))
                            : FString::Printf(TEXT("%.0f M ball-steps/s"), Result.ballStepsPerSecond / 1'000'000.0);
            } else if (Result.status == CandidateStatus::Failed) {
                Score = FString::Printf(TEXT("failed: %s"), *BallsText::FromUtf8(Result.failure));
            } else if (Result.status == CandidateStatus::Unavailable) {
                Score = TEXT("not available");
            }
            AddHint(*Score);
        }
    }
    // design Settings, Physics, Devices: one row per physical device with Off and one button per backend it runs. Each
    // device runs at most one backend; two or more make Hybrid, one runs that backend alone, and the last device on
    // cannot be turned off.
    AddLabel(TEXT("Devices"));
    const auto ShownPicks = [Owner] { return displayedPicks(Owner->GetPreferredBackend()); };
    const auto PickedKind = [ShownPicks](const std::string& Device) -> std::optional<BackendKind> {
        const std::vector<BackendPick> Picks = ShownPicks();
        const auto Found = std::ranges::find(Picks, Device, &BackendPick::device);
        return Found == Picks.end() ? std::nullopt : std::optional<BackendKind>(Found->kind);
    };
    const auto Pick = [Owner, ShownPicks](const std::string& Device, std::optional<BackendKind> Kind) {
        const std::vector<BackendPick> Next = withDevicePick(ShownPicks(), Device, Kind);
        if (Next.empty()) {
            return;
        }
        setBackendPicks(Next);
        refreshBackendStatuses();
        Owner->SetPreferredBackend(kindForPicks(Next));
    };
    for (const PhysicalDevice& Device : physicalDevices()) {
        const std::string Id = Device.id;
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(0.34f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, OptionGap, 0.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(BallsText::FromUtf8(physicalDeviceLabel(Device))))
            .Font(BallsStyle::Font(17.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
        ];
        const float ButtonShare = 0.66f / static_cast<float>(pickableKinds(Device).size() + 1);
        Row->AddSlot().FillWidth(ButtonShare).Padding(0.0f, 0.0f, OptionGap, 0.0f)
        [
            BallsWidgets::Button(FText::FromString(TEXT("Off")), [Pick, Id] { Pick(Id, std::nullopt); }, 17.0f,
                                 TAttribute<bool>::CreateLambda([PickedKind, ShownPicks, Id] {
                                     return !(PickedKind(Id) && ShownPicks().size() == 1);
                                 }),
                                 TAttribute<bool>::CreateLambda([PickedKind, Id] { return !PickedKind(Id); }))
        ];
        for (const BackendKind Kind : pickableKinds(Device)) {
            const DeviceBackendOption* Option = Device.option(Kind);
            const bool bAvailable = Option && deviceBackendStatus(Kind, Option->pluginDevice).available;
            Row->AddSlot().FillWidth(ButtonShare).Padding(0.0f, 0.0f, OptionGap, 0.0f)
            [
                BallsWidgets::Button(FText::FromString(BallsText::FromUtf8(backendKindName(Kind))),
                                     [Pick, Id, Kind] { Pick(Id, Kind); }, 17.0f, bAvailable,
                                     TAttribute<bool>::CreateLambda([PickedKind, Id, Kind] { return PickedKind(Id) == Kind; }))
            ];
        }
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, OptionGap)[SNew(SBox).HeightOverride(OptionHeight)[Row]];
    }
    AddHint(TEXT("One backend per device. Two or more devices run as Hybrid, splitting the balls between them; one runs "
                 "that backend alone."));

    if (InArgs._bAllowBenchmark) {
        Column->AddSlot().AutoHeight().Padding(0.0f, OptionGap, 0.0f, OptionGap)
        [
            SNew(SBox).HeightOverride(OptionHeight)
            [
                BallsWidgets::Button(FText::FromString(TEXT("Run machine benchmark")), [Owner] {
                    Owner->SetBenchmarkReturnToSettings(true);
                    Owner->ShowBenchmark();
                }, 20.0f)
            ]
        ];
        AddHint(TEXT("Measures every backend on this machine and makes the one that holds the most balls at 60 FPS the "
                     "default. It never runs by itself."));
    }

    ChildSlot[SNew(SScrollBox) + SScrollBox::Slot()[Column]];
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
            SNew(SSafeZone)
            .IsTitleSafe(true)
            .HAlign(HAlign_Center)
            .Padding(FMargin(0.0f, 40.0f))
            [
                SNew(SBox).WidthOverride(ListWidth)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Settings")))
                        .Font(BallsStyle::Font(52.0f, true))
                        .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Accent))
                    ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        SNew(SBallsSettingsPanel).App(Owner)
                    ]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 24.0f, 0.0f, 0.0f)
                    [
                        SizedButton(200.0f, 56.0f, BallsWidgets::Button(FText::FromString(TEXT("Back")),
                                                                         [Owner] { Owner->ShowMainMenu(); }, 24.0f))
                    ]
                ]
            ]
        ]
    ];
}

FString SBallsLevelSelect::Signature() const {
    FString Result;
    for (const LevelFiles& Files : Levels) {
        std::error_code Error;
        const auto Bricks = std::filesystem::last_write_time(Files.bricksPath, Error);
        const auto Launchers = std::filesystem::last_write_time(Files.launchersPath, Error);
        Result += FString::Printf(TEXT("%s:%lld:%lld;"), *BallsText::FromUtf8(Files.name),
                                  static_cast<long long>(Bricks.time_since_epoch().count()),
                                  static_cast<long long>(Launchers.time_since_epoch().count()));
    }
    return Result;
}

void SBallsLevelSelect::RebuildList() {
    const float Offset = List->GetScrollOffset();
    List->ClearChildren();
    FBallsApp* Owner = App;
    const GameMode LevelMode = Mode;
    for (const LevelFiles& Files : Levels) {
        const FString Name = BallsText::FromUtf8(Files.name);
        List->AddSlot().Padding(0.0f, RowGap * 0.5f)
        [
            SNew(SBox).HeightOverride(RowHeight)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    BallsWidgets::Button(FText::FromString(Name), [Owner, LevelMode, Files] { Owner->StartGame(LevelMode, Files); },
                                         24.0f, true,
                                         TAttribute<bool>::CreateLambda([this, Name] {
                                             return Name == HighlightName && FPlatformTime::Seconds() < HighlightUntil;
                                         }))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
                [
                    SizedButton(110.0f, RowHeight,
                                BallsWidgets::Button(FText::FromString(TEXT("Edit")),
                                                     [Owner, LevelMode, Files] { Owner->ShowMapEditor(LevelMode, Files); }, 20.0f))
                ]
            ]
        ];
    }
    if (Levels.empty()) {
        List->AddSlot()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No levels found")))
            .Font(BallsStyle::Font(28.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
            .Justification(ETextJustify::Center)
        ];
    }
    List->SetScrollOffset(Offset);
}

void SBallsLevelSelect::Refresh() {
    Levels = findLevels(App->LevelDirectory(Mode));
    ShownSignature = Signature();
    RebuildList();
}

void SBallsLevelSelect::Highlight(const FString& LevelName) {
    HighlightName = LevelName;
    HighlightUntil = FPlatformTime::Seconds() + 2.0;
    for (int32 Index = 0; Index < static_cast<int32>(Levels.size()); ++Index) {
        if (BallsText::FromUtf8(Levels[Index].name) == LevelName) {
            // Rows are RowHeight + RowGap apart; show the new one a little below the top.
            List->SetScrollOffset(FMath::Max(0.0f, static_cast<float>(Index) * (RowHeight + RowGap) - RowHeight));
            break;
        }
    }
}

// The list follows the folder: a level added, changed or removed while this screen is open shows up within a poll.
EActiveTimerReturnType SBallsLevelSelect::Poll(double, float) {
    std::vector<LevelFiles> Current = findLevels(App->LevelDirectory(Mode));
    std::swap(Levels, Current);
    const FString NewSignature = Signature();
    if (NewSignature != ShownSignature) {
        ShownSignature = NewSignature;
        RebuildList();
    } else {
        Levels = std::move(Current);
    }
    return EActiveTimerReturnType::Continue;
}

void SBallsLevelSelect::Construct(const FArguments& InArgs) {
    App = InArgs._App;
    Mode = InArgs._Mode;
    FBallsApp* Owner = App;
    const GameMode LevelMode = Mode;
    List = SNew(SScrollBox);
    Refresh();
    RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SBallsLevelSelect::Poll));

    const FString FolderPath = FString(UTF8_TO_TCHAR(App->LevelDirectory(Mode).string().c_str()));
    auto Action = [](const TCHAR* Label, TFunction<void()> OnClicked) {
        return SNew(SBox).WidthOverride(200.0f).HeightOverride(56.0f)
            [BallsWidgets::Button(FText::FromString(Label), MoveTemp(OnClicked), 22.0f)];
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
                        .Text(FText::FromString(Mode == GameMode::Normal ? TEXT("Normal Mode") : TEXT("Performance Mode")))
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
                        .Text(FText::FromString(TEXT("Levels folder: ") + FolderPath))
                        .Font(BallsStyle::Font(16.0f))
                        .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
                        .AutoWrapText(true)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            Action(TEXT("Back"), [Owner] { Owner->ShowMainMenu(); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            Action(TEXT("New Level"), [Owner, LevelMode] { Owner->ShowMapEditor(LevelMode, std::nullopt); })
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            Action(TEXT("Import"), [Owner] { Owner->OpenLevelImportDialog(); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            Action(TEXT("Open Folder"), [Owner, LevelMode] { Owner->OpenLevelFolder(LevelMode); })
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
                return BallsPalette::Linear(Toast.bIsError ? BallsPalette::Danger : BallsPalette::PanelLight,
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
