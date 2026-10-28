#include "sim/BackendBenchmark.h"

#include "common/BallColor.h"
#include "sim/BackendSelection.h"
#include "sim/BackendWorker.h"
#include "sim/DeviceInventory.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr double checkTolerance = 0.05;
constexpr long long checkSlack = 10;
constexpr double warmupShare = 0.3;
constexpr char fieldSeparator = '\t';

double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

std::vector<std::string> gpuIdentities() {
    std::vector<std::string> gpus;
#if defined(_WIN32)
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi) {
        return gpus;
    }
    const auto createFactory = reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    IDXGIFactory1* factory = nullptr;
    if (createFactory && SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
            DXGI_ADAPTER_DESC1 description{};
            LARGE_INTEGER driverVersion{};
            if (SUCCEEDED(adapter->GetDesc1(&description))) {
                adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
                char name[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr);
                const auto version = static_cast<unsigned long long>(driverVersion.QuadPart);
                gpus.push_back(std::format("{} | {}.{}.{}.{}", name, version >> 48, (version >> 32) & 0xFFFF,
                                           (version >> 16) & 0xFFFF, version & 0xFFFF));
            }
            adapter->Release();
        }
        factory->Release();
    }
    FreeLibrary(dxgi);
#endif
    std::ranges::sort(gpus);
    gpus.erase(std::ranges::unique(gpus).begin(), gpus.end());
    return gpus;
}

std::string_view statusName(CandidateStatus status) {
    switch (status) {
    case CandidateStatus::Pending:
        return "pending";
    case CandidateStatus::Running:
        return "running";
    case CandidateStatus::Done:
        return "done";
    case CandidateStatus::Failed:
        return "failed";
    case CandidateStatus::Unavailable:
        return "unavailable";
    }
    return "pending";
}

std::optional<CandidateStatus> parseStatus(std::string_view name) {
    for (const CandidateStatus status : {CandidateStatus::Pending, CandidateStatus::Running, CandidateStatus::Done,
                                         CandidateStatus::Failed, CandidateStatus::Unavailable}) {
        if (statusName(status) == name) {
            return status;
        }
    }
    return std::nullopt;
}

std::string candidateLabel(BackendKind kind, std::string_view computeGpu) {
    return computeGpu.empty() ? std::string(backendKindName(kind))
                              : std::format("{} ({})", backendKindName(kind), computeGpu);
}

std::string picksCandidateLabel(std::span<const BackendPick> picks) {
    return picks.size() == 1 ? backendPicksLabel(picks) : "Hybrid: " + backendPicksLabel(picks);
}

// A record's backend column: a kind name, or picks for candidates that ran on chosen devices.
std::optional<BenchmarkCandidate> candidateNamed(std::string_view name, const std::string& computeGpu) {
    const std::optional<StoredBackend> stored = parseStoredBackend(name);
    if (!stored) {
        return std::nullopt;
    }
    if (!stored->picks.empty()) {
        return BenchmarkCandidate{stored->kind, computeGpu, picksCandidateLabel(stored->picks),
                                  formatBackendPicks(stored->picks)};
    }
    return BenchmarkCandidate{stored->kind, computeGpu, candidateLabel(stored->kind, computeGpu), {}};
}

std::string candidateColumn(const BenchmarkCandidate& candidate) {
    return candidate.picks.empty() ? std::string(backendKindName(candidate.kind)) : candidate.picks;
}

// Runs a candidate with its picks as the process-wide ones, and puts the player's picks back afterwards.
class CandidatePicks {
public:
    explicit CandidatePicks(const BenchmarkCandidate& candidate) : saved(backendPicks()) {
        std::vector<BackendPick> picks;
        if (!candidate.picks.empty()) {
            picks = parseBackendPicks(candidate.picks).value_or(std::vector<BackendPick>{});
        }
        setBackendPicks(std::move(picks));
    }
    ~CandidatePicks() { setBackendPicks(saved); }
    CandidatePicks(const CandidatePicks&) = delete;
    CandidatePicks& operator=(const CandidatePicks&) = delete;

private:
    std::vector<BackendPick> saved;
};

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, fieldSeparator)) {
        fields.push_back(field);
    }
    return fields;
}

