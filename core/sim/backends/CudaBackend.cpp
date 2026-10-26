#include "sim/backends/CudaBackend.h"

#include <algorithm>

namespace {

constexpr unsigned int threadsPerBlock = 256;
constexpr size_t initialBallCapacity = 1 << 14;
constexpr unsigned int initialHitCapacity = 1 << 12;

struct DeviceBoardView {
    CUdeviceptr paddedCells;
    CUdeviceptr brickColors;
    CUdeviceptr targetCells;
};

static_assert(sizeof(DeviceBoardView) == sizeof(BoardView));

}

std::string CudaBackend::unavailableReason() {
    const CudaDriver& driver = CudaDriver::instance();
    return driver.isReady() ? std::string{} : driver.error();
}

CudaBackend::CudaBackend() : driver(CudaDriver::instance()) {
    driver.makeCurrent();
    stepFunction = driver.function("stepBallsKernel");
    removeFunction = driver.function("removeBallsKernel");
    if (!stepFunction || !removeFunction) {
        fault = "CUDA kernels missing from module";
        return;
    }
    ensureBallCapacity(initialBallCapacity);
    ensureArray(hitCounter, sizeof(unsigned int));
    hitCapacity = initialHitCapacity;
    ensureArray(hitBuffer, hitCapacity * sizeof(BallHit));
}

CudaBackend::~CudaBackend() {
    driver.makeCurrent();
    const auto& api = driver.api();
    for (CUdeviceptr pointer : {balls.x, balls.y, balls.vx, balls.vy, balls.color, balls.bounces, balls.packed, paddedCells.pointer,
                                brickColors.pointer, targetCells.pointer, hitBuffer.pointer, hitCounter.pointer,
                                removeIndices.pointer}) {
        if (pointer) {
            api.memFree(pointer);
        }
    }
}

bool CudaBackend::succeeded(CUresult result, const char* what) {
    if (result == CUDA_SUCCESS) {
        return true;
    }
    if (fault.empty()) {
        fault = driver.describe(result, what);
    }
    return false;
}

void CudaBackend::ensureArray(DeviceArray& array, size_t bytes) {
    if (isFaulted() || array.bytes >= bytes) {
        return;
    }
    const auto& api = driver.api();
    if (array.pointer) {
        api.memFree(array.pointer);
    }
    const size_t newBytes = std::max(bytes, array.bytes * 2);
    array.pointer = 0;
    array.bytes = 0;
    if (succeeded(api.memAlloc(&array.pointer, newBytes), "cuMemAlloc")) {
        array.bytes = newBytes;
    }
}

void CudaBackend::upload(DeviceArray& array, const void* data, size_t bytes) {
    ensureArray(array, bytes);
    if (isFaulted()) {
        return;
    }
    succeeded(driver.api().memcpyHostToDevice(array.pointer, data, bytes), "cuMemcpyHtoD");
}

void CudaBackend::ensureBallCapacity(size_t required) {
    if (isFaulted() || required <= capacity) {
        return;
    }
    const auto& api = driver.api();
    const size_t newCapacity = std::max(required, capacity * 2);
    for (CUdeviceptr* field : {&balls.x, &balls.y, &balls.vx, &balls.vy, &balls.color, &balls.bounces, &balls.packed}) {
        CUdeviceptr grown = 0;
        if (!succeeded(api.memAlloc(&grown, newCapacity * 4), "cuMemAlloc")) {
            return;
        }
        if (*field) {
            if (count > 0) {
                succeeded(api.memcpyDeviceToDevice(grown, *field, count * 4), "cuMemcpyDtoD");
            }
            api.memFree(*field);
        }
        *field = grown;
    }
    capacity = newCapacity;
}

void CudaBackend::uploadBoard(const BrickBoard& board, const TargetField& field) {
    if (isFaulted()) {
        return;
    }
    driver.makeCurrent();
    upload(paddedCells, board.paddedCells().data(), board.paddedCells().size() * sizeof(int32_t));
    const std::vector<int32_t>& colors = board.brickColors();
    upload(brickColors, colors.empty() ? nullptr : colors.data(), std::max<size_t>(colors.size(), 1) * sizeof(int32_t));
    upload(targetCells, field.targetCells().data(), field.targetCells().size() * sizeof(int32_t));
    fieldWidth = static_cast<float>(board.columns());
    fieldHeight = static_cast<float>(board.rows());
}

void CudaBackend::addBalls(std::span<const BallSpawn> spawns) {
    if (spawns.empty() || isFaulted()) {
        return;
    }
    driver.makeCurrent();
    ensureBallCapacity(count + spawns.size());
    if (isFaulted()) {
        return;
    }
    const size_t added = spawns.size();
    std::vector<float> x(added), y(added), vx(added), vy(added);
    std::vector<int32_t> color(added), bounces(added, 0);
    std::vector<uint32_t> packed(added);
    for (size_t i = 0; i < added; ++i) {
        x[i] = spawns[i].x;
        y[i] = spawns[i].y;
        vx[i] = spawns[i].vx;
        vy[i] = spawns[i].vy;
        color[i] = spawns[i].color;
        packed[i] = packBallForRender(x[i], y[i], color[i], renderPackScale(fieldWidth), renderPackScale(fieldHeight));
    }
    const auto& api = driver.api();
    const size_t offset = count * 4;
    succeeded(api.memcpyHostToDevice(balls.x + offset, x.data(), added * 4), "upload x");
    succeeded(api.memcpyHostToDevice(balls.y + offset, y.data(), added * 4), "upload y");
    succeeded(api.memcpyHostToDevice(balls.vx + offset, vx.data(), added * 4), "upload vx");
    succeeded(api.memcpyHostToDevice(balls.vy + offset, vy.data(), added * 4), "upload vy");
    succeeded(api.memcpyHostToDevice(balls.color + offset, color.data(), added * 4), "upload color");
    succeeded(api.memcpyHostToDevice(balls.bounces + offset, bounces.data(), added * 4), "upload bounces");
    succeeded(api.memcpyHostToDevice(balls.packed + offset, packed.data(), added * 4), "upload packed");
    count += added;
    snapshotFresh = false;
}

