#include "sim/BackendBenchmark.h"

#include "common/BallColor.h"
#include "sim/Simulation.h"

#include <algorithm>
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

std::string trimmed(std::string text) {
    const auto first = text.find_first_not_of(' ');
    const auto last = text.find_last_not_of(' ');
    return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
}

std::string cpuBrand() {
#if defined(_WIN32) || defined(__x86_64__) || defined(__i386__)
    constexpr unsigned firstBrandLeaf = 0x80000002u;
    constexpr unsigned brandLeafCount = 3;
    char brand[brandLeafCount * 16 + 1] = {};
    for (unsigned leaf = 0; leaf < brandLeafCount; ++leaf) {
        unsigned registers[4] = {};
#if defined(_WIN32)
        __cpuid(reinterpret_cast<int*>(registers), static_cast<int>(firstBrandLeaf + leaf));
#else
        __get_cpuid(firstBrandLeaf + leaf, &registers[0], &registers[1], &registers[2], &registers[3]);
#endif
        std::memcpy(brand + leaf * 16, registers, 16);
    }
    return trimmed(brand);
#else
    return "unknown CPU";
#endif
}

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

std::optional<BackendKind> kindNamed(std::string_view name) {
    for (const BackendKind kind : allBackendKinds) {
        if (backendKindName(kind) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

std::string candidateLabel(BackendKind kind, std::string_view computeGpu) {
    return computeGpu.empty() ? std::string(backendKindName(kind))
                              : std::format("{} ({})", backendKindName(kind), computeGpu);
}

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
    return {cpuBrand(), std::max(1u, std::thread::hardware_concurrency()), gpuIdentities(), std::string(gameVersion)};
}

std::vector<BenchmarkCandidate> benchmarkCandidates() {
    std::vector<BenchmarkCandidate> candidates;
    for (const BackendKind kind : allBackendKinds) {
        candidates.push_back({kind, {}, candidateLabel(kind, {})});
    }
    return candidates;
}

bool benchmarkNeeded(const std::optional<BenchmarkRecord>& stored, const HardwareFingerprint& current) {
    return !stored || !stored->complete || stored->fingerprint != current;
}

std::optional<size_t> fastestCandidate(std::span<const CandidateResult> results) {
    std::optional<size_t> best;
    for (size_t index = 0; index < results.size(); ++index) {
        const CandidateResult& result = results[index];
        if (result.status == CandidateStatus::Done &&
            (!best || result.ballStepsPerSecond > results[*best].ballStepsPerSecond)) {
            best = index;
        }
    }
    return best;
}

BenchmarkCandidate chooseDefaultBackend(std::span<const CandidateResult> results,
                                        const std::function<bool(BackendKind)>& isAvailable) {
    if (const auto best = fastestCandidate(results)) {
        return results[*best].candidate;
    }
    const BackendKind fallback = isAvailable(BackendKind::Avx2) ? BackendKind::Avx2 : BackendKind::Scalar;
    return {fallback, {}, candidateLabel(fallback, {})};
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
        text += std::format("winner{0}{1}{0}{2}\n", fieldSeparator, backendKindName(record.winner->kind),
                            withoutSeparators(record.winner->computeGpu));
    }
    for (const CandidateResult& result : record.results) {
        text += std::format("result{0}{1}{0}{2}{0}{3}{0}{4:.0f}{0}{5}\n", fieldSeparator,
                            backendKindName(result.candidate.kind), withoutSeparators(result.candidate.computeGpu),
                            statusName(result.status), result.ballStepsPerSecond, withoutSeparators(result.failure));
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
            if (const auto kind = kindNamed(value)) {
                const std::string gpu = fields.size() > 2 ? fields[2] : std::string{};
                record.winner = BenchmarkCandidate{*kind, gpu, candidateLabel(*kind, gpu)};
            }
        } else if (key == "result" && fields.size() >= 5) {
            const auto kind = kindNamed(fields[1]);
            const auto status = parseStatus(fields[3]);
            if (kind && status) {
                CandidateResult result;
                result.candidate = {*kind, fields[2], candidateLabel(*kind, fields[2])};
                result.status = *status;
                result.ballStepsPerSecond = std::strtod(fields[4].c_str(), nullptr);
                result.failure = fields.size() > 5 ? fields[5] : std::string{};
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
    for (const BallColor color : allBallColors) {
        simulation.scatterBalls(color, static_cast<BallCount>(workload.balls / ballColorCount));
    }
    const auto timedOut = [&] { return secondsSince(start) > workload.candidateLimitSeconds; };
    const auto faulted = [&] { return std::string(simulation.backend().faultMessage()); };
    for (int tick = 0; tick < workload.warmupTicks; ++tick) {
        if (cancelled) {
            return;
        }
        simulation.tick(1);
        if (const std::string fault = faulted(); !fault.empty()) {
            fail(fault);
            return;
        }
        if (timedOut()) {
            fail(std::format("timed out after {:.0f} s", workload.candidateLimitSeconds));
            return;
        }
        update(index, [&](CandidateResult& result) {
            result.progress = warmupShare * static_cast<double>(tick + 1) / static_cast<double>(workload.warmupTicks);
        });
    }

    const double balls = static_cast<double>(simulation.backend().ballCount());
    const Clock::time_point measureStart = Clock::now();
    long long ticks = 0;
    while (secondsSince(measureStart) < workload.measuredSeconds) {
        if (cancelled) {
            return;
        }
        simulation.tick(1);
        ++ticks;
        if (const std::string fault = faulted(); !fault.empty()) {
            fail(fault);
            return;
        }
        if (timedOut()) {
            fail(std::format("timed out after {:.0f} s", workload.candidateLimitSeconds));
            return;
        }
        update(index, [&](CandidateResult& result) {
            result.progress = warmupShare + (1.0 - warmupShare) * std::min(1.0, secondsSince(measureStart) / workload.measuredSeconds);
        });
    }
    const double seconds = secondsSince(measureStart);
    update(index, [&](CandidateResult& result) {
        result.status = CandidateStatus::Done;
        result.progress = 1.0;
        result.ballStepsPerSecond = balls * static_cast<double>(ticks) / seconds;
    });
}
