#include "cuda/GrowableDeviceArray.h"

#include <algorithm>

namespace {

CUmemAllocationProp deviceAllocation(CUdevice device) {
    CUmemAllocationProp properties{};
    properties.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    properties.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    properties.location.id = device;
    return properties;
}

size_t roundUp(size_t bytes, size_t step) { return (bytes + step - 1) / step * step; }

}

bool GrowableDeviceArray::supported(const CudaDriver& driver) {
    const CudaDriverApi& api = driver.api();
    return api.memAddressReserve && api.memAddressFree && api.memCreate && api.memRelease && api.memMap &&
           api.memUnmap && api.memSetAccess && api.memGetAllocationGranularity &&
           driver.deviceAttribute(CU_DEVICE_ATTRIBUTE_VIRTUAL_MEMORY_MANAGEMENT_SUPPORTED) != 0;
}

bool GrowableDeviceArray::reserve(const CudaDriver& driver, size_t maximumBytes) {
    release();
    owner = &driver;
    const CudaDriverApi& api = driver.api();
    const CUmemAllocationProp properties = deviceAllocation(driver.cudaDevice());
    if (api.memGetAllocationGranularity(&granularity, &properties, CU_MEM_ALLOC_GRANULARITY_RECOMMENDED) !=
            CUDA_SUCCESS ||
        granularity == 0) {
        return false;
    }
    reserved = roundUp(std::max<size_t>(maximumBytes, 1), granularity);
    if (api.memAddressReserve(&base, reserved, granularity, 0, 0) != CUDA_SUCCESS) {
        base = 0;
        reserved = 0;
        return false;
    }
    return true;
}

bool GrowableDeviceArray::growTo(size_t bytes) {
    if (!base || bytes <= mapped) {
        return base != 0;
    }
    if (bytes > reserved) {
        return false;
    }
    const CudaDriverApi& api = owner->api();
    const size_t added = roundUp(bytes - mapped, granularity);
    if (mapped + added > reserved) {
        return false;
    }
    const CUmemAllocationProp properties = deviceAllocation(owner->cudaDevice());
    CUmemGenericAllocationHandle handle = 0;
    if (api.memCreate(&handle, added, &properties, 0) != CUDA_SUCCESS) {
        return false;
    }
    if (api.memMap(base + mapped, added, 0, handle, 0) != CUDA_SUCCESS) {
        api.memRelease(handle);
        return false;
    }
    CUmemAccessDesc access{};
    access.location = properties.location;
    access.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    if (api.memSetAccess(base + mapped, added, &access, 1) != CUDA_SUCCESS) {
        api.memUnmap(base + mapped, added);
        api.memRelease(handle);
        return false;
    }
    mappings.push_back({handle, mapped, added});
    mapped += added;
    return true;
}

void GrowableDeviceArray::shrinkTo(size_t bytes) {
    if (!base) {
        return;
    }
    const CudaDriverApi& api = owner->api();
    while (!mappings.empty() && mappings.back().offset >= bytes) {
        const Mapping last = mappings.back();
        mappings.pop_back();
        api.memUnmap(base + last.offset, last.bytes);
        api.memRelease(last.handle);
        mapped = last.offset;
    }
}

void GrowableDeviceArray::release() {
    if (!base) {
        return;
    }
    shrinkTo(0);
    owner->api().memAddressFree(base, reserved);
    base = 0;
    reserved = 0;
    mapped = 0;
}
