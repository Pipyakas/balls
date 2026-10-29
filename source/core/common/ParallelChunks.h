#pragma once

#include <cstddef>
#include <functional>

// Runs task(chunkIndex, begin, end) over [0, count) in chunks of chunkSize on every core, returning once all are
// done; a count of one chunk runs on the calling thread. Calls from several threads at once are not supported. Chunks are fixed by the count and size alone, so work keyed
// on the chunk index comes out the same on any core count.
void parallelChunks(size_t count, size_t chunkSize, const std::function<void(size_t, size_t, size_t)>& task);
