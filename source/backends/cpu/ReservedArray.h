#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// An array that never moves: it reserves address space for the whole of physical memory once and commits pages as
// it grows. Growing a std::vector copies into a new block while the old one still exists, which near the memory
// ceiling is exactly the moment that pushes the process into swap; here growth costs only the new pages.
template <typename T>
class ReservedArray {
public:
    ReservedArray() = default;
    ReservedArray(const ReservedArray&) = delete;
    ReservedArray& operator=(const ReservedArray&) = delete;
    ~ReservedArray() { releaseAddressSpace(); }

    T* data() { return elements; }
    const T* data() const { return elements; }
    size_t size() const { return length; }
    size_t committedBytes() const { return committed; }
    T& operator[](size_t index) { return elements[index]; }
    const T& operator[](size_t index) const { return elements[index]; }
    T* begin() { return elements; }
    T* end() { return elements + length; }
    const T* begin() const { return elements; }
    const T* end() const { return elements + length; }

    // New elements read as zero. Returns false, leaving the size unchanged, when the pages cannot be committed.
    bool resize(size_t count) {
        const size_t bytes = count * sizeof(T);
        if (bytes > committed) {
            if (!reserveAddressSpace(bytes)) {
                return false;
            }
            const size_t target = std::min(roundUp(bytes, commitGranule), reserved);
            if (!commit(committed, target - committed)) {
                return false;
            }
            committed = target;
        }
        if (count < length) {
            std::fill(elements + count, elements + length, T{});
        }
        length = count;
        if (count == 0) {
            decommitAll();
        }
        return true;
    }

private:
    static constexpr size_t commitGranule = size_t{64} << 20;

    static size_t roundUp(size_t value, size_t granule) { return (value + granule - 1) / granule * granule; }

    static size_t physicalMemoryBytes() {
#if defined(_WIN32)
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        return GlobalMemoryStatusEx(&memory) ? static_cast<size_t>(memory.ullTotalPhys) : size_t{0};
#else
        const long pageSize = sysconf(_SC_PAGESIZE);
        const long pages = sysconf(_SC_PHYS_PAGES);
        return pageSize > 0 && pages > 0 ? static_cast<size_t>(pageSize) * static_cast<size_t>(pages) : size_t{0};
#endif
    }

    bool reserveAddressSpace(size_t needed) {
        if (elements) {
            return needed <= reserved;
        }
        reserved = roundUp(std::max(needed, physicalMemoryBytes()), commitGranule);
#if defined(_WIN32)
        elements = static_cast<T*>(VirtualAlloc(nullptr, reserved, MEM_RESERVE, PAGE_NOACCESS));
#else
        void* address = mmap(nullptr, reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        elements = address == MAP_FAILED ? nullptr : static_cast<T*>(address);
#endif
        if (!elements) {
            reserved = 0;
        }
        return elements != nullptr && needed <= reserved;
    }

    bool commit(size_t offset, size_t bytes) {
        if (bytes == 0) {
            return true;
        }
        char* start = reinterpret_cast<char*>(elements) + offset;
#if defined(_WIN32)
        return VirtualAlloc(start, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
        return mprotect(start, bytes, PROT_READ | PROT_WRITE) == 0;
#endif
    }

    void decommitAll() {
        if (!elements || committed == 0) {
            return;
        }
#if defined(_WIN32)
        VirtualFree(elements, committed, MEM_DECOMMIT);
#else
        madvise(elements, committed, MADV_DONTNEED);
        mprotect(elements, committed, PROT_NONE);
#endif
        committed = 0;
    }

    void releaseAddressSpace() {
        if (!elements) {
            return;
        }
#if defined(_WIN32)
        VirtualFree(elements, 0, MEM_RELEASE);
#else
        munmap(elements, reserved);
#endif
        elements = nullptr;
        reserved = 0;
        committed = 0;
        length = 0;
    }

    T* elements = nullptr;
    size_t length = 0;
    size_t committed = 0;
    size_t reserved = 0;
};
