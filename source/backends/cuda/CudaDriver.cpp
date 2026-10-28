#include "cuda/CudaDriver.h"

#include "BallKernelImages.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <vector>

namespace {

constexpr const char* forcedImageVariable = "BALLS_CUDA_IMAGE";

std::string imageName(const BallKernelImage& image) {
    return std::format("{}_{}{}", image.binary ? "sm" : "compute", image.computeMajor, image.computeMinor);
}

std::string forcedImageName() {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, forcedImageVariable) != 0 || !value) {
        return {};
    }
    std::string name(value);
    std::free(value);
    return name;
#else
    const char* value = std::getenv(forcedImageVariable);
    return value ? value : "";
#endif
}

std::vector<const BallKernelImage*> imagesToTry(int computeMajor, int computeMinor) {
    if (const std::string forced = forcedImageName(); !forced.empty()) {
        std::vector<const BallKernelImage*> images;
        for (const BallKernelImage& image : ballKernelImages) {
            if (imageName(image) == forced) {
                images.push_back(&image);
            }
        }
        return images;
    }
    const int deviceVersion = computeMajor * 10 + computeMinor;
    const auto versionOf = [](const BallKernelImage& image) { return image.computeMajor * 10 + image.computeMinor; };
    std::vector<const BallKernelImage*> images;
    for (const BallKernelImage& image : ballKernelImages) {
        const bool binaryRuns = image.computeMajor == computeMajor && image.computeMinor <= computeMinor;
        if (image.binary ? binaryRuns : versionOf(image) <= deviceVersion) {
            images.push_back(&image);
        }
    }
    std::ranges::stable_sort(images, [&](const BallKernelImage* left, const BallKernelImage* right) {
        if (left->binary != right->binary) {
            return left->binary;
        }
        return versionOf(*left) > versionOf(*right);
    });
    return images;
}

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
                        driver.resolve("cuMemGetInfo_v2", functions.memGetInfo) &&
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
    driver.resolve("cuMemcpyHtoDAsync_v2", functions.memcpyHostToDeviceAsync);
    driver.resolve("cuMemAddressReserve", functions.memAddressReserve);
    driver.resolve("cuMemAddressFree", functions.memAddressFree);
    driver.resolve("cuMemCreate", functions.memCreate);
    driver.resolve("cuMemRelease", functions.memRelease);
    driver.resolve("cuMemMap", functions.memMap);
    driver.resolve("cuMemUnmap", functions.memUnmap);
    driver.resolve("cuMemSetAccess", functions.memSetAccess);
    driver.resolve("cuMemGetAllocationGranularity", functions.memGetAllocationGranularity);
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
    const int computeMajor = deviceAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR);
    const int computeMinor = deviceAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
    CUresult lastResult = CUDA_ERROR_NO_BINARY_FOR_GPU;
    for (const BallKernelImage* image : imagesToTry(computeMajor, computeMinor)) {
        lastResult = functions.moduleLoadDataEx(&module, image->bytes, 0, nullptr, nullptr);
        if (lastResult == CUDA_SUCCESS) {
            gpuName += std::format(" ({})", imageName(*image));
            return true;
        }
    }
    module = nullptr;
    errorMessage = describe(lastResult, "cuModuleLoadDataEx");
    return false;
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
