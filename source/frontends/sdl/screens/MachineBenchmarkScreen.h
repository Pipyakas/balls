#pragma once

#include "app/Screen.h"
#include "sim/BackendBenchmark.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class App;

// The /benchmark control answer, shared by the live screen and the stored record.
std::string benchmarkJson(std::string_view state, std::span<const CandidateResult> results,
                          const std::optional<BenchmarkCandidate>& winner);

// First-launch benchmark: measures every backend on a worker thread, makes the
// fastest the default and opens the Main menu.
class MachineBenchmarkScreen final : public Screen {
public:
    MachineBenchmarkScreen(App& app, HardwareFingerprint fingerprint);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "benchmark"; }
    std::string stateJson() const override;
    std::optional<ControlResponse> handleControl(const ControlRequest& request) override;

private:
    void finish(bool complete);
    void drawRow(const CandidateResult& result, float top, bool isWinner) const;

    App& app;
    HardwareFingerprint fingerprint;
    std::unique_ptr<BackendBenchmark> benchmark;
    std::string loadError;
    std::optional<BenchmarkCandidate> winner;
    std::vector<CandidateResult> finalResults;
    float secondsSinceDone = 0.0f;
};
