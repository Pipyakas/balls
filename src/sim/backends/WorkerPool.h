#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class WorkerPool {
public:
    using ChunkTask = std::function<void(size_t chunkIndex, size_t begin, size_t end)>;

    explicit WorkerPool(size_t threadCount);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    size_t chunkCount() const { return workers.size() + 1; }
    void runChunks(size_t itemCount, size_t granularity, const ChunkTask& task);

private:
    void workerLoop(size_t workerIndex);
    void runChunk(size_t chunkIndex);

    std::vector<std::jthread> workers;
    std::mutex mutex;
    std::condition_variable wakeCondition;
    std::condition_variable doneCondition;
    const ChunkTask* currentTask = nullptr;
    size_t currentItemCount = 0;
    size_t currentGranularity = 1;
    size_t generation = 0;
    size_t pendingWorkers = 0;
    bool stopping = false;
};
