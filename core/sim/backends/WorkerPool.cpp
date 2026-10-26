#include "sim/backends/WorkerPool.h"

#include <algorithm>

WorkerPool::WorkerPool(size_t threadCount) {
    const size_t helperCount = threadCount > 1 ? threadCount - 1 : 0;
    workers.reserve(helperCount);
    for (size_t i = 0; i < helperCount; ++i) {
        workers.emplace_back([this, i] { workerLoop(i + 1); });
    }
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wakeCondition.notify_all();
    workers.clear();
}

void WorkerPool::runChunk(size_t chunkIndex) {
    const size_t chunks = chunkCount();
    const size_t groups = (currentItemCount + currentGranularity - 1) / currentGranularity;
    const size_t beginGroup = groups * chunkIndex / chunks;
    const size_t endGroup = groups * (chunkIndex + 1) / chunks;
    const size_t begin = std::min(beginGroup * currentGranularity, currentItemCount);
    const size_t end = std::min(endGroup * currentGranularity, currentItemCount);
    (*currentTask)(chunkIndex, begin, end);
}

void WorkerPool::workerLoop(size_t workerIndex) {
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
        runChunk(workerIndex);
        {
            std::lock_guard lock(mutex);
            --pendingWorkers;
        }
        doneCondition.notify_one();
    }
}

void WorkerPool::runChunks(size_t itemCount, size_t granularity, const ChunkTask& task) {
    {
        std::lock_guard lock(mutex);
        currentTask = &task;
        currentItemCount = itemCount;
        currentGranularity = std::max<size_t>(granularity, 1);
        pendingWorkers = workers.size();
        ++generation;
    }
    wakeCondition.notify_all();
    runChunk(0);
    std::unique_lock lock(mutex);
    doneCondition.wait(lock, [&] { return pendingWorkers == 0; });
}
