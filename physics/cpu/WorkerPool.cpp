#include "cpu/WorkerPool.h"

#include <algorithm>

WorkerPool::WorkerPool(size_t threadCount) {
    const size_t helperCount = threadCount > 1 ? threadCount - 1 : 0;
    workers.reserve(helperCount);
    for (size_t i = 0; i < helperCount; ++i) {
        workers.emplace_back([this] { workerLoop(); });
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

void WorkerPool::workerLoop() {
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
