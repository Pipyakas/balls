#include "TestFramework.h"

#include "game/MachineProfile.h"

#include <filesystem>
#include <fstream>

namespace {

// A fresh folder per test, removed again when the test ends.
struct ScratchFolder {
    std::filesystem::path path;

    explicit ScratchFolder(const char* name) : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~ScratchFolder() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

} // namespace

TEST_CASE("a machine profile starts empty when no benchmark record exists") {
    const ScratchFolder folder("balls_profile_empty_test");
    MachineProfile profile(folder.path);
    profile.load();
    EXPECT(!profile.benchmark().has_value());
}

TEST_CASE("a machine profile preserves benchmark records across save and load") {
    const ScratchFolder folder("balls_profile_benchmark_test");
    BenchmarkRecord record;
    record.fingerprint.cpuModel = "Test CPU";
    record.fingerprint.logicalCores = 8;
    record.complete = true;
    BenchmarkCandidate winner;
    winner.kind = BackendKind::Avx2;
    winner.label = "AVX2";
    record.winner = winner;
    CandidateResult candidateResult;
    candidateResult.candidate = winner;
    candidateResult.status = CandidateStatus::Done;
    candidateResult.ballStepsPerSecond = 500'000'000.0;
    candidateResult.estimatedBalls = 1'000'000.0;
    record.results.push_back(candidateResult);

    {
        MachineProfile profile(folder.path);
        profile.rememberBenchmark(record);
    }

    MachineProfile reloaded(folder.path);
    reloaded.load();
    EXPECT(reloaded.benchmark().has_value());
    EXPECT(reloaded.benchmark()->fingerprint.cpuModel == "Test CPU");
    EXPECT(reloaded.benchmark()->winner.has_value());
    EXPECT(reloaded.benchmark()->winner->kind == BackendKind::Avx2);
    EXPECT(reloaded.benchmark()->results.size() == 1);
    EXPECT(reloaded.benchmark()->results[0].ballStepsPerSecond == 500'000'000.0);
}

TEST_CASE("a machine profile ignores older files with remembered ball limits") {
    const ScratchFolder folder("balls_profile_legacy_test");
    // Write legacy limit formats from older versions
    std::ofstream(folder.path / "limits.txt") << "AVX2 60 1250000\nVulkan 30 900000\n";
    std::ofstream(folder.path / "machine-profile.txt") << "limit\tVulkan\t2500\t60.0\n";

    MachineProfile profile(folder.path);
    profile.load();
    // No benchmark record should be loaded from limits files
    EXPECT(!profile.benchmark().has_value());
}

TEST_CASE("targetMachineWinner handles missing and invalid files gracefully") {
    EXPECT(!targetMachineWinner("non_existent_target_benchmark.txt").has_value());
}
