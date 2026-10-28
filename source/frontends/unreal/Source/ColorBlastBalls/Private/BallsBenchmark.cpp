#include "BallsBenchmark.h"

#include "BallsText.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

#include <fstream>
#include <sstream>

namespace {

// The Settings page and the result toast both show this, so it lives here rather than in the screen.
std::string GameVersion() {
    return std::string(TCHAR_TO_UTF8(*FString(FApp::GetBuildVersion())));
}

}

std::filesystem::path FBallsMachineBenchmark::RecordFile() {
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Balls/benchmark.txt");
    return std::filesystem::path(TCHAR_TO_UTF8(*Path));
}

void FBallsMachineBenchmark::LoadRecord() {
    Fingerprint = currentHardwareFingerprint(GameVersion());
    Record.reset();
    std::ifstream File(RecordFile());
    if (!File) {
        return;
    }
    std::stringstream Buffer;
    Buffer << File.rdbuf();
    Record = parseBenchmarkRecord(Buffer.str());
}

bool FBallsMachineBenchmark::NeedsRun() const { return benchmarkNeeded(Record, Fingerprint); }

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
    return Record ? Record->results : std::vector<CandidateResult>{};
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
    const std::filesystem::path Path = RecordFile();
    std::error_code Error;
    std::filesystem::create_directories(Path.parent_path(), Error);
    std::ofstream File(Path, std::ios::trunc);
    if (File) {
        File << serializeBenchmarkRecord(Next);
    }
    if (bComplete) {
        Record = std::move(Next);
    }
    Benchmark.reset();
}
