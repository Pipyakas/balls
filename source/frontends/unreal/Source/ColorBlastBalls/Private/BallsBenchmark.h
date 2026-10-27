#pragma once

#include "BallsCoreIncludes.h"
#include "CoreMinimal.h"

#include <filesystem>
#include <memory>
#include <optional>

// "First launch: machine benchmark". Measures every backend once on this machine on a worker thread, keeps the
// winner as the default backend, and re-measures when the hardware fingerprint (CPU, GPUs, drivers, game version)
// changes. The record lives next to the settings file so a machine profile stays one folder.
class FBallsMachineBenchmark {
public:
    // The stored record, or nothing when this machine has never measured.
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
    const std::optional<BenchmarkRecord>& GetRecord() const { return Record; }
    const HardwareFingerprint& GetFingerprint() const { return Fingerprint; }

private:
    static std::filesystem::path RecordFile();

    std::unique_ptr<BackendBenchmark> Benchmark;
    std::optional<BenchmarkRecord> Record;
    HardwareFingerprint Fingerprint;
};
