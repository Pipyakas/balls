#pragma once

#include "sim/PhysicsBackend.h"

#include <cstdlib>
#include <memory>
#include <new>

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

struct CpuBallStore {
    SimdVector<float> x;
    SimdVector<float> y;
    SimdVector<float> vx;
    SimdVector<float> vy;
    SimdVector<int32_t> color;
    SimdVector<int32_t> bounces;
    SimdVector<uint32_t> packed;
    size_t count = 0;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;

    void add(std::span<const BallSpawn> spawns);
    void swapRemove(std::span<const uint32_t> descendingIndices);
    void clear() { resize(0); }
    BallTally truncate(size_t newCount);
    BallSnapshot snapshot() const { return {packed.data(), count, fieldWidth, fieldHeight}; }
    std::vector<BallSpawn> exportBalls() const;

private:
    void resize(size_t newCount);
};
