#pragma once

#include "sim/PhysicsBackend.h"
#include "cuda/CudaDriver.h"

#include <array>
#include <string>
#include <vector>

class CudaBackend final : public PhysicsBackend {
public:
    CudaBackend();
    ~CudaBackend() override;
    CudaBackend(const CudaBackend&) = delete;
    CudaBackend& operator=(const CudaBackend&) = delete;

    static std::string unavailableReason();
    static GpuRoofline measureRoofline(size_t bufferBytes);
    static size_t streamedBytesPerBall(bool gridActive);

    std::string_view name() const override { return "CUDA"; }
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t> descendingIndices) override;
    size_t ballCount() const override { return count; }
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    void clear() override { count = 0; }
    BallTally truncate(size_t newCount) override;
    std::string_view faultMessage() const override { return fault; }
    bool resolvesHitsOnDevice() const override { return true; }
    DeviceHitResults deviceHitResults() const override;
    bool shareRenderBuffer(unsigned int openGlBuffer, size_t capacityBytes) override;
    void setVisibilityGrid(int width, int height) override;
    double deviceStepMs() const override { return lastStepKernelMs; }

private:
    struct DeviceBalls {
        CUdeviceptr position;
        CUdeviceptr state;
        CUdeviceptr packed;
    };

    struct DeviceArray {
        CUdeviceptr pointer = 0;
        size_t bytes = 0;
    };

    struct FrameCounters {
        unsigned int killedCount;
        unsigned int holeCount;
        unsigned int holeCursor;
        std::array<unsigned int, ballColorCount> killsByColor;
    };

    bool succeeded(CUresult result, const char* what);
    bool isFaulted() const { return !fault.empty(); }
    bool launch(CUfunction function, size_t threads, void** arguments, const char* what);
    void ensureBallCapacity(size_t required);
    void ensurePackedCapacity(size_t required);
    void ensureArray(DeviceArray& array, size_t bytes);
    void upload(DeviceArray& array, const void* data, size_t bytes);
    void compactKilledBalls(unsigned int killed);
    void releaseRenderBuffer();
    bool copyIntoRenderBuffer(CUdeviceptr source, size_t elements);
    bool writeGridIntoRenderBuffer();
    BallTally downloadColorTally(size_t first, size_t last);
    size_t gridCellCount() const;
    void downloadPacked();
    bool downloadGrid();

    const CudaDriver& driver;
    CUfunction stepFunction = nullptr;
    CUfunction collectHolesFunction = nullptr;
    CUfunction fillHolesFunction = nullptr;
    CUfunction gridToRenderFunction = nullptr;
    DeviceBalls balls{};
    size_t capacity = 0;
    size_t packedCapacity = 0;
    size_t count = 0;
    DeviceArray paddedCells;
    DeviceArray brickColors;
    DeviceArray targetCells;
    DeviceArray cellWords;
    DeviceArray brickHitsLeft;
    DeviceArray killedIndices;
    DeviceArray holes;
    DeviceArray counters;
    DeviceArray gridSlots;
    DeviceArray gridPacked;
    unsigned int frameSeed = 0;
    int gridWidth = 0;
    int gridHeight = 0;
    bool gridFilled = false;
    size_t brickCount = 0;
    unsigned int killedCapacity = 0;
    float ballSpeed = 1.0f;
    std::vector<int32_t> hostBrickHitsLeft;
    FrameCounters lastCounters{};
    std::vector<uint32_t> hostPacked;
    CUgraphicsResource renderResource = nullptr;
    size_t renderCapacityBytes = 0;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    bool snapshotFresh = false;
    std::string fault;
    CUevent stepStarted = nullptr;
    CUevent stepFinished = nullptr;
    double lastStepKernelMs = 0.0;
};
