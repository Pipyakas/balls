#include "UI/SBallsGraphicsPage.h"

#include "BallsApp.h"
#include "BallsGraphics.h"
#include "BallsPalette.h"
#include "UI/BallsStyle.h"
#include "UI/SBallsMenus.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace {

constexpr float PageWidth = 980.0f;
constexpr float PageHeight = 940.0f;
constexpr float PagePadding = 24.0f;
constexpr float TitleSize = 40.0f;
constexpr float StatusSize = 18.0f;
constexpr float SectionSize = 20.0f;
constexpr float SectionHeight = 34.0f;
constexpr float RowHeight = 40.0f;
constexpr float RowGap = 8.0f;
constexpr float LabelWidth = 230.0f;
constexpr float OptionGap = 8.0f;
constexpr float OptionLabelSize = 17.0f;
constexpr float HintSize = 16.0f;
constexpr float BlockGap = 18.0f;
constexpr float CloseHeight = 52.0f;
constexpr float DimAlpha = 0.75f;

struct FChoice {
    FString Label;
    TFunction<bool(const FBallsGraphicsSettings&)> IsSelected;
    TFunction<void(FBallsGraphicsSettings&)> Apply;
    FString Unavailable;
};

}

void SBallsGraphicsPage::Construct(const FArguments& InArgs) {
    FBallsApp* Owner = InArgs._App;
    TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

    auto AddText = [&Column](TAttribute<FText> Text, float Size, FColor Color, float Bottom) {
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Bottom)
        [
            SNew(STextBlock).Text(Text).Font(BallsStyle::Font(Size)).ColorAndOpacity(BallsPalette::Linear(Color)).AutoWrapText(true)
        ];
    };
    auto AddSection = [&](const FString& Title) {
        Column->AddSlot().AutoHeight()
        [
            SNew(SBox).HeightOverride(SectionHeight).VAlign(VAlign_Top)
            [
                SNew(STextBlock).Text(FText::FromString(Title)).Font(BallsStyle::Font(SectionSize))
                .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
            ]
        ];
    };
    // A label at the left and the choices side by side; a choice the machine cannot run is disabled.
    auto AddRow = [&Column, Owner](const FString& Label, TArray<FChoice> Choices) {
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(LabelWidth)
                [
                    SNew(STextBlock).Text(FText::FromString(Label)).Font(BallsStyle::Font(OptionLabelSize + 1.0f))
                    .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
                ]
            ];
        for (int32 Index = 0; Index < Choices.Num(); ++Index) {
            FChoice Choice = Choices[Index];
            const bool bAvailable = Choice.Unavailable.IsEmpty();
            Row->AddSlot().FillWidth(1.0f).Padding(Index == 0 ? 0.0f : OptionGap * 0.5f, 0.0f,
                                                   Index + 1 == Choices.Num() ? 0.0f : OptionGap * 0.5f, 0.0f)
            [
                BallsWidgets::Button(FText::FromString(Choice.Label),
                                     [Owner, Apply = Choice.Apply] {
                                         FBallsGraphicsSettings Next = Owner->GetGraphics();
                                         Apply(Next);
                                         Owner->SetGraphics(Next);
                                     },
                                     OptionLabelSize, bAvailable,
                                     TAttribute<bool>::CreateLambda([Owner, IsSelected = Choice.IsSelected] {
                                         return IsSelected(Owner->GetGraphics());
                                     }))
            ];
        }
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[SNew(SBox).HeightOverride(RowHeight)[Row]];
    };
    auto AddRowWidget = [&Column](const FString& Label, TSharedRef<SWidget> Widget) {
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(LabelWidth)
                [
                    SNew(STextBlock).Text(FText::FromString(Label)).Font(BallsStyle::Font(OptionLabelSize + 1.0f))
                    .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                Widget
            ];
        Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)[SNew(SBox).HeightOverride(RowHeight)[Row]];
    };
    // One choice per value of a settings field, selected while the field holds it.
    auto Choices = []<typename Value>(const TArray<TPair<FString, Value>>& Values, Value FBallsGraphicsSettings::*Field,
                                      TFunction<FString(Value)> Unavailable = nullptr) {
        TArray<FChoice> Result;
        for (const TPair<FString, Value>& Entry : Values) {
            const Value Option = Entry.Value;
            Result.Add({Entry.Key, [Field, Option](const FBallsGraphicsSettings& Settings) { return Settings.*Field == Option; },
                        [Field, Option](FBallsGraphicsSettings& Settings) { Settings.*Field = Option; },
                        Unavailable ? Unavailable(Option) : FString()});
        }
        return Result;
    };
    auto Hint = [&AddText](const FString& Text) {
        if (!Text.IsEmpty()) {
            AddText(FText::FromString(Text), HintSize, BallsPalette::TextMuted, BlockGap);
        }
    };

    Column->AddSlot().AutoHeight()
    [
        SNew(SBox).HAlign(HAlign_Center)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Graphics"))).Font(BallsStyle::Font(TitleSize, true))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Text))
        ]
    ];
    // What the last change did to the frame, so a setting can be judged without leaving the page.
    AddText(TAttribute<FText>::CreateLambda([Owner] {
                const FrameTimings& Frames = Owner->GetFrameTimings();
                return FText::FromString(FString::Printf(TEXT("%s    %.0f FPS    GPU %.2f ms"),
                                                         *BallsGraphics::Describe(Owner->GetGraphics()),
                                                         Frames.framesPerSecond(), Owner->GetLatestSample().gpuMs));
            }),
            StatusSize, BallsPalette::Accent, BlockGap);

    AddSection(TEXT("Resolution"));
    {
        TArray<TPair<FString, int32>> Percentages;
        for (const int32 Percentage : BallsGraphics::ScreenPercentages()) {
            Percentages.Add({FString::Printf(TEXT("%d%%"), Percentage), Percentage});
        }
        AddRow(TEXT("Screen resolution"), Choices(Percentages, &FBallsGraphicsSettings::ScreenPercentage));
    }
    AddRow(TEXT("Dynamic resolution"),
           Choices(TArray<TPair<FString, bool>>{{TEXT("Off"), false}, {TEXT("On"), true}}, &FBallsGraphicsSettings::bDynamicResolution));
    AddRowWidget(TEXT("Target FPS"),
                 BallsWidgets::Stepper([Owner] { return Owner->GetGraphics().TargetFps; },
                                       [Owner](int32 Value) {
                                           FBallsGraphicsSettings Next = Owner->GetGraphics();
                                           Next.TargetFps = Value;
                                           Owner->SetGraphics(Next);
                                       },
                                       1, BallsGraphics::MaximumTargetFps(), RowHeight));
    Hint(TEXT("These are kept when a preset is picked; dynamic resolution lowers the resolution (down to 50%) whenever a "
              "frame would miss the target FPS."));

    AddSection(TEXT("Preset"));
    {
        TArray<FChoice> Presets;
        for (int32 Level = 0; Level < FBallsGraphicsSettings::LevelCount; ++Level) {
            Presets.Add({BallsGraphics::PresetName(Level),
                         [Level](const FBallsGraphicsSettings& Settings) { return BallsGraphics::MatchingPreset(Settings) == Level; },
                         [Level](FBallsGraphicsSettings& Settings) { BallsGraphics::ApplyPreset(Level, Settings); }});
        }
        AddRow(TEXT("Overall"), MoveTemp(Presets));
    }
    Hint(TEXT("Low is the flat look with nothing optional; Medium and High add temporal anti-aliasing, screen-space "
              "reflections and shadows; Epic and Cinematic turn on Lumen, Nanite and virtual shadow maps. Changing any "
              "row below makes the settings Custom."));

    AddSection(TEXT("Anti-aliasing"));
    AddRow(TEXT("Method"), Choices(TArray<TPair<FString, EBallsAntiAliasing>>{
                                       {TEXT("Off"), EBallsAntiAliasing::None},
                                       {TEXT("FXAA"), EBallsAntiAliasing::FXAA},
                                       {TEXT("TAA"), EBallsAntiAliasing::TAA},
                                       {TEXT("TAAU"), EBallsAntiAliasing::TAAU},
                                       {TEXT("TSR"), EBallsAntiAliasing::TSR},
                                       {TEXT("FSR"), EBallsAntiAliasing::FSR}},
                                   &FBallsGraphicsSettings::AntiAliasing,
                                   TFunction<FString(EBallsAntiAliasing)>([](EBallsAntiAliasing Method) {
                                       return BallsGraphics::Unavailable(Method);
                                   })));
    {
        FString Notes = TEXT("TAAU, TSR and FSR rebuild a full-resolution frame from a lower screen resolution; Off, FXAA "
                             "and TAA stretch it.");
        const FString NoFsr = BallsGraphics::Unavailable(EBallsAntiAliasing::FSR);
        if (!NoFsr.IsEmpty()) {
            Notes += TEXT(" FSR ") + NoFsr + TEXT(".");
        }
        Hint(Notes);
    }

    AddSection(TEXT("UE5 features"));
    const FString NoLumen = BallsGraphics::UnavailableLumen();
    const FString NoNanite = BallsGraphics::UnavailableNanite();
    const FString NoVirtualShadows = BallsGraphics::UnavailableVirtualShadows();
    AddRow(TEXT("Global illumination"),
           Choices(TArray<TPair<FString, EBallsGlobalIllumination>>{{TEXT("Off"), EBallsGlobalIllumination::None},
                                                                     {TEXT("Lumen"), EBallsGlobalIllumination::Lumen}},
                   &FBallsGraphicsSettings::GlobalIllumination,
                   TFunction<FString(EBallsGlobalIllumination)>([NoLumen](EBallsGlobalIllumination Method) {
                       return Method == EBallsGlobalIllumination::Lumen ? NoLumen : FString();
                   })));
    AddRow(TEXT("Reflections"),
           Choices(TArray<TPair<FString, EBallsReflections>>{{TEXT("Off"), EBallsReflections::None},
                                                              {TEXT("Screen space"), EBallsReflections::ScreenSpace},
                                                              {TEXT("Lumen"), EBallsReflections::Lumen}},
                   &FBallsGraphicsSettings::Reflections, TFunction<FString(EBallsReflections)>([NoLumen](EBallsReflections Method) {
                       return Method == EBallsReflections::Lumen ? NoLumen : FString();
                   })));
    AddRow(TEXT("Shadows"),
           Choices(TArray<TPair<FString, EBallsShadows>>{{TEXT("Off"), EBallsShadows::Off},
                                                          {TEXT("Shadow maps"), EBallsShadows::ShadowMaps},
                                                          {TEXT("Virtual"), EBallsShadows::Virtual}},
                   &FBallsGraphicsSettings::Shadows, TFunction<FString(EBallsShadows)>([NoVirtualShadows](EBallsShadows Method) {
                       return Method == EBallsShadows::Virtual ? NoVirtualShadows : FString();
                   })));
    AddRow(TEXT("Nanite"), Choices(TArray<TPair<FString, bool>>{{TEXT("Off"), false}, {TEXT("On"), true}},
                                   &FBallsGraphicsSettings::bNanite, TFunction<FString(bool)>([NoNanite](bool bOn) {
                                       return bOn ? NoNanite : FString();
                                   })));
    {
        FString Notes = TEXT("Lumen lights the board from the bricks and walls and gives Lumen reflections; virtual "
                             "shadow maps are the UE5 shadows, sharpest with Nanite; Nanite draws the bricks as Nanite "
                             "geometry. The balls are drawn after these passes, so they only change the bricks and the "
                             "walls.");
        if (!NoLumen.IsEmpty() || !NoNanite.IsEmpty()) {
            Notes += FString::Printf(TEXT(" Here Lumen %s, Nanite %s and virtual shadow maps %s."),
                                     NoLumen.IsEmpty() ? TEXT("runs") : *NoLumen, NoNanite.IsEmpty() ? TEXT("runs") : *NoNanite,
                                     NoVirtualShadows.IsEmpty() ? TEXT("run") : *NoVirtualShadows);
        }
        Hint(Notes);
    }

    AddSection(TEXT("Scalability"));
    for (int32 Group = 0; Group < FBallsGraphicsSettings::GroupCount; ++Group) {
        TArray<FChoice> Levels;
        for (int32 Level = 0; Level < FBallsGraphicsSettings::LevelCount; ++Level) {
            Levels.Add({BallsGraphics::PresetName(Level),
                        [Group, Level](const FBallsGraphicsSettings& Settings) { return Settings.Groups[Group] == Level; },
                        [Group, Level](FBallsGraphicsSettings& Settings) { Settings.Groups[Group] = Level; }});
        }
        AddRow(BallsGraphics::GroupName(static_cast<EBallsScalabilityGroup>(Group)), MoveTemp(Levels));
    }
    Hint(TEXT("The engine's own scalability groups, as in its Settings > Engine Scalability menu."));

    Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
    [
        SNew(SBox).WidthOverride(240.0f).HeightOverride(CloseHeight)
        [
            BallsWidgets::Button(FText::FromString(TEXT("Close")), [Owner] { Owner->ShowGraphicsPage(false); }, 24.0f)
        ]
    ];

    ChildSlot
    [
        // A click on the dimmed board around the page closes it; one inside the page stops at the panel.
        SNew(SBorder)
        .BorderImage(BallsStyle::White())
        .BorderBackgroundColor(BallsPalette::Linear(BallsPalette::Background, DimAlpha))
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .OnMouseButtonDown_Lambda([Owner](const FGeometry&, const FPointerEvent&) {
            Owner->ShowGraphicsPage(false);
            return FReply::Handled();
        })
        [
            SNew(SBox).WidthOverride(PageWidth).MaxDesiredHeight(PageHeight)
            [
                SNew(SBorder).BorderImage(BallsStyle::Panel()).Padding(0.0f)
                .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
                [
                    SNew(SScrollBox) + SScrollBox::Slot().Padding(PagePadding)[Column]
                ]
            ]
        ]
    ];
}
