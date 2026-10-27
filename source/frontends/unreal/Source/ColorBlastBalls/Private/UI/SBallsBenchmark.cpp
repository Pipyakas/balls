#include "UI/SBallsBenchmark.h"

#include "BallsApp.h"
#include "BallsBenchmark.h"
#include "BallsPalette.h"
#include "BallsText.h"
#include "UI/BallsStyle.h"
#include "UI/SBallsMenus.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace {

constexpr float TitleSize = 44.0f;
constexpr float SubtitleSize = 18.0f;
constexpr float RowTextSize = 22.0f;
constexpr float RowHeight = 64.0f;
constexpr float RowGap = 12.0f;
constexpr float ListWidth = 760.0f;
constexpr float RowPadding = 20.0f;
constexpr float ProgressWidth = 220.0f;
constexpr float ProgressHeight = 8.0f;
constexpr float ButtonWidth = 220.0f;
constexpr float ButtonHeight = 56.0f;
constexpr double PollSeconds = 0.1;
constexpr double BallStepsPerMillion = 1'000'000.0;

FString ResultText(const CandidateResult& Result) {
    switch (Result.status) {
    case CandidateStatus::Done:
        return FString::Printf(TEXT("%.0f M ball-steps/s"), Result.ballStepsPerSecond / BallStepsPerMillion);
    case CandidateStatus::Failed:
        return FString::Printf(TEXT("failed: %s"), *BallsText::FromUtf8(Result.failure));
    case CandidateStatus::Unavailable:
        return TEXT("not available");
    case CandidateStatus::Running:
        return FString();
    default:
        return TEXT("waiting");
    }
}

FSlateColor ResultColor(const CandidateResult& Result) {
    switch (Result.status) {
    case CandidateStatus::Failed:
        return BallsPalette::Linear(BallsPalette::Danger);
    case CandidateStatus::Done:
        return BallsPalette::Linear(BallsPalette::Text);
    default:
        return BallsPalette::Linear(BallsPalette::TextMuted);
    }
}

TSharedRef<SWidget> ProgressBar(float Progress) {
    // Slate has no flat progress bar in this style, so the fill is a tinted box whose width follows progress.
    return SNew(SBox).WidthOverride(ProgressWidth).HeightOverride(ProgressHeight)
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                SNew(SBorder).BorderImage(BallsStyle::White()).BorderBackgroundColor(
                    BallsPalette::Linear(BallsPalette::PanelLight))
            ]
            + SOverlay::Slot().HAlign(HAlign_Left)
            [
                SNew(SBox)
                .WidthOverride(ProgressWidth * FMath::Clamp(Progress, 0.0f, 1.0f))
                [SNew(SBorder).BorderImage(BallsStyle::White()).BorderBackgroundColor(
                    BallsPalette::Linear(BallsPalette::Accent))]
            ]
        ];
}

}

void SBallsBenchmark::Construct(const FArguments& InArgs) {
    App = InArgs._App;
    Rows = SNew(SVerticalBox);
    Footer = SNew(STextBlock).Font(BallsStyle::Font(RowTextSize, true)).ColorAndOpacity(
        BallsPalette::Linear(BallsPalette::Accent));

    ChildSlot.VAlign(VAlign_Center).HAlign(HAlign_Center)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Setting up for this machine")))
            .Font(BallsStyle::Font(TitleSize, true))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::Accent))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 36.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Measuring each physics backend once. This only happens on first launch.")))
            .Font(BallsStyle::Font(SubtitleSize))
            .ColorAndOpacity(BallsPalette::Linear(BallsPalette::TextMuted))
        ]
        + SVerticalBox::Slot().AutoHeight()[Rows.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)[Footer.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 24.0f, 0.0f, 0.0f)
        [
            SNew(SBox).WidthOverride(ButtonWidth).HeightOverride(ButtonHeight)
            [
                BallsWidgets::Button(
                    TAttribute<FText>::CreateLambda([this] {
                        return FText::FromString(App && App->IsBenchmarkSettled() ? TEXT("Continue") : TEXT("Skip"));
                    }),
                    [this] {
                        if (App->IsBenchmarkSettled()) {
                            App->LeaveBenchmark();
                        } else {
                            App->SkipBenchmark();
                        }
                    },
                    20.0f)
            ]
        ]
    ];

    RegisterActiveTimer(PollSeconds, FWidgetActiveTimerDelegate::CreateSP(this, &SBallsBenchmark::Poll));
    ShownSignature = Signature();
    Refresh();
}

FString SBallsBenchmark::Signature() const {
    FString Result;
    if (!App) {
        return Result;
    }
    for (const CandidateResult& Entry : App->GetBenchmark().Results()) {
        Result += FString::Printf(TEXT("%d:%.0f:%.2f;"), static_cast<int32>(Entry.status), Entry.ballStepsPerSecond,
                                  Entry.progress);
    }
    Result += App->IsBenchmarkSettled() ? TEXT("settled") : TEXT("measuring");
    return Result;
}

void SBallsBenchmark::Refresh() {
    if (!Rows.IsValid() || !App) {
        return;
    }
    Rows->ClearChildren();
    const std::vector<CandidateResult> Results = App->GetBenchmark().Results();
    const std::optional<size_t> Best = fastestCandidate(Results);
    const bool bSettled = App->IsBenchmarkSettled();
    for (size_t Index = 0; Index < Results.size(); ++Index) {
        const CandidateResult& Entry = Results[Index];
        const bool bWinner = bSettled && Best && *Best == Index;
        Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
        [
            SNew(SBox).WidthOverride(ListWidth).HeightOverride(RowHeight)
            [
                SNew(SBorder)
                .BorderImage(BallsStyle::White())
                .BorderBackgroundColor(bWinner ? BallsPalette::Linear(BallsPalette::PanelLight)
                                               : BallsPalette::Linear(BallsPalette::Panel))
                .Padding(FMargin(RowPadding, 0.0f))
                .VAlign(VAlign_Center)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(BallsText::FromUtf8(Entry.candidate.label)))
                        .Font(BallsStyle::Font(RowTextSize, true))
                        .ColorAndOpacity(bWinner ? BallsPalette::Linear(BallsPalette::Accent)
                                                 : BallsPalette::Linear(BallsPalette::TextStrong))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        Entry.status == CandidateStatus::Running
                            ? ProgressBar(static_cast<float>(Entry.progress))
                            : SNew(STextBlock)
                                  .Text(FText::FromString(ResultText(Entry)))
                                  .Font(BallsStyle::Font(RowTextSize))
                                  .ColorAndOpacity(ResultColor(Entry))
                    ]
                ]
            ]
        ];
    }

    const FString Winner = App->GetBenchmarkWinnerLabel();
    Footer->SetText(FText::FromString(bSettled && !Winner.IsEmpty()
                                          ? FString::Printf(TEXT("Default backend: %s"), *Winner)
                                          : FString()));
}

EActiveTimerReturnType SBallsBenchmark::Poll(double CurrentTime, float DeltaTime) {
    const FString Current = Signature();
    if (Current != ShownSignature) {
        ShownSignature = Current;
        Refresh();
    }
    return EActiveTimerReturnType::Continue;
}
