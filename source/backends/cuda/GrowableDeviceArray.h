#pragma once

#include "cuda/CudaDriver.h"

#include <cstddef>
#include <vector>

// A device array that grows in place. Its whole address range is reserved up front and physical memory is mapped onto
// the end as it grows, so the address never changes and growing never copies: replacing the ball arrays copied every
// ball, and near the top of device memory went through host memory, which stalled a frame for half a second.
class GrowableDeviceArray {
public:
    static bool supported(const CudaDriver& driver);

    bool reserve(const CudaDriver& driver, size_t maximumBytes);
    // Maps memory up to at least `bytes`; false when the device has no more, leaving what was mapped.
    bool growTo(size_t bytes);
    // Unmaps everything past `bytes`, rounded up to whole mappings.
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
