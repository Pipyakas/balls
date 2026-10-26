#include "common/DynamicLibrary.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <array>

DynamicLibrary::DynamicLibrary(const std::filesystem::path& fileName) {
#if defined(_WIN32)
    handle = LoadLibraryW(fileName.c_str());
#else
    handle = dlopen(fileName.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

DynamicLibrary::~DynamicLibrary() {
    if (!handle) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* DynamicLibrary::findSymbol(const char* symbolName) const {
    if (!handle) {
        return nullptr;
    }
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbolName));
#else
    return dlsym(handle, symbolName);
#endif
}

std::filesystem::path DynamicLibrary::executableDirectory() {
#if defined(_WIN32)
    std::array<wchar_t, MAX_PATH> buffer{};
    GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(buffer.data()).parent_path();
#else
    std::array<char, 4096> buffer{};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    return length > 0 ? std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length))).parent_path()
                      : std::filesystem::current_path();
#endif
}