void CudaBackend::step(const StepParams& params, std::vector<BallHit>& hits) {
    snapshotFresh = false;
    if (count == 0 || isFaulted()) {
        return;
    }
    driver.makeCurrent();
    const auto& api = driver.api();
    succeeded(api.memsetD32(hitCounter.pointer, 0, 1), "cuMemsetD32");
    DeviceBoardView board{paddedCells.pointer, brickColors.pointer, targetCells.pointer};
    int ballCountArgument = static_cast<int>(count);
    StepParams stepArgument = params;
    CUdeviceptr hitPointer = hitBuffer.pointer;
    CUdeviceptr counterPointer = hitCounter.pointer;
    unsigned int capacityArgument = hitCapacity;
    void* arguments[] = {&balls, &ballCountArgument, &board, &stepArgument, &hitPointer, &counterPointer,
                         &capacityArgument};
    const unsigned int blocks = static_cast<unsigned int>((count + threadsPerBlock - 1) / threadsPerBlock);
    succeeded(api.launchKernel(stepFunction, blocks, 1, 1, threadsPerBlock, 1, 1, 0, nullptr, arguments, nullptr),
                 "stepBallsKernel");
    unsigned int hitTotal = 0;
    if (!succeeded(api.memcpyDeviceToHost(&hitTotal, hitCounter.pointer, sizeof(hitTotal)), "download hit count")) {
        return;
    }
    const unsigned int received = std::min(hitTotal, hitCapacity);
    if (received > 0) {
        hostHits.resize(received);
        if (!succeeded(api.memcpyDeviceToHost(hostHits.data(), hitBuffer.pointer, received * sizeof(BallHit)),
                       "download hits")) {
            return;
        }
        std::ranges::sort(hostHits, {}, &BallHit::ball);
        hits.insert(hits.end(), hostHits.begin(), hostHits.end());
    }
    if (hitTotal > hitCapacity) {
        hitCapacity = std::max(hitTotal, hitCapacity * 2);
        ensureArray(hitBuffer, hitCapacity * sizeof(BallHit));
    }
}

void CudaBackend::removeBalls(std::span<const uint32_t> descendingIndices) {
    if (isFaulted()) {
        return;
    }
    if (descendingIndices.empty()) {
        return;
    }
    driver.makeCurrent();
    upload(removeIndices, descendingIndices.data(), descendingIndices.size_bytes());
    int ballCountArgument = static_cast<int>(count);
    CUdeviceptr indicesPointer = removeIndices.pointer;
    int removeCount = static_cast<int>(descendingIndices.size());
    void* arguments[] = {&balls, &ballCountArgument, &indicesPointer, &removeCount};
    succeeded(driver.api().launchKernel(removeFunction, 1, 1, 1, 1, 1, 1, 0, nullptr, arguments, nullptr),
                 "removeBallsKernel");
    count -= descendingIndices.size();
    snapshotFresh = false;
}

void CudaBackend::downloadPositions() {
    if (isFaulted() || snapshotFresh) {
        return;
    }
    driver.makeCurrent();
    hostPacked.resize(count);
    if (count > 0) {
        succeeded(driver.api().memcpyDeviceToHost(hostPacked.data(), balls.packed, count * 4), "download packed");
    }
    snapshotFresh = true;
}

BallSnapshot CudaBackend::snapshot() {
    downloadPositions();
    if (isFaulted()) {
        return {};
    }
    return {hostPacked.data(), count, fieldWidth, fieldHeight};
}

std::vector<BallSpawn> CudaBackend::exportBalls() {
    if (isFaulted() || count == 0) {
        return {};
    }
    driver.makeCurrent();
    std::vector<float> x(count), y(count), vx(count), vy(count);
    std::vector<int32_t> color(count);
    const auto& api = driver.api();
    const bool downloaded = succeeded(api.memcpyDeviceToHost(x.data(), balls.x, count * 4), "download x") &&
                            succeeded(api.memcpyDeviceToHost(y.data(), balls.y, count * 4), "download y") &&
                            succeeded(api.memcpyDeviceToHost(vx.data(), balls.vx, count * 4), "download vx") &&
                            succeeded(api.memcpyDeviceToHost(vy.data(), balls.vy, count * 4), "download vy") &&
                            succeeded(api.memcpyDeviceToHost(color.data(), balls.color, count * 4), "download color");
    if (!downloaded) {
        return {};
    }
    std::vector<BallSpawn> exported(count);
    for (size_t i = 0; i < count; ++i) {
        exported[i] = {x[i], y[i], vx[i], vy[i], color[i]};
    }
    return exported;
}
