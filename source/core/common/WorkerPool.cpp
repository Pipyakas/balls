#include "common/WorkerPool.h"

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

// Chunks are claimed one at a time from the cursor, and only while it still names this job's generation.
void WorkerPool::drainChunks(const Job& job) {
    uint64_t claimed = cursor.load(std::memory_order_acquire);
    for (;;) {
        if ((claimed >> 32) != (job.generation & 0xFFFFFFFFu) || (claimed & 0xFFFFFFFFu) >= job.chunks) {
            return;
        }
        if (!cursor.compare_exchange_weak(claimed, claimed + 1, std::memory_order_acq_rel)) {
            continue;
        }
        const size_t chunk = static_cast<size_t>(claimed & 0xFFFFFFFFu);
        const size_t begin = chunk * job.chunkSize;
        (*job.task)(chunk, begin, std::min(begin + job.chunkSize, job.itemCount));
        if (chunksDone.fetch_add(1, std::memory_order_acq_rel) + 1 == job.chunks) {
            std::lock_guard lock(mutex);
            doneCondition.notify_all();
        }
        claimed = cursor.load(std::memory_order_acquire);
    }
}

// A worker only helps with the job it woke for; the caller waits for that job's chunks, not for every worker to
// wake, so a worker the scheduler runs late (they run below normal priority next to a GPU) delays nothing.
void WorkerPool::workerLoop(WorkerPriority priority) {
    applyPriority(priority);
    uint64_t seenGeneration = 0;
    while (true) {
        Job job;
        {
            std::unique_lock lock(mutex);
            wakeCondition.wait(lock, [&] { return stopping || current.generation != seenGeneration; });
            if (stopping) {
                return;
            }
            seenGeneration = current.generation;
            job = current;
        }
        drainChunks(job);
    }
}

size_t WorkerPool::chunkCountFor(size_t itemCount, size_t chunkSize) {
    const size_t safeChunk = std::max<size_t>(chunkSize, 1);
    return (itemCount + safeChunk - 1) / safeChunk;
}

void WorkerPool::runChunks(size_t itemCount, size_t chunkSize, const ChunkTask& task) {
    Job job;
    {
        std::lock_guard lock(mutex);
        job.task = &task;
        job.itemCount = itemCount;
        job.chunkSize = std::max<size_t>(chunkSize, 1);
        job.chunks = chunkCountFor(itemCount, job.chunkSize);
        job.generation = current.generation + 1;
        chunksDone.store(0, std::memory_order_relaxed);
        cursor.store((job.generation & 0xFFFFFFFFu) << 32, std::memory_order_release);
        current = job;
    }
    if (job.chunks == 0) {
        return;
    }
    if (job.chunks > 1) {
        wakeCondition.notify_all();
    }
    drainChunks(job);
    std::unique_lock lock(mutex);
    doneCondition.wait(lock, [&] { return chunksDone.load(std::memory_order_acquire) == job.chunks; });
}
