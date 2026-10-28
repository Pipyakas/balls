#pragma once

#include "sim/PhysicsBackend.h"
#include "sim/SortSchedule.h"
#include "rocm/RocmDriver.h"

#include <array>
#include <string>
#include <vector>

class RocmBackend final : public PhysicsBackend {
public:
    explicit RocmBackend(int deviceOrdinal = 0);
    ~RocmBackend() override;
    RocmBackend(const RocmBackend&) = delete;
    RocmBackend& operator=(const RocmBackend&) = delete;

    static std::string unavailableReason(int deviceOrdinal = 0);
    static size_t streamedBytesPerBall(bool gridActive);

    std::string_view name() const override { return "ROCm"; }
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t> descendingIndices) override;
    size_t ballCount() const override { return count; }
    size_t ballCapacityLimit() const override;
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
    DeviceHitResults deviceHitResults() const override;
    void uploadBrickHits(std::span<const int32_t> hitsLeft) override;
    bool shareRenderBuffer(unsigned int openGlBuffer, size_t capacityBytes) override { return false; }
    void setVisibilityGrid(int width, int height) override;
    double deviceStepMs() const override { return lastStepKernelMs; }
    bool supportsBallCollisions() const override { return false; }

private:
    struct DeviceVisibilityGridArgument {
        void* slots = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        float scaleX = 0.0f;
        float scaleY = 0.0f;
        uint32_t frameSeed = 0;
        uint32_t bidderMask = 0;
    };

    struct DeviceBalls {
        void* position = nullptr;
        void* state = nullptr;
        void* packed = nullptr;
    };

    struct DeviceArray {
        void* pointer = nullptr;
        size_t bytes = 0;
    };

    struct FrameCounters {
        uint32_t killedCount = 0;
        uint32_t holeCount = 0;
        uint32_t holeCursor = 0;
        std::array<uint32_t, ballColorCount> killsByColor{};
    };

    struct KernelSymbol {
        uint64_t kernelObject = 0;
        uint32_t kernargSize = 0;
        uint32_t groupSize = 0;
        uint32_t privateSize = 0;
    };

    struct DeviceSortedOutput {
        void* cellCursors = nullptr;
        void* position = nullptr;
        void* state = nullptr;
        int32_t columns = 0;
        int32_t rows = 0;
    };

    bool succeeded(hsa_status_t status, const char* what);
    bool isFaulted() const { return !fault.empty(); }

    void* allocateVram(size_t bytes);
    void freeVram(void* ptr);
    void ensureArray(DeviceArray& array, size_t bytes);
    void ensureStagingCapacity(size_t bytes);
    void uploadToDevice(void* devDst, const void* hostSrc, size_t bytes);
    void downloadFromDevice(void* hostDst, const void* devSrc, size_t bytes);
    void upload(DeviceArray& array, const void* data, size_t bytes);
    void download(void* dest, const void* src, size_t bytes);
    void copyDeviceToDevice(void* dst, const void* src, size_t bytes);
    void fillDeviceMemory(void* ptr, uint32_t value, size_t dwords);

    void* getKernargSlot(size_t size);
    void resetKernargRing();
    uint64_t queuePacket(const KernelSymbol& kernel, uint32_t workgroupSize,
                         uint32_t gridSize, const void* args, size_t argsSize,
                         bool barrier, hsa_signal_t signal);
    bool launch(const KernelSymbol& kernel, uint32_t workgroupSize,
                uint32_t totalThreads, const void* args, size_t argsSize, const char* what);

    bool ensureBallCapacity(size_t required);
    bool growThroughHost(size_t wanted);
    void ensurePackedCapacity(size_t required);
    void compactKilledBalls(uint32_t killed);
    void uploadWholeBoard(const BoardUpload& board);
    void applyChangedCellWords(const BoardUpload& board);
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
    std::vector<BallSpawn> exportRange(size_t first);

    const RocmDriver& driver;
    hsa_queue_t* queue = nullptr;
    hsa_executable_t executable{};
    hsa_signal_t stepSignal{};
    hsa_signal_t countersSignal{};
    void* hostCountersBuffer = nullptr;
    bool countersDirty = true;

    KernelSymbol stepKernel;
    KernelSymbol collectHolesKernel;
    KernelSymbol fillHolesKernel;
    KernelSymbol gridToRenderKernel;
    KernelSymbol countCellsKernel;
    KernelSymbol scanCellsKernel;
    KernelSymbol stepAndSortKernel;
    KernelSymbol applyCellWordsKernel;
    KernelSymbol freeFlightRenderKernel;
    KernelSymbol materializeFreeFlightKernel;

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
    DeviceArray cellCursors;
    DeviceArray cellWordUpdates;
    size_t uploadedCellWords = 0;

    void* sortedPosition = nullptr;
    void* sortedState = nullptr;
    size_t sortedCapacity = 0;

    void* kernargRingBuffer = nullptr;
    size_t kernargRingSize = 0;
    size_t kernargRingOffset = 0;

    void* stagingBuffer = nullptr;
    size_t stagingCapacity = 0;

    SortSchedule sortSchedule;
    int sortColumns = 1;
    int sortRows = 1;
    uint32_t frameSeed = 0;
    int gridWidth = 0;
    int gridHeight = 0;
    bool gridFilled = false;
    size_t brickCount = 0;
    uint32_t killedCapacity = 0;
    float ballSpeed = 1.0f;
    std::vector<int32_t> hostBrickHitsLeft;
    std::vector<uint32_t> hostHitBricks;
    FrameCounters lastCounters{};
    std::vector<uint32_t> hostPacked;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    bool snapshotFresh = false;
    std::string fault;
    double lastStepKernelMs = 0.0;
    bool frozen = false;
    uint32_t freeFlightEpoch = 0;
    uint32_t clockTick = 0;
    StepParams lastParams{};
};