std::string withoutSeparators(std::string text) {
    std::ranges::replace(text, fieldSeparator, ' ');
    std::ranges::replace(text, '\n', ' ');
    return text;
}

}

HardwareFingerprint currentHardwareFingerprint(std::string_view gameVersion) {
    return {cpuBrandName(), std::max(1u, std::thread::hardware_concurrency()), gpuIdentities(), std::string(gameVersion)};
}

std::vector<BenchmarkCandidate> benchmarkCandidates() {
    constexpr size_t maximumComposedDevices = 6;
    std::vector<BenchmarkCandidate> candidates;
    for (const BackendKind kind : selectableBackends()) {
        if (kind != BackendKind::Hybrid) {
            candidates.push_back({kind, {}, candidateLabel(kind, {}), {}});
        }
    }
    const std::vector<PhysicalDevice>& devices = physicalDevices();
    std::vector<BackendPick> fastest;
    for (const PhysicalDevice& device : devices) {
        bool first = true;
        for (const BackendKind kind : pickableKinds(device)) {
            const DeviceBackendOption* option = device.option(kind);
            if (!option || !deviceBackendStatus(kind, option->pluginDevice).available) {
                continue;
            }
            if (first) {
                fastest.push_back({kind, device.id});
                first = false;
            }
            const PhysicalDevice* defaultDevice = defaultDeviceFor(kind);
            if (defaultDevice && defaultDevice->id != device.id) {
                const std::vector<BackendPick> single{{kind, device.id}};
                candidates.push_back({kind, {}, picksCandidateLabel(single), formatBackendPicks(single)});
            }
        }
    }
    if (fastest.size() >= 2 && fastest.size() <= maximumComposedDevices) {
        for (uint32_t mask = 1; mask < (1u << fastest.size()); ++mask) {
            if (std::popcount(mask) < 2) {
                continue;
            }
            std::vector<BackendPick> picks;
            for (size_t index = 0; index < fastest.size(); ++index) {
                if ((mask >> index) & 1u) {
                    picks.push_back(fastest[index]);
                }
            }
            candidates.push_back(
                {BackendKind::Hybrid, {}, picksCandidateLabel(picks), formatBackendPicks(picks)});
        }
    }
    return candidates;
}

bool benchmarkNeeded(const std::optional<BenchmarkRecord>& stored, const HardwareFingerprint& current) {
    return !stored || !stored->complete || stored->fingerprint != current;
}

std::optional<size_t> bestCandidate(std::span<const CandidateResult> results) {
    std::optional<size_t> best;
    const auto better = [](const CandidateResult& challenger, const CandidateResult& holder) {
        if (challenger.estimatedBalls != holder.estimatedBalls) {
            return challenger.estimatedBalls > holder.estimatedBalls;
        }
        return challenger.ballStepsPerSecond > holder.ballStepsPerSecond;
    };
    for (size_t index = 0; index < results.size(); ++index) {
        const CandidateResult& result = results[index];
        if (result.status == CandidateStatus::Done && (!best || better(result, results[*best]))) {
            best = index;
        }
    }
    return best;
}

std::string shortBallCount(double balls) {
    constexpr double unbounded = 1e15;
    constexpr double billion = 1e9;
    if (balls >= unbounded) {
        return "no limit";
    }
    constexpr double million = 1e6;
    constexpr double thousand = 1e3;
    if (balls >= billion) {
        return std::format("{:.1f} B", balls / billion);
    }
    if (balls >= million) {
        return std::format("{:.0f} M", balls / million);
    }
    if (balls >= thousand) {
        return std::format("{:.0f} k", balls / thousand);
    }
    return std::format("{:.0f}", balls);
}

BenchmarkCandidate chooseDefaultBackend(std::span<const CandidateResult> results,
                                        const std::function<bool(BackendKind)>& isAvailable) {
    if (const auto best = bestCandidate(results)) {
        return results[*best].candidate;
    }
    const BackendKind fallback = isAvailable(BackendKind::Avx2) ? BackendKind::Avx2 : BackendKind::Scalar;
    return {fallback, {}, candidateLabel(fallback, {})};
}

