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

}

TEST_CASE("ball limits read the SDL and Unreal line format") {
    const MeasuredBallLimits limits = parseBallLimits("AVX2 60 1250000\nVulkan 30 900000\r\nnot a line\n");
    EXPECT(limits.size() == 2);
    EXPECT(limits.at(BackendKind::Avx2).balls == 1'250'000);
    EXPECT(limits.at(BackendKind::Avx2).targetFps == 60.0);
    EXPECT(limits.at(BackendKind::Vulkan).targetFps == 30.0);
}

TEST_CASE("ball limits read the limit lines of Unity's old profile file") {
    const MeasuredBallLimits limits =
        parseBallLimits("cpu\tSome CPU\ncores\t4\nscore\tAVX2\t390000000\nlimit\tAVX2\t800000\t60.0\nlimit\tCUDA\t5\tbad\n");
    EXPECT(limits.size() == 1);
    EXPECT(limits.at(BackendKind::Avx2).balls == 800'000);
    EXPECT(limits.at(BackendKind::Avx2).targetFps == 60.0);
}

TEST_CASE("ball limits survive a serialize and parse round trip") {
    const MeasuredBallLimits original{{BackendKind::Scalar, {60.0, 1000}}, {BackendKind::CudaAvx2, {120.0, 7'000'000'000ull}}};
    const MeasuredBallLimits parsed = parseBallLimits(serializeBallLimits(original));
    EXPECT(parsed.size() == original.size());
    EXPECT(parsed.at(BackendKind::CudaAvx2).balls == 7'000'000'000ull);
    EXPECT(parsed.at(BackendKind::Scalar).targetFps == 60.0);
}

TEST_CASE("a machine profile keeps its limits in its folder") {
    const ScratchFolder folder("balls_profile_test");
    {
        MachineProfile profile(folder.path);
        profile.load();
        EXPECT(!profile.ballLimit(BackendKind::Avx2));
        profile.rememberBallLimit(BackendKind::Avx2, 60.0, 4321);
    }
    MachineProfile reloaded(folder.path);
    reloaded.load();
    EXPECT(reloaded.ballLimit(BackendKind::Avx2) && reloaded.ballLimit(BackendKind::Avx2)->balls == 4321);
}

TEST_CASE("a machine profile adopts Unity's old file until it saves its own") {
    const ScratchFolder folder("balls_profile_legacy_test");
    std::ofstream(folder.path / "machine-profile.txt") << "limit\tVulkan\t2500\t60.0\n";
    MachineProfile profile(folder.path);
    profile.load();
    EXPECT(profile.ballLimit(BackendKind::Vulkan) && profile.ballLimit(BackendKind::Vulkan)->balls == 2500);
    profile.rememberBallLimit(BackendKind::Scalar, 60.0, 10);
    MachineProfile reloaded(folder.path);
    reloaded.load();
    EXPECT(reloaded.ballLimit(BackendKind::Vulkan).has_value());
    EXPECT(reloaded.ballLimit(BackendKind::Scalar).has_value());
}
