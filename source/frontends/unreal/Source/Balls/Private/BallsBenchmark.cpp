#include "BallsBenchmark.h"

#include "BallsText.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"

namespace {

std::string GameVersion() {
    return std::string(TCHAR_TO_UTF8(*FString(FApp::GetBuildVersion())));
}

}

void FBallsMachineBenchmark::LoadRecord() { Fingerprint = currentHardwareFingerprint(GameVersion()); }

bool FBallsMachineBenchmark::NeedsRun() const { return benchmarkNeeded(Profile.benchmark(), Fingerprint); }

bool FBallsMachineBenchmark::Start(const std::filesystem::path& LevelDirectory, FString& OutError) {
    const std::vector<LevelFiles> Levels = findLevels(LevelDirectory);
    if (Levels.empty()) {
        OutError = TEXT("no Performance level to measure on");
        return false;
    }
    auto Level = loadLevel(Levels.front());
    if (!Level) {
        OutError = BallsText::FromUtf8(Level.error());
        return false;
    }
    BenchmarkWorkload Workload;
    Workload.level = std::move(Level->bricks);
    Benchmark = std::make_unique<BackendBenchmark>(std::move(Workload), benchmarkCandidates());
    Benchmark->start();
    return true;
}

void FBallsMachineBenchmark::Skip() {
    if (Benchmark) {
        Benchmark->cancel();
    }
}

bool FBallsMachineBenchmark::IsFinished() const { return Benchmark && Benchmark->finished(); }

std::vector<CandidateResult> FBallsMachineBenchmark::Results() const {
    if (Benchmark) {
        return Benchmark->results();
    }
    return Profile.benchmark() ? Profile.benchmark()->results : std::vector<CandidateResult>{};
}

std::optional<BenchmarkCandidate> FBallsMachineBenchmark::PickWinner() const {
    const std::vector<CandidateResult> Current = Results();
    if (Current.empty()) {
        return std::nullopt;
    }
    return chooseDefaultBackend(Current, [](BackendKind Kind) { return isBackendAvailable(Kind); });
}

void FBallsMachineBenchmark::Save(bool bComplete, const std::optional<BenchmarkCandidate>& Winner) {
    BenchmarkRecord Next;
    Next.fingerprint = Fingerprint;
    Next.results = Results();
    Next.winner = Winner;
    Next.complete = bComplete;
    Profile.rememberBenchmark(std::move(Next));
    Benchmark.reset();
}
