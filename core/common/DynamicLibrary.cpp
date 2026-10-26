#include "common/DynamicLibrary.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

DynamicLibrary::DynamicLibrary(const char* fileName) {
#if defined(_WIN32)
    handle = LoadLibraryA(fileName);
#else
    handle = dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
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
