#pragma once

#include "app/Screen.h"
#include "sim/BackendBenchmark.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class App;

class MachineBenchmarkScreen final : public Screen {
public:
    MachineBenchmarkScreen(App& app, HardwareFingerprint fingerprint);

    void update(float deltaTime) override;
    void draw() override;
    std::string_view name() const override { return "benchmark"; }
    std::string stateJson() const override { return controlBenchmarkJson(controlState()); }

    ControlBenchmarkState controlState() const;
    void skip();

private:
    void finish(bool complete);
    void drawRow(const CandidateResult& result, float top, bool isWinner) const;

    App& app;
    HardwareFingerprint fingerprint;
    std::unique_ptr<BackendBenchmark> benchmark;
    std::string loadError;
    std::optional<BenchmarkCandidate> winner;
    std::vector<CandidateResult> finalResults;
};
