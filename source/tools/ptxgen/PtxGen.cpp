#include <nvrtc.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct KernelImage {
    int computeMajor;
    int computeMinor;
    bool binary;
};

constexpr int warpMatchComputeMajor = 7;

constexpr KernelImage defaultKernelImages[] = {
    {5, 0, true},  {6, 0, true},  {7, 0, true},  {7, 5, true},  {8, 0, true},
    {8, 6, true},  {8, 9, true},  {7, 5, false}, {5, 0, false},
};
#if defined(_WIN32)
constexpr const char* nvrtcLibraryName = "nvrtc64_120_0.dll";
#else
constexpr const char* nvrtcLibraryName = "libnvrtc.so.12";
#endif

struct NvrtcApi {
    decltype(&::nvrtcGetErrorString) getErrorString = nullptr;
    decltype(&::nvrtcCreateProgram) createProgram = nullptr;
    decltype(&::nvrtcCompileProgram) compileProgram = nullptr;
    decltype(&::nvrtcGetProgramLogSize) getProgramLogSize = nullptr;
    decltype(&::nvrtcGetProgramLog) getProgramLog = nullptr;
    decltype(&::nvrtcGetPTXSize) getPtxSize = nullptr;
    decltype(&::nvrtcGetPTX) getPtx = nullptr;
    decltype(&::nvrtcGetCUBINSize) getCubinSize = nullptr;
    decltype(&::nvrtcGetCUBIN) getCubin = nullptr;
    decltype(&::nvrtcDestroyProgram) destroyProgram = nullptr;
};

template <typename Function>
bool loadSymbol(void* library, const char* name, Function& target) {
#if defined(_WIN32)
    target = reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name)));
#else
    target = reinterpret_cast<Function>(dlsym(library, name));
#endif
    return target != nullptr;
}

bool loadNvrtc(NvrtcApi& api) {
#if defined(_WIN32)
    void* library = LoadLibraryA(nvrtcLibraryName);
#else
    void* library = dlopen(nvrtcLibraryName, RTLD_NOW | RTLD_LOCAL);
#endif
    if (!library) {
#if defined(_WIN32)
        std::cerr << "cannot load " << nvrtcLibraryName << " (run scripts/FetchCudaSdk.ps1)\n";
#else
        const char* reason = dlerror();
        std::cerr << "cannot load " << nvrtcLibraryName << ": " << (reason ? reason : "unknown error")
                  << " (run scripts/FetchCudaSdk.sh)\n";
#endif
        return false;
    }
    return loadSymbol(library, "nvrtcGetErrorString", api.getErrorString) &&
           loadSymbol(library, "nvrtcCreateProgram", api.createProgram) &&
           loadSymbol(library, "nvrtcCompileProgram", api.compileProgram) &&
           loadSymbol(library, "nvrtcGetProgramLogSize", api.getProgramLogSize) &&
           loadSymbol(library, "nvrtcGetProgramLog", api.getProgramLog) &&
           loadSymbol(library, "nvrtcGetPTXSize", api.getPtxSize) &&
           loadSymbol(library, "nvrtcGetPTX", api.getPtx) &&
           loadSymbol(library, "nvrtcGetCUBINSize", api.getCubinSize) &&
           loadSymbol(library, "nvrtcGetCUBIN", api.getCubin) &&
           loadSymbol(library, "nvrtcDestroyProgram", api.destroyProgram);
}

NvrtcApi nvrtc;

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

bool check(nvrtcResult result, const char* what) {
    if (result != NVRTC_SUCCESS) {
        std::cerr << what << ": " << nvrtc.getErrorString(result) << '\n';
        return false;
    }
    return true;
}

std::string arrayName(const KernelImage& image) {
    return std::string(image.binary ? "ballKernelsSm" : "ballKernelsCompute") + std::to_string(image.computeMajor) +
           std::to_string(image.computeMinor);
}

std::string architectureOption(const KernelImage& image) {
    return std::string(image.binary ? "--gpu-architecture=sm_" : "--gpu-architecture=compute_") +
           std::to_string(image.computeMajor) + std::to_string(image.computeMinor);
}

std::string compiledImage(nvrtcProgram program, const KernelImage& image) {
    size_t size = 0;
    std::string bytes;
    if (image.binary) {
        nvrtc.getCubinSize(program, &size);
        bytes.resize(size);
        nvrtc.getCubin(program, bytes.data());
        return bytes;
    }
    nvrtc.getPtxSize(program, &size);
    bytes.resize(size);
    nvrtc.getPtx(program, bytes.data());
    if (!bytes.empty() && bytes.back() == '\0') {
        bytes.pop_back();
    }
    return bytes;
}

