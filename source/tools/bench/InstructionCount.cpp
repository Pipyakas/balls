#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "sim/BackendFactory.h"
#include "sim/Simulation.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cupti.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <numbers>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr size_t activityBufferBytes = size_t{8} << 20;
constexpr size_t activityBufferAlignment = 8;
constexpr double nanosecondsPerMillisecond = 1.0e6;

struct CuptiApi {
    decltype(&cuptiSubscribe) subscribe = nullptr;
    decltype(&cuptiEnableDomain) enableDomain = nullptr;
    decltype(&cuptiActivityRegisterCallbacks) registerCallbacks = nullptr;
    decltype(&cuptiActivityEnable) enable = nullptr;
    decltype(&cuptiActivityFlushAll) flushAll = nullptr;
    decltype(&cuptiActivityGetNextRecord) nextRecord = nullptr;
    decltype(&cuptiGetResultString) resultString = nullptr;
};

struct PcCount {
    uint64_t warpExecutions = 0;
    uint64_t threadExecutions = 0;
    uint64_t activeThreadExecutions = 0;
};

struct KernelSpan {
    std::string name;
    uint64_t start = 0;
    uint64_t end = 0;
};

struct Collected {
    std::map<uint32_t, std::string> functionNames;
    std::map<std::pair<uint32_t, uint32_t>, PcCount> counts;
    std::map<uint32_t, std::set<uint32_t>> launchesByFunction;
    std::vector<KernelSpan> kernels;
    std::string cubinDirectory;
    int cubinsWritten = 0;
};

CuptiApi cupti;
Collected collected;

template <typename Function>
bool resolve(HMODULE library, const char* name, Function& function) {
    function = reinterpret_cast<Function>(GetProcAddress(library, name));
    return function != nullptr;
}

bool loadCupti(const std::string& path) {
    HMODULE library = LoadLibraryA(path.c_str());
    if (!library) {
        std::fprintf(stderr, "cannot load %s\n", path.c_str());
        return false;
    }
    return resolve(library, "cuptiSubscribe", cupti.subscribe) &&
           resolve(library, "cuptiEnableDomain", cupti.enableDomain) &&
           resolve(library, "cuptiActivityRegisterCallbacks", cupti.registerCallbacks) &&
           resolve(library, "cuptiActivityEnable", cupti.enable) &&
           resolve(library, "cuptiActivityFlushAll", cupti.flushAll) &&
           resolve(library, "cuptiActivityGetNextRecord", cupti.nextRecord) &&
           resolve(library, "cuptiGetResultString", cupti.resultString);
}

bool succeeded(CUptiResult result, const char* what) {
    if (result == CUPTI_SUCCESS) {
        return true;
    }
    const char* text = "unknown";
    cupti.resultString(result, &text);
    std::fprintf(stderr, "%s failed: %s\n", what, text);
    return false;
}

void CUPTIAPI onBufferRequested(uint8_t** buffer, size_t* size, size_t* maxRecords) {
    *buffer = static_cast<uint8_t*>(_aligned_malloc(activityBufferBytes, activityBufferAlignment));
    *size = activityBufferBytes;
    *maxRecords = 0;
}

void recordActivity(const CUpti_Activity* record) {
    if (record->kind == CUPTI_ACTIVITY_KIND_INSTRUCTION_EXECUTION) {
        const auto* instruction = reinterpret_cast<const CUpti_ActivityInstructionExecution*>(record);
        PcCount& count = collected.counts[{instruction->functionId, instruction->pcOffset}];
        count.warpExecutions += instruction->executed;
        count.threadExecutions += instruction->threadsExecuted;
        count.activeThreadExecutions += instruction->notPredOffThreadsExecuted;
        collected.launchesByFunction[instruction->functionId].insert(instruction->correlationId);
    } else if (record->kind == CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL) {
        const auto* kernel = reinterpret_cast<const CUpti_ActivityKernel9*>(record);
        collected.kernels.push_back({kernel->name ? kernel->name : "?", kernel->start, kernel->end});
    } else if (record->kind == CUPTI_ACTIVITY_KIND_FUNCTION) {
        const auto* function = reinterpret_cast<const CUpti_ActivityFunction*>(record);
        collected.functionNames[function->id] = function->name;
    }
}

