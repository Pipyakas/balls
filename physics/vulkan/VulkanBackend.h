#pragma once

#include "sim/PhysicsBackend.h"
#include "sim/SortSchedule.h"
#include "vulkan/VulkanComputeDevice.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

class VulkanBackend final : public PhysicsBackend {
public:
    explicit VulkanBackend(std::shared_ptr<VulkanComputeDevice> computeDevice);
    ~VulkanBackend() override = default;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    std::string_view name() const override { return "Vulkan"; }
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override;
    void step(const StepParams& params, std::vector<BallHit>& hits) override;
    void removeBalls(std::span<const uint32_t>) override {}
    size_t ballCount() const override { return count; }
    BallSnapshot snapshot() override;
    std::vector<BallSpawn> exportBalls() override;
    std::vector<BallSpawn> takeTail(size_t count) override;
    bool takeCompactTail(size_t count, CompactBalls& balls) override;
    bool addCompact(const CompactBalls& balls) override;
    void clear() override;
    BallTally truncate(size_t newCount) override;
    std::string_view faultMessage() const override { return fault; }
    bool resolvesHitsOnDevice() const override { return true; }
    DeviceHitResults deviceHitResults() const override;
    void setVisibilityGrid(int, int) override {}
    double deviceStepMs() const override { return lastStepKernelMs; }

private:
    struct StepConstants {
        StepParams params;
        uint32_t ballCount;
        uint32_t killedCapacity;
        uint32_t groupsPerRow;
        uint32_t packedCapacity;
        uint32_t toSorted;
        int32_t sortColumns;
        int32_t sortRows;
        uint32_t padding0;
        uint32_t padding1;
    };

    struct CompactionConstants {
        uint32_t killedCount;
        uint32_t survivorCount;
        uint32_t oldCount;
        uint32_t packedCapacity;
    };

    bool isFaulted() const { return !fault.empty(); }
    bool initialize();
    void setFault(std::string message);
    nvrhi::BufferHandle structuredBuffer(size_t elements, bool writable, const char* name);
    nvrhi::BufferHandle constantBuffer(size_t bytes, const char* name);
    void ensureBallCapacity(size_t required);
    void ensureReadback(nvrhi::BufferHandle& buffer, size_t bytes, const char* name);
    void rebuildBindingSets();
    bool readBuffer(nvrhi::IBuffer* source, nvrhi::IBuffer* staging, void* destination, size_t bytes);
    bool readBallWords(size_t first, size_t words, bool positions, bool states, std::vector<uint32_t>& firstWords,
                       std::vector<uint32_t>& secondWords);
    void execute(nvrhi::CommandListHandle& commands);
    void dispatch(nvrhi::ICommandList* commands, nvrhi::IComputePipeline* pipeline, nvrhi::IBindingSet* bindings,
                  uint32_t groupsX, uint32_t groupsY);
    void compactKilledBalls(uint32_t killed);
    std::vector<BallSpawn> exportRange(size_t first);
    BallTally colorTally(size_t first, size_t last);

    std::shared_ptr<VulkanComputeDevice> computeDevice;
    nvrhi::IDevice* device = nullptr;
    BallStepTuning tuning;
    nvrhi::BindingLayoutHandle stepLayout;
    nvrhi::BindingLayoutHandle compactionLayout;
    nvrhi::ComputePipelineHandle stepPipeline;
    nvrhi::ComputePipelineHandle countPipeline;
    nvrhi::ComputePipelineHandle scanPipeline;
    nvrhi::ComputePipelineHandle sortPipeline;
    nvrhi::ComputePipelineHandle collectHolesPipeline;
    nvrhi::ComputePipelineHandle fillHolesPipeline;
    std::array<nvrhi::BindingSetHandle, 2> stepBindings;
    std::array<nvrhi::BindingSetHandle, 2> countBindings;
    std::array<nvrhi::BindingSetHandle, 2> sortBindings;
    std::array<nvrhi::BindingSetHandle, 2> compactionBindings;
    nvrhi::CommandListHandle commands;
    nvrhi::TimerQueryHandle timer;
    nvrhi::BufferHandle positions[2];
    nvrhi::BufferHandle states[2];
    nvrhi::BufferHandle packedBalls;
    nvrhi::BufferHandle cellWords;
    nvrhi::BufferHandle targetCells;
    nvrhi::BufferHandle brickHitsLeft;
    nvrhi::BufferHandle killedIndices;
    nvrhi::BufferHandle holes;
    nvrhi::BufferHandle counters;
    nvrhi::BufferHandle cellCursors;
    nvrhi::BufferHandle stepConstants;
    nvrhi::BufferHandle countConstants;
    nvrhi::BufferHandle sortConstants;
    nvrhi::BufferHandle compactionConstants;
    nvrhi::BufferHandle countersReadback;
    nvrhi::BufferHandle packedReadback;
    nvrhi::BufferHandle hitsReadback;
    nvrhi::BufferHandle ballReadback;
    size_t capacity = 0;
    size_t count = 0;
    size_t brickCount = 0;
    uint32_t killedCapacity = 1024;
    uint32_t currentBuffer = 0;
    int sortColumns = 0;
    int sortRows = 0;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    float ballSpeed = 1.0f;
    bool snapshotFresh = false;
    std::array<uint32_t, 10> lastCounters{};
    std::vector<int32_t> hostBrickHitsLeft;
    std::vector<uint32_t> hostPacked;
    SortSchedule sortSchedule;
    std::string fault;
    double lastStepKernelMs = 0.0;
};
