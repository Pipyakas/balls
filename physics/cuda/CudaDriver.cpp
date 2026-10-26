#include "cuda/CudaDriver.h"

#include "BallKernelsPtx.h"

#include <format>

namespace {

#if defined(_WIN32)
constexpr const char* driverLibraryName = "nvcuda.dll";
#else
constexpr const char* driverLibraryName = "libcuda.so.1";
#endif

}

CudaDriver& CudaDriver::instance() {
    static CudaDriver driver;
    return driver;
}

CudaDriver::CudaDriver() : library(std::make_unique<DynamicLibrary>(driverLibraryName)) {
    if (loadEntryPoints()) {
        createContextAndModule();
    }
}

bool CudaDriver::loadEntryPoints() {
    if (!library->isLoaded()) {
        errorMessage = "no NVIDIA driver";
        return false;
    }
    const DynamicLibrary& driver = *library;
    const bool loaded = driver.resolve("cuInit", functions.init) &&
                        driver.resolve("cuDeviceGetCount", functions.deviceGetCount) &&
                        driver.resolve("cuDeviceGet", functions.deviceGet) &&
                        driver.resolve("cuDeviceGetName", functions.deviceGetName) &&
                        driver.resolve("cuDevicePrimaryCtxRetain", functions.primaryContextRetain) &&
                        driver.resolve("cuCtxSetCurrent", functions.contextSetCurrent) &&
                        driver.resolve("cuModuleLoadDataEx", functions.moduleLoadDataEx) &&
                        driver.resolve("cuModuleGetFunction", functions.moduleGetFunction) &&
                        driver.resolve("cuMemAlloc_v2", functions.memAlloc) &&
                        driver.resolve("cuMemFree_v2", functions.memFree) &&
                        driver.resolve("cuMemAllocHost_v2", functions.memAllocHost) &&
                        driver.resolve("cuMemFreeHost", functions.memFreeHost) &&
                        driver.resolve("cuMemcpyHtoD_v2", functions.memcpyHostToDevice) &&
                        driver.resolve("cuMemcpyDtoH_v2", functions.memcpyDeviceToHost) &&
                        driver.resolve("cuMemcpyDtoD_v2", functions.memcpyDeviceToDevice) &&
                        driver.resolve("cuMemsetD32_v2", functions.memsetD32) &&
                        driver.resolve("cuLaunchKernel", functions.launchKernel) &&
                        driver.resolve("cuCtxSynchronize", functions.contextSynchronize) &&
                        driver.resolve("cuGetErrorString", functions.getErrorString);
    driver.resolve("cuEventCreate", functions.eventCreate);
    driver.resolve("cuEventRecord", functions.eventRecord);
    driver.resolve("cuEventSynchronize", functions.eventSynchronize);
    driver.resolve("cuEventElapsedTime", functions.eventElapsedTime);
    driver.resolve("cuEventDestroy_v2", functions.eventDestroy);
    driver.resolve("cuDeviceGetAttribute", functions.deviceGetAttribute);
    driver.resolve("cuGraphicsGLRegisterBuffer", functions.graphicsGlRegisterBuffer);
    driver.resolve("cuGraphicsUnregisterResource", functions.graphicsUnregisterResource);
    driver.resolve("cuGraphicsMapResources", functions.graphicsMapResources);
    driver.resolve("cuGraphicsUnmapResources", functions.graphicsUnmapResources);
    driver.resolve("cuGraphicsResourceGetMappedPointer_v2", functions.graphicsResourceGetMappedPointer);
    if (!loaded) {
        errorMessage = "NVIDIA driver is missing required entry points";
    }
    return loaded;
}

bool CudaDriver::createContextAndModule() {
    if (CUresult result = functions.init(0); result != CUDA_SUCCESS) {
        errorMessage = describe(result, "cuInit");
        return false;
    }
    int deviceCount = 0;
    if (functions.deviceGetCount(&deviceCount) != CUDA_SUCCESS || deviceCount == 0) {
        errorMessage = "no CUDA device";
        return false;
    }
    functions.deviceGet(&device, 0);
    char name[256]{};
    functions.deviceGetName(name, static_cast<int>(sizeof(name)), device);
    gpuName = name;
    if (CUresult result = functions.primaryContextRetain(&context, device); result != CUDA_SUCCESS) {
        errorMessage = describe(result, "cuDevicePrimaryCtxRetain");
        return false;
    }
    functions.contextSetCurrent(context);
    if (CUresult result = functions.moduleLoadDataEx(&module, ballKernelsPtx, 0, nullptr, nullptr);
        result != CUDA_SUCCESS) {
        errorMessage = describe(result, "cuModuleLoadDataEx");
        return false;
    }
    return true;
}

CUfunction CudaDriver::function(const char* name) const {
    CUfunction result = nullptr;
    if (!module || functions.moduleGetFunction(&result, module, name) != CUDA_SUCCESS) {
        return nullptr;
    }
    return result;
}

int CudaDriver::deviceAttribute(CUdevice_attribute attribute) const {
    int value = 0;
    if (functions.deviceGetAttribute) {
        functions.deviceGetAttribute(&value, attribute, device);
    }
    return value;
}

void CudaDriver::makeCurrent() const { functions.contextSetCurrent(context); }

std::string CudaDriver::describe(CUresult result, const char* what) const {
    const char* text = nullptr;
    if (functions.getErrorString) {
        functions.getErrorString(result, &text);
    }
    return std::format("{}: {}", what, text ? text : "unknown CUDA error");
}
