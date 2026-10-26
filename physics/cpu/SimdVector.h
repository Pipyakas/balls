#pragma once

#include <cstddef>
#include <new>
#include <vector>

template <typename T, size_t Alignment>
struct AlignedAllocator {
    using value_type = T;

    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() = default;
    template <typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) {}

    T* allocate(size_t count) {
        return static_cast<T*>(::operator new(count * sizeof(T), std::align_val_t(Alignment)));
    }
    void deallocate(T* pointer, size_t) { ::operator delete(pointer, std::align_val_t(Alignment)); }

    template <typename U>
    bool operator==(const AlignedAllocator<U, Alignment>&) const {
        return true;
    }
};

inline constexpr size_t simdAlignment = 64;
inline constexpr size_t simdLaneCount = 8;

template <typename T>
using SimdVector = std::vector<T, AlignedAllocator<T, simdAlignment>>;
