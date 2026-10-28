#pragma once

#include "common/DynamicLibrary.h"

#include <cuda.h>

#include <memory>
#include <string>

struct CudaDriverApi {
    decltype(&::cuInit) init = nullptr;
    decltype(&::cuDeviceGetCount) deviceGetCount = nullptr;
    decltype(&::cuDeviceGet) deviceGet = nullptr;
    decltype(&::cuDeviceGetName) deviceGetName = nullptr;
    decltype(&::cuDevicePrimaryCtxRetain) primaryContextRetain = nullptr;
    decltype(&::cuCtxSetCurrent) contextSetCurrent = nullptr;
    decltype(&::cuModuleLoadDataEx) moduleLoadDataEx = nullptr;
    decltype(&::cuModuleGetFunction) moduleGetFunction = nullptr;
    decltype(&::cuMemAlloc_v2) memAlloc = nullptr;
    decltype(&::cuMemFree_v2) memFree = nullptr;
    decltype(&::cuMemGetInfo_v2) memGetInfo = nullptr;
    decltype(&::cuMemAllocHost_v2) memAllocHost = nullptr;
    decltype(&::cuMemFreeHost) memFreeHost = nullptr;
    decltype(&::cuMemcpyHtoD_v2) memcpyHostToDevice = nullptr;
    decltype(&::cuMemcpyHtoDAsync_v2) memcpyHostToDeviceAsync = nullptr;
    decltype(&::cuMemcpyDtoH_v2) memcpyDeviceToHost = nullptr;
    decltype(&::cuMemcpyDtoD_v2) memcpyDeviceToDevice = nullptr;
    decltype(&::cuMemsetD32_v2) memsetD32 = nullptr;
    decltype(&::cuLaunchKernel) launchKernel = nullptr;
    decltype(&::cuCtxSynchronize) contextSynchronize = nullptr;
    decltype(&::cuGetErrorString) getErrorString = nullptr;
    decltype(&::cuEventCreate) eventCreate = nullptr;
    decltype(&::cuEventRecord) eventRecord = nullptr;
    decltype(&::cuEventSynchronize) eventSynchronize = nullptr;
    decltype(&::cuEventElapsedTime) eventElapsedTime = nullptr;
    decltype(&::cuEventDestroy_v2) eventDestroy = nullptr;
    decltype(&::cuDeviceGetAttribute) deviceGetAttribute = nullptr;
    decltype(&::cuMemAddressReserve) memAddressReserve = nullptr;
    decltype(&::cuMemAddressFree) memAddressFree = nullptr;
    decltype(&::cuMemCreate) memCreate = nullptr;
    decltype(&::cuMemRelease) memRelease = nullptr;
    decltype(&::cuMemMap) memMap = nullptr;
    decltype(&::cuMemUnmap) memUnmap = nullptr;
    decltype(&::cuMemSetAccess) memSetAccess = nullptr;
    decltype(&::cuMemGetAllocationGranularity) memGetAllocationGranularity = nullptr;
    CUresult(CUDAAPI* graphicsGlRegisterBuffer)(CUgraphicsResource*, unsigned int, unsigned int) = nullptr;
    decltype(&::cuGraphicsUnregisterResource) graphicsUnregisterResource = nullptr;
    decltype(&::cuGraphicsMapResources) graphicsMapResources = nullptr;
    decltype(&::cuGraphicsUnmapResources) graphicsUnmapResources = nullptr;
    decltype(&::cuGraphicsResourceGetMappedPointer_v2) graphicsResourceGetMappedPointer = nullptr;
};

class CudaDriver {
public:
    static CudaDriver& instance();

    bool isReady() const { return errorMessage.empty(); }
    const std::string& error() const { return errorMessage; }
    const std::string& deviceName() const { return gpuName; }
    int deviceAttribute(CUdevice_attribute attribute) const;
    CUdevice cudaDevice() const { return device; }
    const CudaDriverApi& api() const { return functions; }
    CUfunction function(const char* name) const;
    void makeCurrent() const;
    std::string describe(CUresult result, const char* what) const;

private:
    CudaDriver();

    bool loadEntryPoints();
    bool createContextAndModule();

    std::unique_ptr<DynamicLibrary> library;
    CudaDriverApi functions;
    CUdevice device = 0;
    CUcontext context = nullptr;
    CUmodule module = nullptr;
    std::string gpuName;
    std::string errorMessage;
};
