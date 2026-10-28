#include "cpu/WorkerPool.h"

#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

void requestFullSpeed() {
#if defined(_WIN32)
    THREAD_POWER_THROTTLING_STATE state{};
    state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
#endif
}

void requestFullSpeedForProcess() {
#if defined(_WIN32)
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
#endif
}

void applyPriority(WorkerPriority priority) {
    requestFullSpeed();
#if defined(_WIN32)
    if (priority == WorkerPriority::Background) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    }
#else
    static_cast<void>(priority);
#endif
}

}

WorkerPool::WorkerPool(size_t threadCount, WorkerPriority priority) {
    requestFullSpeedForProcess();
    requestFullSpeed();
    const size_t helperCount = threadCount > 1 ? threadCount - 1 : 0;
    workers.reserve(helperCount);
    for (size_t i = 0; i < helperCount; ++i) {
        workers.emplace_back([this, priority] { workerLoop(priority); });
    }
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wakeCondition.notify_all();
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();
}

void WorkerPool::drainChunks() {
    const size_t itemCount = currentItemCount;
    const size_t chunkSize = currentChunkSize;
    const size_t chunks = (itemCount + chunkSize - 1) / chunkSize;
    for (size_t chunk = nextChunk.fetch_add(1, std::memory_order_relaxed); chunk < chunks;
         chunk = nextChunk.fetch_add(1, std::memory_order_relaxed)) {
        const size_t begin = chunk * chunkSize;
        (*currentTask)(chunk, begin, std::min(begin + chunkSize, itemCount));
    }
}

void WorkerPool::workerLoop(WorkerPriority priority) {
    applyPriority(priority);
    size_t seenGeneration = 0;
    while (true) {
        {
            std::unique_lock lock(mutex);
            wakeCondition.wait(lock, [&] { return stopping || generation != seenGeneration; });
            if (stopping) {
                return;
            }
            seenGeneration = generation;
        }
        drainChunks();
        {
            std::lock_guard lock(mutex);
            --pendingWorkers;
        }
        doneCondition.notify_one();
    }
}

size_t WorkerPool::chunkCountFor(size_t itemCount, size_t chunkSize) {
    const size_t safeChunk = std::max<size_t>(chunkSize, 1);
    return (itemCount + safeChunk - 1) / safeChunk;
}

void WorkerPool::runChunks(size_t itemCount, size_t chunkSize, const ChunkTask& task) {
    {
        std::lock_guard lock(mutex);
        currentTask = &task;
        currentItemCount = itemCount;
        currentChunkSize = std::max<size_t>(chunkSize, 1);
        nextChunk.store(0, std::memory_order_relaxed);
        pendingWorkers = workers.size();
        ++generation;
    }
    wakeCondition.notify_all();
    drainChunks();
    std::unique_lock lock(mutex);
    doneCondition.wait(lock, [&] { return pendingWorkers == 0; });
}
