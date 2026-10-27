#include "UI/SBallsMenus.h"

#include "BallsApp.h"
#include "BallsPalette.h"
#include "BallsText.h"
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
    constexpr float OptionHeight = 48.0f;
    constexpr float OptionGap = 8.0f;
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

    AddHeading(TEXT("Physics"));
    AddLabel(TEXT("Backend"));
    for (const BackendKind Kind : allBackendKinds) {
        const bool bAvailable = isBackendAvailable(Kind);
        AddOption(BallsText::FromUtf8(backendKindName(Kind)),
                  TAttribute<bool>::CreateLambda([Owner, Kind] { return Owner->GetPreferredBackend() == Kind; }),
                  [Owner, Kind] { Owner->ChooseBackend(Kind); }, bAvailable,
                  bAvailable ? FString() : BallsText::FromUtf8(backendUnavailableReason(Kind)));
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

void SBallsLevelSelect::Construct(const FArguments& InArgs) {
    FBallsApp* Owner = InArgs._App;
    const GameMode Mode = InArgs._Mode;
    TSharedRef<SScrollBox> List = SNew(SScrollBox);
    for (const LevelFiles& Files : InArgs._Levels) {
        List->AddSlot().Padding(0.0f, RowGap * 0.5f)
        [
            SNew(SBox).HeightOverride(RowHeight)
            [
                BallsWidgets::Button(FText::FromString(BallsText::FromUtf8(Files.name)),
                                     [Owner, Mode, Files] { Owner->StartGame(Mode, Files); }, 24.0f)
            ]
        ];
    }
    if (InArgs._Levels.empty()) {
        List->AddSlot()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("No levels found")))
            .Font(BallsStyle::Font(28.0f))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
            .Justification(ETextJustify::Center)
        ];
    }

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
                        List
                    ]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 30.0f, 0.0f, 0.0f)
                    [
                        SizedButton(200.0f, 56.0f, BallsWidgets::Button(FText::FromString(TEXT("Back")),
                                                                         [Owner] { Owner->ShowMainMenu(); }, 24.0f))
                    ]
                ]
            ]
        ]
    ];
}
