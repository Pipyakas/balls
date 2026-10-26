#pragma once

class DynamicLibrary {
public:
    explicit DynamicLibrary(const char* fileName);
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    bool isLoaded() const { return handle != nullptr; }

    template <typename Function>
    bool resolve(const char* symbolName, Function& target) const {
        target = reinterpret_cast<Function>(findSymbol(symbolName));
        return target != nullptr;
    }

private:
    void* findSymbol(const char* symbolName) const;

    void* handle = nullptr;
};
