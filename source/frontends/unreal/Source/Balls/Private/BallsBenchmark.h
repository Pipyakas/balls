#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

#include <filesystem>
#include <memory>
#include <optional>

class FBallsMachineBenchmark {
public:
    explicit FBallsMachineBenchmark(MachineProfile& InProfile) : Profile(InProfile) {}

    void LoadRecord();

    bool NeedsRun() const;

    bool Start(const std::filesystem::path& LevelDirectory, FString& OutError);
    void Skip();
    bool IsRunning() const { return Benchmark != nullptr; }
    bool IsFinished() const;
    std::vector<CandidateResult> Results() const;

    std::optional<BenchmarkCandidate> PickWinner() const;

    void Save(bool bComplete, const std::optional<BenchmarkCandidate>& Winner);
    const std::optional<BenchmarkRecord>& GetRecord() const { return Profile.benchmark(); }
    const HardwareFingerprint& GetFingerprint() const { return Fingerprint; }

private:
    MachineProfile& Profile;
    std::unique_ptr<BackendBenchmark> Benchmark;
    HardwareFingerprint Fingerprint;
};
