#pragma once

#include <cstddef>
#include <functional>

void parallelChunks(size_t count, size_t chunkSize, const std::function<void(size_t, size_t, size_t)>& task);
