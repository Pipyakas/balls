#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

enum class WorkerPriority { Normal, Background };

// A tuning value from the environment, or the fallback: lets a benchmark try other settings without a rebuild.
long environmentNumber(const char* name, long fallback);
// Keeps the calling thread on the CPU's fastest cores (on a hybrid CPU, the P-cores); a no-op elsewhere.
void preferPerformanceCores();

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
    void workerLoop(WorkerPriority priority);
    void drainChunks();

    std::vector<std::thread> workers;
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
