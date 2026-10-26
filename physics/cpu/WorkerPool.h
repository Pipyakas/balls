#pragma once

#include <atomic>
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

    size_t threadCount() const { return workers.size() + 1; }
    static size_t chunkCountFor(size_t itemCount, size_t chunkSize);
    void runChunks(size_t itemCount, size_t chunkSize, const ChunkTask& task);

private:
    void workerLoop();
    void drainChunks();

    std::vector<std::jthread> workers;
    std::mutex mutex;
    std::condition_variable wakeCondition;
    std::condition_variable doneCondition;
    const ChunkTask* currentTask = nullptr;
    size_t currentItemCount = 0;
    size_t currentChunkSize = 1;
    std::atomic<size_t> nextChunk = 0;
    size_t generation = 0;
    size_t pendingWorkers = 0;
    bool stopping = false;
};
