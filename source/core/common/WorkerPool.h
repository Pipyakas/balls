#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

enum class WorkerPriority { Normal, Background };

class WorkerPool {
public:
    using ChunkTask = std::function<void(size_t chunkIndex, size_t begin, size_t end)>;

    explicit WorkerPool(size_t threadCount, WorkerPriority priority = WorkerPriority::Normal);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    size_t threadCount() const { return workers.size() + 1; }
    static size_t chunkCountFor(size_t itemCount, size_t chunkSize);
    void runChunks(size_t itemCount, size_t chunkSize, const ChunkTask& task);

private:
    struct Job {
        const ChunkTask* task = nullptr;
        size_t itemCount = 0;
        size_t chunkSize = 1;
        size_t chunks = 0;
        uint64_t generation = 0;
    };

    void workerLoop(WorkerPriority priority);
    void drainChunks(const Job& job);

    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wakeCondition;
    std::condition_variable doneCondition;
    Job current;
    // The job's generation in the high 32 bits and its next chunk in the low ones, so a worker that wakes late for a
    // finished job cannot take a chunk of the next one.
    std::atomic<uint64_t> cursor = 0;
    std::atomic<size_t> chunksDone = 0;
    bool stopping = false;
};