void CUPTIAPI onBufferCompleted(CUcontext, uint32_t, uint8_t* buffer, size_t, size_t validSize) {
    CUpti_Activity* record = nullptr;
    while (cupti.nextRecord(buffer, validSize, &record) == CUPTI_SUCCESS) {
        recordActivity(record);
    }
    _aligned_free(buffer);
}

void CUPTIAPI onModuleLoaded(void*, CUpti_CallbackDomain domain, CUpti_CallbackId id, const void* data) {
    if (domain != CUPTI_CB_DOMAIN_RESOURCE || id != CUPTI_CBID_RESOURCE_MODULE_LOADED) {
        return;
    }
    const auto* resource = static_cast<const CUpti_ResourceData*>(data);
    const auto* module = static_cast<const CUpti_ModuleResourceData*>(resource->resourceDescriptor);
    if (!module || !module->pCubin || module->cubinSize == 0) {
        return;
    }
    const std::string path = collected.cubinDirectory + "/module" + std::to_string(module->moduleId) + ".cubin";
    std::ofstream(path, std::ios::binary).write(module->pCubin, static_cast<std::streamsize>(module->cubinSize));
    ++collected.cubinsWritten;
}

bool startCollection(bool timeline) {
    if (!succeeded(cupti.registerCallbacks(onBufferRequested, onBufferCompleted), "cuptiActivityRegisterCallbacks")) {
        return false;
    }
    if (timeline) {
        return succeeded(cupti.enable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL), "enable kernel activity");
    }
    CUpti_SubscriberHandle subscriber{};
    return succeeded(cupti.subscribe(&subscriber, reinterpret_cast<CUpti_CallbackFunc>(onModuleLoaded), nullptr),
                     "cuptiSubscribe") &&
           succeeded(cupti.enableDomain(1, subscriber, CUPTI_CB_DOMAIN_RESOURCE), "cuptiEnableDomain") &&
           succeeded(cupti.enable(CUPTI_ACTIVITY_KIND_FUNCTION), "enable function activity") &&
           succeeded(cupti.enable(CUPTI_ACTIVITY_KIND_INSTRUCTION_EXECUTION), "enable instruction execution");
}

