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
    decltype(&::cuMemAllocHost_v2) memAllocHost = nullptr;
    decltype(&::cuMemFreeHost) memFreeHost = nullptr;
    decltype(&::cuMemcpyHtoD_v2) memcpyHostToDevice = nullptr;
    decltype(&::cuMemcpyDtoH_v2) memcpyDeviceToHost = nullptr;
    decltype(&::cuMemcpyDtoD_v2) memcpyDeviceToDevice = nullptr;
    decltype(&::cuMemsetD32_v2) memsetD32 = nullptr;
    decltype(&::cuLaunchKernel) launchKernel = nullptr;
    decltype(&::cuCtxSynchronize) contextSynchronize = nullptr;
    decltype(&::cuGetErrorString) getErrorString = nullptr;
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
    CUcontext context = nullptr;
    CUmodule module = nullptr;
    std::string gpuName;
    std::string errorMessage;
};
