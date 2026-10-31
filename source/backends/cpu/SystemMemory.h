#pragma once

#include <cstddef>
#include <optional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

// What the CPU backends ask the system about memory, to size the balls they take.
namespace system_memory {

struct PhysicalMemory {
    size_t total = 0;
    size_t available = 0;
};

inline bool isMemoryLow() {
#if defined(_WIN32)
    static const HANDLE lowMemory = CreateMemoryResourceNotification(LowMemoryResourceNotification);
    BOOL low = FALSE;
    return lowMemory && QueryMemoryResourceNotification(lowMemory, &low) && low;
#else
    return false;
#endif
}

inline std::optional<PhysicalMemory> physicalMemory() {
#if defined(_WIN32)
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) {
        return std::nullopt;
    }
    return PhysicalMemory{static_cast<size_t>(memory.ullTotalPhys), static_cast<size_t>(memory.ullAvailPhys)};
#else
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long availablePages = sysconf(_SC_AVPHYS_PAGES);
    if (pageSize <= 0 || pages <= 0 || availablePages < 0) {
        return std::nullopt;
    }
    return PhysicalMemory{static_cast<size_t>(pages) * static_cast<size_t>(pageSize),
                          static_cast<size_t>(availablePages) * static_cast<size_t>(pageSize)};
#endif
}

// Memory the process can still commit, paging included: Windows' commit limit left (RAM plus page file), or free RAM
// plus free swap elsewhere.
inline std::optional<size_t> committableBytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) {
        return std::nullopt;
    }
    return static_cast<size_t>(memory.ullAvailPageFile);
#else
    struct sysinfo info{};
    if (sysinfo(&info) != 0) {
        return std::nullopt;
    }
    const std::optional<PhysicalMemory> memory = physicalMemory();
    const size_t ram = memory ? memory->available : static_cast<size_t>(info.freeram) * info.mem_unit;
    return ram + static_cast<size_t>(info.freeswap) * info.mem_unit;
#endif
}

} // namespace system_memory
