#pragma once

#include "sim/PhysicsBackend.h"
#include "sim/backends/CudaDriver.h"

#include <string>
#include <vector>

class CudaBackend final : public PhysicsBackend {
public:
    CudaBackend();
    ~CudaBackend() override;
    CudaBackend(const CudaBackend&) = delete;
    CudaBackend& operator=(const CudaBackend&) = delete;

    static std::string unavailableReason();

    std::string_view name() const override { return "CUDA"; }
    void uploadBoard(const BrickBoard& board, const TargetField& field) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t> descendingIndices) override;
    size_t ballCount() const override { return count; }
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    void clear() override { count = 0; }
    std::string_view faultMessage() const override { return fault; }

private:
    struct DeviceBalls {
        CUdeviceptr x;
        CUdeviceptr y;
        CUdeviceptr vx;
        CUdeviceptr vy;
        CUdeviceptr color;
        CUdeviceptr bounces;
        CUdeviceptr packed;
    };

    struct DeviceArray {
        CUdeviceptr pointer = 0;
        size_t bytes = 0;
    };

    bool succeeded(CUresult result, const char* what);
    bool isFaulted() const { return !fault.empty(); }
    void ensureBallCapacity(size_t required);
    void ensureArray(DeviceArray& array, size_t bytes);
    void upload(DeviceArray& array, const void* data, size_t bytes);
    void downloadPositions();

    const CudaDriver& driver;
    CUfunction stepFunction = nullptr;
    CUfunction removeFunction = nullptr;
    DeviceBalls balls{};
    size_t capacity = 0;
    size_t count = 0;
    DeviceArray paddedCells;
    DeviceArray brickColors;
    DeviceArray targetCells;
    DeviceArray hitBuffer;
    DeviceArray hitCounter;
    DeviceArray removeIndices;
    unsigned int hitCapacity = 0;
    std::vector<BallHit> hostHits;
    std::vector<uint32_t> hostPacked;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    bool snapshotFresh = false;
    std::string fault;
};