double estimateBallsAtTarget(double smallBalls, double smallFrameSeconds, double largeBalls, double largeFrameSeconds,
                             double frameBudgetSeconds, double capacity) {
    // A stepped backend's frame grows almost in proportion to the count (2.6x to 3.3x for 4x the balls); one whose
    // balls fly freely only draws sampled bidders and stays within noise of flat. Fitting a line through two
    // nearly equal noisy times would extrapolate noise, so anything growing less than this is capacity-limited.
    constexpr double flatGrowth = 1.5;
    const double addedBalls = largeBalls - smallBalls;
    if (addedBalls <= 0.0 || largeFrameSeconds <= smallFrameSeconds * flatGrowth) {
        return largeFrameSeconds <= frameBudgetSeconds ? capacity : 0.0;
    }
    const double perBall = (largeFrameSeconds - smallFrameSeconds) / addedBalls;
    const double fixed = std::max(0.0, largeFrameSeconds - perBall * largeBalls);
    const double reachable = fixed < frameBudgetSeconds ? (frameBudgetSeconds - fixed) / perBall : 0.0;
    return std::min(reachable, capacity);
}

std::string serializeBenchmarkRecord(const BenchmarkRecord& record) {
    std::string text;
    text += std::format("cpu{}{}\n", fieldSeparator, withoutSeparators(record.fingerprint.cpuModel));
    text += std::format("cores{}{}\n", fieldSeparator, record.fingerprint.logicalCores);
    for (const std::string& gpu : record.fingerprint.gpus) {
        text += std::format("gpu{}{}\n", fieldSeparator, withoutSeparators(gpu));
    }
    text += std::format("version{}{}\n", fieldSeparator, withoutSeparators(record.fingerprint.gameVersion));
    text += std::format("complete{}{}\n", fieldSeparator, record.complete ? 1 : 0);
    if (record.winner) {
        text += std::format("winner{0}{1}{0}{2}\n", fieldSeparator, candidateColumn(*record.winner),
                            withoutSeparators(record.winner->computeGpu));
    }
    for (const CandidateResult& result : record.results) {
        text += std::format("result{0}{1}{0}{2}{0}{3}{0}{4:.0f}{0}{5}{0}{6:.0f}{0}{7:.0f}\n", fieldSeparator,
                            candidateColumn(result.candidate), withoutSeparators(result.candidate.computeGpu),
                            statusName(result.status), result.ballStepsPerSecond, withoutSeparators(result.failure),
                            result.estimatedBalls, result.ballCapacity);
    }
    return text;
}

