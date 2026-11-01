#pragma once

#include <filesystem>

class DynamicLibrary {
public:
    explicit DynamicLibrary(const std::filesystem::path& fileName);
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    bool isLoaded() const { return handle != nullptr; }

    template <typename Function>
    bool resolve(const char* symbolName, Function& target) const {
        target = reinterpret_cast<Function>(findSymbol(symbolName));
        return target != nullptr;
    }

    static std::filesystem::path executableDirectory();

private:
    void* findSymbol(const char* symbolName) const;

    void* handle = nullptr;
};
