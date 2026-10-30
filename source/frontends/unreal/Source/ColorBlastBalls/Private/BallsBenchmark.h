#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

#include <filesystem>
#include <memory>
#include <optional>

// "First launch: machine benchmark". Measures every backend once on this machine on a worker thread, keeps the
// winner as the default backend, and re-measures when the hardware fingerprint (CPU, GPUs, drivers, game version)
// changes. The record is kept by the core's MachineProfile, next to the settings file.
class FBallsMachineBenchmark {
public:
    explicit FBallsMachineBenchmark(MachineProfile& InProfile) : Profile(InProfile) {}

    // Reads this machine's fingerprint; the stored record is the profile's.
    void LoadRecord();
    // True when the record no longer describes this machine, or was skipped last time.
    bool NeedsRun() const;
    // Loads the first Performance level in LevelDirectory and starts the worker. False (with OutError) when it
    // cannot run.
    bool Start(const std::filesystem::path& LevelDirectory, FString& OutError);
    void Skip();
    bool IsRunning() const { return Benchmark != nullptr; }
    bool IsFinished() const;
    std::vector<CandidateResult> Results() const;
    // The winner after a completed run, or the best finished candidate so far, or AVX2/Scalar when nothing finished.
    std::optional<BenchmarkCandidate> PickWinner() const;
    // Stores the record; Complete is false when the player skipped, so the next launch measures again.
    void Save(bool bComplete, const std::optional<BenchmarkCandidate>& Winner);
    const std::optional<BenchmarkRecord>& GetRecord() const { return Profile.benchmark(); }
    const HardwareFingerprint& GetFingerprint() const { return Fingerprint; }

private:
    MachineProfile& Profile;
    std::unique_ptr<BackendBenchmark> Benchmark;
    HardwareFingerprint Fingerprint;
};