std::optional<BenchmarkRecord> parseBenchmarkRecord(std::string_view text) {
    BenchmarkRecord record;
    bool sawFingerprint = false;
    std::stringstream stream{std::string(text)};
    std::string line;
    while (std::getline(stream, line)) {
        const std::vector<std::string> fields = splitFields(line);
        if (fields.empty()) {
            continue;
        }
        const std::string& key = fields[0];
        const std::string value = fields.size() > 1 ? fields[1] : std::string{};
        if (key == "cpu") {
            record.fingerprint.cpuModel = value;
            sawFingerprint = true;
        } else if (key == "cores") {
            record.fingerprint.logicalCores = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (key == "gpu") {
            record.fingerprint.gpus.push_back(value);
        } else if (key == "version") {
            record.fingerprint.gameVersion = value;
        } else if (key == "complete") {
            record.complete = value == "1";
        } else if (key == "winner") {
            record.winner = candidateNamed(value, fields.size() > 2 ? fields[2] : std::string{});
        } else if (key == "result" && fields.size() >= 5) {
            const auto candidate = candidateNamed(fields[1], fields[2]);
            const auto status = parseStatus(fields[3]);
            if (candidate && status) {
                CandidateResult result;
                result.candidate = *candidate;
                result.status = *status;
                result.ballStepsPerSecond = std::strtod(fields[4].c_str(), nullptr);
                result.failure = fields.size() > 5 ? fields[5] : std::string{};
                result.estimatedBalls = fields.size() > 6 ? std::strtod(fields[6].c_str(), nullptr) : 0.0;
                result.ballCapacity = fields.size() > 7 ? std::strtod(fields[7].c_str(), nullptr) : 0.0;
                record.results.push_back(std::move(result));
            }
        }
    }
    if (!sawFingerprint) {
        return std::nullopt;
    }
    return record;
}

BackendBenchmark::BackendBenchmark(BenchmarkWorkload benchmarkWorkload, std::vector<BenchmarkCandidate> candidates)
    : workload(std::move(benchmarkWorkload)) {
    for (BenchmarkCandidate& candidate : candidates) {
        candidateResults.push_back({std::move(candidate)});
    }
}

BackendBenchmark::~BackendBenchmark() {
    cancel();
}

void BackendBenchmark::start() {
    worker = std::jthread([this] { run(); });
}

void BackendBenchmark::cancel() { cancelled = true; }

std::vector<CandidateResult> BackendBenchmark::results() const {
    std::lock_guard lock(resultsMutex);
    return candidateResults;
}

void BackendBenchmark::update(size_t index, const std::function<void(CandidateResult&)>& change) {
    std::lock_guard lock(resultsMutex);
    change(candidateResults[index]);
}

namespace {

struct CheckOutcome {
    std::optional<long long> hits;
    std::string failure;
};

// The same seeded scatter on every backend, so hit counts only differ by floating-point rounding.
CheckOutcome countCheckHits(const BenchmarkWorkload& workload, BackendKind kind) {
    BackendHandle backend = createBackend(kind);
    if (!backend) {
        return {std::nullopt, "could not be created"};
    }
    Simulation simulation(std::move(backend));
    simulation.load(workload.level);
    for (const BallColor color : allBallColors) {
        simulation.scatterBalls(color, static_cast<BallCount>(workload.checkBalls / ballColorCount));
    }
    for (int tick = 0; tick < workload.checkTicks; ++tick) {
        simulation.tick(1);
        if (const std::string_view fault = simulation.backend().faultMessage(); !fault.empty()) {
            return {std::nullopt, std::string(fault)};
        }
    }
    return {static_cast<long long>(simulation.stats().totalHits), {}};
}

}

void BackendBenchmark::run() {
    std::optional<long long> referenceHits;
    if (isBackendAvailable(BackendKind::Scalar)) {
        referenceHits = countCheckHits(workload, BackendKind::Scalar).hits;
    }
    for (size_t index = 0; index < candidateResults.size() && !cancelled; ++index) {
        runCandidate(index, referenceHits);
    }
    done = true;
}

void BackendBenchmark::runCandidate(size_t index, std::optional<long long> referenceHits) {
    const BackendKind kind = candidateResults[index].candidate.kind;
    const CandidatePicks picks(candidateResults[index].candidate);
    if (!isBackendAvailable(kind)) {
        update(index, [&](CandidateResult& result) {
            result.status = CandidateStatus::Unavailable;
            result.failure = backendUnavailableReason(kind);
        });
        return;
    }
    const Clock::time_point start = Clock::now();
    const auto fail = [&](std::string reason) {
        update(index, [&](CandidateResult& result) {
            result.status = CandidateStatus::Failed;
            result.failure = std::move(reason);
        });
    };
    update(index, [](CandidateResult& result) { result.status = CandidateStatus::Running; });

    const CheckOutcome check = countCheckHits(workload, kind);
    if (!check.hits) {
        fail(check.failure);
        return;
    }
    if (referenceHits) {
        const long long allowed = static_cast<long long>(static_cast<double>(*referenceHits) * checkTolerance) + checkSlack;
        if (std::llabs(*check.hits - *referenceHits) > allowed) {
            fail(std::format("{} hits on the check run, Scalar had {}", *check.hits, *referenceHits));
            return;
        }
    }

    BackendHandle backend = createBackend(kind);
    if (!backend) {
        fail("could not be created");
        return;
    }
    Simulation simulation(std::move(backend));
    simulation.load(workload.level);
    const float slotSize = simulation.tuning().ballRadius * visibilitySlotsPerRadius;
    simulation.backend().setVisibilityGrid(visibilitySlots(static_cast<float>(simulation.board().columns()), slotSize),
                                           visibilitySlots(static_cast<float>(simulation.board().rows()), slotSize));
    const auto scatterUpTo = [&](size_t balls) {
        const size_t live = static_cast<size_t>(simulation.liveBallCount());
        const size_t perColor = balls > live ? (balls - live) / ballColorCount : 0;
        for (const BallColor color : allBallColors) {
            simulation.scatterBalls(color, static_cast<BallCount>(perColor));
        }
    };
    const auto timedOut = [&] { return secondsSince(start) > workload.candidateLimitSeconds; };
    // Runs game frames (ticksPerFrame ticks each) until `frames` are done or `seconds` pass, whichever the caller
    // asks for; false when the candidate faulted, timed out or was cancelled.
    const auto runFrames = [&](int frames, double seconds, double progressFrom, double progressTo,
                               long long& framesRun) {
        const Clock::time_point begin = Clock::now();
        framesRun = 0;
        while (frames > 0 ? framesRun < frames : secondsSince(begin) < seconds) {
            if (cancelled) {
                return false;
            }
            simulation.tick(workload.ticksPerFrame);
            ++framesRun;
            if (const std::string_view fault = simulation.backend().faultMessage(); !fault.empty()) {
                fail(std::string(fault));
                return false;
            }
            if (timedOut()) {
                fail(std::format("timed out after {:.0f} s", workload.candidateLimitSeconds));
                return false;
            }
            const double done = frames > 0 ? static_cast<double>(framesRun) / frames : secondsSince(begin) / seconds;
            update(index, [&](CandidateResult& result) {
                result.progress = progressFrom + (progressTo - progressFrom) * std::min(1.0, done);
            });
        }
        return true;
    };
    const auto measureFrameSeconds = [&](double progressFrom, double progressTo, double& frameSeconds) {
        long long frames = 0;
        const Clock::time_point begin = Clock::now();
        if (!runFrames(0, workload.measuredSeconds, progressFrom, progressTo, frames)) {
            return false;
        }
        frameSeconds = secondsSince(begin) / static_cast<double>(std::max(frames, 1LL));
        return true;
    };

    scatterUpTo(workload.balls);
    long long frames = 0;
    const int warmupFrames = std::max(1, workload.warmupTicks / workload.ticksPerFrame);
    double smallFrameSeconds = 0.0;
    if (!runFrames(warmupFrames, 0.0, 0.0, warmupShare, frames) || !measureFrameSeconds(warmupShare, 0.6, smallFrameSeconds)) {
        return;
    }
    const double smallBalls = static_cast<double>(simulation.backend().ballCount());
    scatterUpTo(workload.largerBalls);
    double largeFrameSeconds = 0.0;
    if (!runFrames(workload.warmupFramesAfterGrowth, 0.0, 0.6, 0.7, frames) ||
        !measureFrameSeconds(0.7, 1.0, largeFrameSeconds)) {
        return;
    }
    const double largeBalls = static_cast<double>(simulation.backend().ballCount());
    const double capacity = static_cast<double>(simulation.backend().ballCapacityLimit());
    const double estimate = estimateBallsAtTarget(smallBalls, smallFrameSeconds, largeBalls, largeFrameSeconds,
                                                  workload.frameBudgetShare / workload.targetFps, capacity);
    update(index, [&](CandidateResult& result) {
        result.status = CandidateStatus::Done;
        result.progress = 1.0;
        result.ballStepsPerSecond = largeBalls * workload.ticksPerFrame / largeFrameSeconds;
        result.estimatedBalls = estimate;
        result.ballCapacity = capacity;
    });
}

std::string candidateResultText(const CandidateResult& result) {
    constexpr double ballStepsPerMillion = 1e6;
    switch (result.status) {
    case CandidateStatus::Done:
        return std::format("{} balls at 60 FPS   {:.0f} M ball-steps/s", shortBallCount(result.estimatedBalls),
                           result.ballStepsPerSecond / ballStepsPerMillion);
    case CandidateStatus::Failed:
        return "failed: " + result.failure;
    case CandidateStatus::Unavailable:
        return "not available";
    case CandidateStatus::Pending:
    case CandidateStatus::Running:
        break;
    }
    return {};
}
