#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>

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
//
// The next granule past the elements in use is committed early and its pages faulted in on a background thread.
// Within the last few gigabytes of free memory Windows can hold a faulting thread for up to a second while it trims
// working sets; that thread is then this one, not the frame adding balls.
template <typename T>
class ReservedArray {
public:
    ReservedArray() = default;
    ReservedArray(const ReservedArray&) = delete;
    ReservedArray& operator=(const ReservedArray&) = delete;
    ~ReservedArray() {
        settlePrefault();
        releaseAddressSpace();
    }

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
        if (prefault.valid() && bytes > prefaultBegin) {
            settlePrefault();
        }
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
            settlePrefault();
            decommitAll();
            return true;
        }
        prefaultAhead(bytes);
        return true;
    }

private:
    static constexpr size_t commitGranule = size_t{64} << 20;

    static constexpr size_t pageBytes = 4096;

    static size_t roundUp(size_t value, size_t granule) { return (value + granule - 1) / granule * granule; }

    void settlePrefault() {
        if (prefault.valid()) {
            prefault.get();
        }
    }

    void prefaultAhead(size_t usedBytes) {
        if (prefault.valid()) {
            if (prefault.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }
            prefault.get();
        }
        if (committed >= usedBytes + commitGranule || committed + commitGranule > reserved ||
            !commit(committed, commitGranule)) {
            return;
        }
        prefaultBegin = committed;
        committed += commitGranule;
        volatile char* const first = reinterpret_cast<char*>(elements) + prefaultBegin;
        prefault = std::async(std::launch::async, [first] {
            for (size_t offset = 0; offset < commitGranule; offset += pageBytes) {
                first[offset] = 0;
            }
        });
    }

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
    std::future<void> prefault;
    size_t prefaultBegin = 0;
    size_t length = 0;
    size_t committed = 0;
    size_t reserved = 0;
};
