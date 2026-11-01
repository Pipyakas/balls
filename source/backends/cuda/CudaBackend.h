#pragma once

#include "sim/PhysicsBackend.h"
#include "sim/SortSchedule.h"
#include "cuda/CudaDriver.h"
#include "cuda/GrowableDeviceArray.h"
#include "common/WorkerPool.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

class CudaBackend final : public PhysicsBackend {
public:
    explicit CudaBackend(int deviceOrdinal = 0);
    ~CudaBackend() override;
    CudaBackend(const CudaBackend&) = delete;
    CudaBackend& operator=(const CudaBackend&) = delete;

    static std::string unavailableReason(int deviceOrdinal = 0);
    static GpuRoofline measureRoofline(size_t bufferBytes);
    static size_t streamedBytesPerBall(bool gridActive);

    std::string_view name() const override { return "CUDA"; }
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t> descendingIndices) override;
    size_t ballCount() const override { return count; }
    size_t ballCapacityLimit() const override;
    size_t preallocateBalls() override;
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    std::vector<BallSpawn> takeTail(size_t count) override;
    bool takeCompactTail(size_t count, CompactBalls& balls) override;
    bool addCompact(const CompactBalls& balls) override;
    void clear() override {
        count = 0;
        frozen = false;
    }
    BallTally truncate(size_t newCount) override;
    std::string_view faultMessage() const override { return fault; }
    bool resolvesHitsOnDevice() const override { return true; }
    bool acceptsSparseBoardUpdates() const override { return true; }
    void setDrawing(bool enabled) override { drawing = enabled; }
    DeviceHitResults deviceHitResults() const override;
    void uploadBrickHits(std::span<const int32_t> hitsLeft) override;
    bool shareRenderBuffer(unsigned int openGlBuffer, size_t capacityBytes) override;
    void setVisibilityGrid(int width, int height) override;
    double deviceStepMs() const override { return lastStepKernelMs; }

private:
    struct DeviceVisibilityGridArgument {
        CUdeviceptr slots;
        int width;
        int height;
        float scaleX;
        float scaleY;
        unsigned int frameSeed;
        unsigned int bidderMask;
    };

    std::vector<BallSpawn> exportRange(size_t first);
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
    bool ensureBallCapacity(size_t required);
    static size_t preallocatedBallsFor(size_t freeBytes);
    void freeBallArrays();
    bool allocateFixedBallArrays(size_t ballCapacity);
    static size_t boardBytes(const BoardUpload& board);
    void releaseArray(DeviceArray& array);
    void uploadTargets(std::span<const int32_t> targets);
    void releaseTargets();
    CUdeviceptr targetPointer() const;
    void carveBallArraysAfterBoard(size_t boardArrayBytes);
    bool growThroughHost(size_t wanted);
    bool reserveGrowableArrays();
    uint32_t* uploadStaging(size_t balls);
    bool growInPlaceTo(size_t required);
    void ensurePackedCapacity(size_t required);
    void ensureArray(DeviceArray& array, size_t bytes);
    void upload(DeviceArray& array, const void* data, size_t bytes);
    void compactKilledBalls(unsigned int killed);
    void uploadWholeBoard(const BoardUpload& board);
    void applyChangedCellWords(const BoardUpload& board);
    bool scatterUpdates(std::span<const int32_t> pairs, CUdeviceptr target, DeviceArray& staging);
    void releaseRenderBuffer();
    bool copyIntoRenderBuffer(CUdeviceptr source, size_t elements);
    bool writeGridIntoRenderBuffer();
    BallTally downloadColorTally(size_t first, size_t last);
    size_t gridCellCount() const;
    void downloadPacked();
    bool downloadGrid();
    bool ensureSortBuffers();
    void releaseSortBuffers();
    bool countBallsByCell();
    void chooseSortGrid();
    bool prepareVisibilityGrid(DeviceVisibilityGridArgument& grid);
    void renderFreeFlight(const StepParams& params);
    void materializeFreeFlight();
    BallSpawn projectToEpoch(const BallSpawn& spawn) const;

    const CudaDriver& driver;
    CUfunction stepFunction = nullptr;
    CUfunction collectHolesFunction = nullptr;
    CUfunction fillHolesFunction = nullptr;
    CUfunction gridToRenderFunction = nullptr;
    CUfunction countCellsFunction = nullptr;
    CUfunction scanCellsFunction = nullptr;
    CUfunction stepAndSortFunction = nullptr;
    CUfunction applyCellWordsFunction = nullptr;
    CUfunction freeFlightRenderFunction = nullptr;
    CUfunction materializeFreeFlightFunction = nullptr;
    DeviceBalls balls{};
    size_t capacity = 0;
    size_t packedCapacity = 0;
    size_t count = 0;
    bool fixedCapacity = false;

    size_t preallocatedCapacity = 0;
    DeviceArray targetCells;

    void* hostTargetCells = nullptr;
    size_t hostTargetBytes = 0;
    DeviceArray cellWords;
    DeviceArray brickHitsLeft;
    DeviceArray killedIndices;
    DeviceArray holes;
    DeviceArray counters;
    DeviceArray gridSlots;
    DeviceArray gridPacked;
    DeviceArray cellCursors;
    DeviceArray cellWordUpdates;
    DeviceArray brickHitUpdates;
    DeviceArray targetUpdates;
    size_t uploadedCellWords = 0;
    CUdeviceptr sortedPosition = 0;
    CUdeviceptr sortedState = 0;
    size_t sortedCapacity = 0;

    bool growInPlace = false;
    bool drawing = true;

    uint32_t* staging = nullptr;
    std::unique_ptr<WorkerPool> encodePool;
    size_t stagingBalls = 0;
    CUevent stagingRead = nullptr;
    GrowableDeviceArray positionArray;
    GrowableDeviceArray stateArray;
    GrowableDeviceArray sortedPositionArray;
    GrowableDeviceArray sortedStateArray;
    SortSchedule sortSchedule;
    int sortColumns = 1;
    int sortRows = 1;
    unsigned int frameSeed = 0;
    int gridWidth = 0;
    int gridHeight = 0;
    bool gridFilled = false;
    size_t brickCount = 0;
    unsigned int killedCapacity = 0;
    float ballSpeed = 1.0f;
    std::vector<int32_t> hostBrickHitsLeft;
    std::vector<uint32_t> hostHitBricks;
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

    bool frozen = false;
    unsigned int freeFlightEpoch = 0;
    unsigned int clockTick = 0;
    StepParams lastParams{};
};