std::vector<BallSpawn> scatterBalls(const BrickBoard& board, size_t count, float speed) {
    std::mt19937 random(1234u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_int_distribution<int> colorDistribution(0, ballColorCount - 1);
    std::vector<BallSpawn> balls;
    balls.reserve(count);
    while (balls.size() < count) {
        const float x = unit(random) * static_cast<float>(board.columns());
        const float y = unit(random) * static_cast<float>(board.rows());
        if (board.brickAt(static_cast<int>(x), static_cast<int>(y)) >= 0) {
            continue;
        }
        const float angle = unit(random) * 2.0f * std::numbers::pi_v<float>;
        balls.push_back({x, y, std::cos(angle) * speed, std::sin(angle) * speed, colorDistribution(random)});
    }
    return balls;
}

struct Options {
    std::string cuptiPath;
    std::string levelDirectory;
    std::string levelName = "DA_1000Bricks";
    std::string outputDirectory = ".";
    std::string mode = "instructions";
    size_t balls = 4'000'000;
    int substeps = 2;
    int ticks = 6;
};

Options parseOptions(int argumentCount, char** arguments) {
    Options options;
    for (int i = 1; i + 1 < argumentCount; i += 2) {
        const std::string key = arguments[i];
        const char* value = arguments[i + 1];
        if (key == "--cupti") {
            options.cuptiPath = value;
        } else if (key == "--dir") {
            options.levelDirectory = value;
        } else if (key == "--level") {
            options.levelName = value;
        } else if (key == "--out") {
            options.outputDirectory = value;
        } else if (key == "--mode") {
            options.mode = value;
        } else if (key == "--balls") {
            options.balls = std::strtoull(value, nullptr, 10);
        } else if (key == "--substeps") {
            options.substeps = std::atoi(value);
        } else if (key == "--ticks") {
            options.ticks = std::atoi(value);
        }
    }
    return options;
}

BrickLayout layoutFor(const Options& options, bool& ok) {
    BrickLayout layout;
    ok = true;
    if (options.levelName == "empty") {
        layout.width = 30;
        layout.height = 1;
        layout.playableHeight = 38;
        layout.cellIds.assign(30, emptyBrickId);
        return layout;
    }
    auto level = loadLevel(levelFilesFor(options.levelDirectory, options.levelName));
    if (!level) {
        std::fprintf(stderr, "level: %s\n", level.error().c_str());
        ok = false;
        return layout;
    }
    return level->bricks;
}

void writeInstructionCounts(const Options& options) {
    const std::string csvPath = options.outputDirectory + "/instructions.csv";
    std::ofstream csv(csvPath);
    csv << "function,launches,pc,warp_executions,thread_executions,active_thread_executions\n";
    for (const auto& [key, count] : collected.counts) {
        const auto name = collected.functionNames.find(key.first);
        csv << (name == collected.functionNames.end() ? std::to_string(key.first) : name->second) << ','
            << collected.launchesByFunction[key.first].size() << ',' << key.second << ',' << count.warpExecutions << ','
            << count.threadExecutions << ',' << count.activeThreadExecutions << '\n';
    }
    std::printf("cubins=%d pcs=%zu -> %s\n", collected.cubinsWritten, collected.counts.size(), csvPath.c_str());
}

void printTimeline(const std::vector<double>& eventMs) {
    size_t tick = 0;
    for (const KernelSpan& kernel : collected.kernels) {
        const bool isStep = kernel.name.find("step") != std::string::npos;
        std::printf("%-26s %8.3f ms", kernel.name.c_str(),
                    static_cast<double>(kernel.end - kernel.start) / nanosecondsPerMillisecond);
        if (isStep && tick < eventMs.size()) {
            std::printf("   backend event %8.3f ms", eventMs[tick++]);
        }
        std::printf("\n");
    }
}

}

int main(int argumentCount, char** arguments) {
    const Options options = parseOptions(argumentCount, arguments);
    if (options.cuptiPath.empty() || options.levelDirectory.empty()) {
        std::fprintf(stderr, "usage: balls_insncount --cupti <cupti64.dll> --dir <levels> [--mode instructions|timeline] "
                             "[--level name|empty] [--out dir] [--balls n] [--substeps n] [--ticks n]\n");
        return 2;
    }
    const bool timeline = options.mode == "timeline";
    collected.cubinDirectory = options.outputDirectory;
    if (!loadCupti(options.cuptiPath) || !startCollection(timeline)) {
        return 1;
    }
    bool layoutOk = false;
    const BrickLayout layout = layoutFor(options, layoutOk);
    if (!layoutOk) {
        return 1;
    }
    std::vector<double> eventMs;
    {
        Simulation simulation(createBackend(BackendKind::Cuda));
        if (simulation.backend().name() != std::string_view("CUDA")) {
            std::fprintf(stderr, "CUDA backend unavailable\n");
            return 1;
        }
        simulation.load(layout);
        PhysicsBackend& backend = simulation.backend();
        backend.addBalls(scatterBalls(simulation.board(), options.balls, simulation.tuning().ballSpeed));
        const StepParams params = simulation.stepParams(options.substeps);
        std::vector<BallHit> hits;
        for (int tick = 0; tick < options.ticks; ++tick) {
            backend.step(params, hits);
            eventMs.push_back(backend.deviceStepMs());
        }
    }
    cupti.flushAll(1);
    if (timeline) {
        printTimeline(eventMs);
    } else {
        writeInstructionCounts(options);
    }
    return 0;
}
