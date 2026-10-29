#include "common/ParallelChunks.h"

#include "common/WorkerPool.h"

#include <algorithm>
#include <thread>

// One pool for the process, made on first use: the Auto ramp calls this every frame, and a task per core per call
// cost more than the launches it split.
void parallelChunks(size_t count, size_t chunkSize, const std::function<void(size_t, size_t, size_t)>& task) {
    chunkSize = std::max<size_t>(chunkSize, 1);
    if (count <= chunkSize) {
        if (count > 0) {
            task(0, 0, count);
        }
        return;
    }
    static WorkerPool pool(std::max(1u, std::thread::hardware_concurrency()));
    pool.runChunks(count, chunkSize, task);
}
