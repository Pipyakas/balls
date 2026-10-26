#include "sim/backends/CudaDriver.h"

#include "BallKernelsPtx.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <format>
#include <stdexcept>

namespace {

constexpr const char* driverLibraryName = "nvcuda.dll";

template <typename Function>
bool loadSymbol(HMODULE library, const char* name, Function& target) {
    target = reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(library, name)));
    return target != nullptr;
}

}

CudaDriver& CudaDriver::instance() {
    static CudaDriver driver;
    return driver;
}

CudaDriver::CudaDriver() {
    if (loadLibrary()) {
        createContextAndModule();
    }
}

bool CudaDriver::loadLibrary() {
    HMODULE library = LoadLibraryA(driverLibraryName);
    if (!library) {
        errorMessage = "no NVIDIA driver (nvcuda.dll)";
        return false;
    }
    const bool loaded = loadSymbol(library, "cuInit", functions.init) &&
                        loadSymbol(library, "cuDeviceGetCount", functions.deviceGetCount) &&
                        loadSymbol(library, "cuDeviceGet", functions.deviceGet) &&
                        loadSymbol(library, "cuDeviceGetName", functions.deviceGetName) &&
                        loadSymbol(library, "cuDevicePrimaryCtxRetain", functions.primaryContextRetain) &&
                        loadSymbol(library, "cuCtxSetCurrent", functions.contextSetCurrent) &&
                        loadSymbol(library, "cuModuleLoadDataEx", functions.moduleLoadDataEx) &&
                        loadSymbol(library, "cuModuleGetFunction", functions.moduleGetFunction) &&
                        loadSymbol(library, "cuMemAlloc_v2", functions.memAlloc) &&
                        loadSymbol(library, "cuMemFree_v2", functions.memFree) &&
                        loadSymbol(library, "cuMemAllocHost_v2", functions.memAllocHost) &&
                        loadSymbol(library, "cuMemFreeHost", functions.memFreeHost) &&
                        loadSymbol(library, "cuMemcpyHtoD_v2", functions.memcpyHostToDevice) &&
                        loadSymbol(library, "cuMemcpyDtoH_v2", functions.memcpyDeviceToHost) &&
                        loadSymbol(library, "cuMemcpyDtoD_v2", functions.memcpyDeviceToDevice) &&
                        loadSymbol(library, "cuMemsetD32_v2", functions.memsetD32) &&
                        loadSymbol(library, "cuLaunchKernel", functions.launchKernel) &&
                        loadSymbol(library, "cuCtxSynchronize", functions.contextSynchronize) &&
                        loadSymbol(library, "cuGetErrorString", functions.getErrorString);
    if (!loaded) {
        errorMessage = "NVIDIA driver is missing required entry points";
    }
    return loaded;
}

bool CudaDriver::createContextAndModule() {
    auto fail = [this](CUresult result, const char* what) {
        const char* text = nullptr;
        functions.getErrorString(result, &text);
        errorMessage = std::format("{}: {}", what, text ? text : "unknown error");
        return false;
    };
    if (CUresult result = functions.init(0); result != CUDA_SUCCESS) {
        return fail(result, "cuInit");
    }
    int deviceCount = 0;
    if (CUresult result = functions.deviceGetCount(&deviceCount); result != CUDA_SUCCESS || deviceCount == 0) {
        errorMessage = "no CUDA device";
        return false;
    }
    CUdevice device{};
    functions.deviceGet(&device, 0);
    char name[256]{};
    functions.deviceGetName(name, static_cast<int>(sizeof(name)), device);
    gpuName = name;
    if (CUresult result = functions.primaryContextRetain(&context, device); result != CUDA_SUCCESS) {
        return fail(result, "cuDevicePrimaryCtxRetain");
    }
    functions.contextSetCurrent(context);
    if (CUresult result = functions.moduleLoadDataEx(&module, ballKernelsPtx, 0, nullptr, nullptr);
        result != CUDA_SUCCESS) {
        return fail(result, "cuModuleLoadDataEx");
    }
    return true;
}

CUfunction CudaDriver::function(const char* name) const {
    CUfunction result = nullptr;
    check(functions.moduleGetFunction(&result, module, name), name);
    return result;
}

void CudaDriver::makeCurrent() const { functions.contextSetCurrent(context); }

void CudaDriver::check(CUresult result, const char* what) const {
    if (result == CUDA_SUCCESS) {
        return;
    }
    const char* text = nullptr;
    functions.getErrorString(result, &text);
    throw std::runtime_error(std::format("{}: {}", what, text ? text : "unknown CUDA error"));
}