void writeImageTable(std::ofstream& output, const std::vector<KernelImage>& images) {
    output << "\nstruct BallKernelImage {\n    int computeMajor;\n    int computeMinor;\n    bool binary;\n"
              "    const unsigned char* bytes;\n};\n\ninline constexpr BallKernelImage ballKernelImages[] = {\n";
    for (const KernelImage& image : images) {
        output << "    {" << image.computeMajor << ", " << image.computeMinor << ", "
               << (image.binary ? "true" : "false") << ", " << arrayName(image) << "},\n";
    }
    output << "};\n";
}

void writeByteArray(std::ofstream& output, const std::string& name, const std::string& ptx) {
    output << "\ninline constexpr unsigned char " << name << "[] = {\n";
    for (size_t i = 0; i < ptx.size(); ++i) {
        output << static_cast<unsigned>(static_cast<unsigned char>(ptx[i])) << ',';
        if (i % 32 == 31) {
            output << '\n';
        }
    }
    output << "0};\n";
}

}

int main(int argumentCount, char** arguments) {
    int argIndex = 1;
    std::optional<int> onlySm;
    if (argIndex < argumentCount && std::string_view(arguments[argIndex]).starts_with("--only-sm=")) {
        const std::string_view arg(arguments[argIndex]);
        const std::string_view smStr = arg.substr(10);
        onlySm = std::stoi(std::string(smStr));
        ++argIndex;
    }

    if (argumentCount - argIndex < 2) {
        std::cerr << "usage: balls_ptxgen [--only-sm=NN] <output.h> <header>... <kernel.cu>\n";
        return 2;
    }
    if (!loadNvrtc(nvrtc)) {
        return 1;
    }
    const std::filesystem::path outputPath = arguments[argIndex];
    const std::filesystem::path kernelPath = arguments[argumentCount - 1];

    std::vector<KernelImage> activeImages;
    if (onlySm) {
        activeImages.push_back({*onlySm / 10, *onlySm % 10, true});
    } else {
        activeImages.assign(std::begin(defaultKernelImages), std::end(defaultKernelImages));
    }

    std::vector<std::string> headerContents;
    std::vector<std::string> headerNames;
    for (int i = argIndex + 1; i < argumentCount - 1; ++i) {
        const std::filesystem::path headerPath = arguments[i];
        headerContents.push_back(readFile(headerPath));
        headerNames.push_back(headerPath.filename().string());
    }
    std::vector<const char*> headerContentPointers;
    std::vector<const char*> headerNamePointers;
    for (size_t i = 0; i < headerContents.size(); ++i) {
        headerContentPointers.push_back(headerContents[i].c_str());
        headerNamePointers.push_back(headerNames[i].c_str());
    }

    const std::string kernelSource = readFile(kernelPath);
    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    output << "#pragma once\n";
    for (const KernelImage& image : activeImages) {
        nvrtcProgram program{};
        if (!check(nvrtc.createProgram(&program, kernelSource.c_str(), kernelPath.filename().string().c_str(),
                                      static_cast<int>(headerContents.size()), headerContentPointers.data(),
                                      headerNamePointers.data()),
                   "nvrtcCreateProgram")) {
            return 1;
        }
        const std::string architecture = architectureOption(image);
        const char* rankDefine =
            image.computeMajor >= warpMatchComputeMajor ? "-DBALLS_RANK_WITH_MATCH" : "-DBALLS_RANK_WITH_ATOMICS";
        const char* options[] = {architecture.c_str(), rankDefine, "--std=c++17", "--use_fast_math",
                                 "-default-device"};
        const nvrtcResult compileResult = nvrtc.compileProgram(program, static_cast<int>(std::size(options)), options);
        size_t logSize = 0;
        nvrtc.getProgramLogSize(program, &logSize);
        if (logSize > 1) {
            std::string log(logSize, '\0');
            nvrtc.getProgramLog(program, log.data());
            std::cerr << log << '\n';
        }
        if (!check(compileResult, "nvrtcCompileProgram")) {
            return 1;
        }
        const std::string bytes = compiledImage(program, image);
        nvrtc.destroyProgram(&program);
        writeByteArray(output, arrayName(image), bytes);
    }
    writeImageTable(output, activeImages);
    return 0;
}
