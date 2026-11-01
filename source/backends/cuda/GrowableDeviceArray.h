#pragma once

#include "cuda/CudaDriver.h"

#include <cstddef>
#include <vector>

class GrowableDeviceArray {
public:
    static bool supported(const CudaDriver& driver);

    bool reserve(const CudaDriver& driver, size_t maximumBytes);

    bool growTo(size_t bytes);

    void shrinkTo(size_t bytes);
    void release();

    CUdeviceptr pointer() const { return base; }
    size_t mappedBytes() const { return mapped; }
    bool isReserved() const { return base != 0; }

private:
    struct Mapping {
        CUmemGenericAllocationHandle handle;
        size_t offset;
        size_t bytes;
    };

    const CudaDriver* owner = nullptr;
    CUdeviceptr base = 0;
    size_t reserved = 0;
    size_t mapped = 0;
    size_t granularity = 0;
    std::vector<Mapping> mappings;
};
