#include "common/ParallelChunks.h"

#include "common/WorkerPool.h"

#include <algorithm>
#include <thread>

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
