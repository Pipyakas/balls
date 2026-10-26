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
    ensureBallCapacity(initialBallCapacity);
    ensureArray(hitCounter, sizeof(unsigned int));
    hitCapacity = initialHitCapacity;
    ensureArray(hitBuffer, hitCapacity * sizeof(BallHit));
}

CudaBackend::~CudaBackend() {
    driver.makeCurrent();
    const auto& api = driver.api();
    for (CUdeviceptr pointer : {balls.x, balls.y, balls.vx, balls.vy, balls.color, balls.bounces, paddedCells.pointer,
                                brickColors.pointer, targetCells.pointer, hitBuffer.pointer, hitCounter.pointer,
                                removeIndices.pointer}) {
        if (pointer) {
            api.memFree(pointer);
        }
    }
}

void CudaBackend::ensureArray(DeviceArray& array, size_t bytes) {
    if (array.bytes >= bytes) {
        return;
    }
    const auto& api = driver.api();
    if (array.pointer) {
        api.memFree(array.pointer);
    }
    const size_t newBytes = std::max(bytes, array.bytes * 2);
    driver.check(api.memAlloc(&array.pointer, newBytes), "cuMemAlloc");
    array.bytes = newBytes;
}

void CudaBackend::upload(DeviceArray& array, const void* data, size_t bytes) {
    ensureArray(array, bytes);
    driver.check(driver.api().memcpyHostToDevice(array.pointer, data, bytes), "cuMemcpyHtoD");
}

void CudaBackend::ensureBallCapacity(size_t required) {
    if (required <= capacity) {
        return;
    }
    const auto& api = driver.api();
    const size_t newCapacity = std::max(required, capacity * 2);
    for (CUdeviceptr* field : {&balls.x, &balls.y, &balls.vx, &balls.vy, &balls.color, &balls.bounces}) {
        CUdeviceptr grown = 0;
        driver.check(api.memAlloc(&grown, newCapacity * 4), "cuMemAlloc");
        if (*field) {
            if (count > 0) {
                driver.check(api.memcpyDeviceToDevice(grown, *field, count * 4), "cuMemcpyDtoD");
            }
            api.memFree(*field);
        }
        *field = grown;
    }
    capacity = newCapacity;
}

void CudaBackend::uploadBoard(const BrickBoard& board, const TargetField& field) {
    driver.makeCurrent();
    upload(paddedCells, board.paddedCells().data(), board.paddedCells().size() * sizeof(int32_t));
    const std::vector<int32_t>& colors = board.brickColors();
    upload(brickColors, colors.empty() ? nullptr : colors.data(), std::max<size_t>(colors.size(), 1) * sizeof(int32_t));
    upload(targetCells, field.targetCells().data(), field.targetCells().size() * sizeof(int32_t));
}

void CudaBackend::addBalls(std::span<const BallSpawn> spawns) {
    if (spawns.empty()) {
        return;
    }
    driver.makeCurrent();
    ensureBallCapacity(count + spawns.size());
    const size_t added = spawns.size();
    std::vector<float> x(added), y(added), vx(added), vy(added);
    std::vector<int32_t> color(added), bounces(added, 0);
    for (size_t i = 0; i < added; ++i) {
        x[i] = spawns[i].x;
        y[i] = spawns[i].y;
        vx[i] = spawns[i].vx;
        vy[i] = spawns[i].vy;
        color[i] = spawns[i].color;
    }
    const auto& api = driver.api();
    const size_t offset = count * 4;
    driver.check(api.memcpyHostToDevice(balls.x + offset, x.data(), added * 4), "upload x");
    driver.check(api.memcpyHostToDevice(balls.y + offset, y.data(), added * 4), "upload y");
    driver.check(api.memcpyHostToDevice(balls.vx + offset, vx.data(), added * 4), "upload vx");
    driver.check(api.memcpyHostToDevice(balls.vy + offset, vy.data(), added * 4), "upload vy");
    driver.check(api.memcpyHostToDevice(balls.color + offset, color.data(), added * 4), "upload color");
    driver.check(api.memcpyHostToDevice(balls.bounces + offset, bounces.data(), added * 4), "upload bounces");
    count += added;
    snapshotFresh = false;
}

void CudaBackend::step(const StepParams& params, std::vector<BallHit>& hits) {
    snapshotFresh = false;
    if (count == 0) {
        return;
    }
    driver.makeCurrent();
    const auto& api = driver.api();
    driver.check(api.memsetD32(hitCounter.pointer, 0, 1), "cuMemsetD32");
    DeviceBoardView board{paddedCells.pointer, brickColors.pointer, targetCells.pointer};
    int ballCountArgument = static_cast<int>(count);
    StepParams stepArgument = params;
    CUdeviceptr hitPointer = hitBuffer.pointer;
    CUdeviceptr counterPointer = hitCounter.pointer;
    unsigned int capacityArgument = hitCapacity;
    void* arguments[] = {&balls, &ballCountArgument, &board, &stepArgument, &hitPointer, &counterPointer,
                         &capacityArgument};
    const unsigned int blocks = static_cast<unsigned int>((count + threadsPerBlock - 1) / threadsPerBlock);
    driver.check(api.launchKernel(stepFunction, blocks, 1, 1, threadsPerBlock, 1, 1, 0, nullptr, arguments, nullptr),
                 "stepBallsKernel");
    unsigned int hitTotal = 0;
    driver.check(api.memcpyDeviceToHost(&hitTotal, hitCounter.pointer, sizeof(hitTotal)), "download hit count");
    const unsigned int received = std::min(hitTotal, hitCapacity);
    if (received > 0) {
        hostHits.resize(received);
        driver.check(api.memcpyDeviceToHost(hostHits.data(), hitBuffer.pointer, received * sizeof(BallHit)),
                     "download hits");
        std::ranges::sort(hostHits, {}, &BallHit::ball);
        hits.insert(hits.end(), hostHits.begin(), hostHits.end());
    }
    if (hitTotal > hitCapacity) {
        hitCapacity = std::max(hitTotal, hitCapacity * 2);
        ensureArray(hitBuffer, hitCapacity * sizeof(BallHit));
    }
}

void CudaBackend::removeBalls(std::span<const uint32_t> descendingIndices) {
    if (descendingIndices.empty()) {
        return;
    }
    driver.makeCurrent();
    upload(removeIndices, descendingIndices.data(), descendingIndices.size_bytes());
    int ballCountArgument = static_cast<int>(count);
    CUdeviceptr indicesPointer = removeIndices.pointer;
    int removeCount = static_cast<int>(descendingIndices.size());
    void* arguments[] = {&balls, &ballCountArgument, &indicesPointer, &removeCount};
    driver.check(driver.api().launchKernel(removeFunction, 1, 1, 1, 1, 1, 1, 0, nullptr, arguments, nullptr),
                 "removeBallsKernel");
    count -= descendingIndices.size();
    snapshotFresh = false;
}

void CudaBackend::downloadPositions() {
    if (snapshotFresh) {
        return;
    }
    driver.makeCurrent();
    hostX.resize(count);
    hostY.resize(count);
    hostColor.resize(count);
    if (count > 0) {
        const auto& api = driver.api();
        driver.check(api.memcpyDeviceToHost(hostX.data(), balls.x, count * 4), "download x");
        driver.check(api.memcpyDeviceToHost(hostY.data(), balls.y, count * 4), "download y");
        driver.check(api.memcpyDeviceToHost(hostColor.data(), balls.color, count * 4), "download color");
    }
    snapshotFresh = true;
}

BallSnapshot CudaBackend::snapshot() {
    downloadPositions();
    return {hostX.data(), hostY.data(), hostColor.data(), count};
}

std::vector<BallSpawn> CudaBackend::exportBalls() {
    downloadPositions();
    std::vector<float> vx(count), vy(count);
    if (count > 0) {
        driver.makeCurrent();
        driver.check(driver.api().memcpyDeviceToHost(vx.data(), balls.vx, count * 4), "download vx");
        driver.check(driver.api().memcpyDeviceToHost(vy.data(), balls.vy, count * 4), "download vy");
    }
    std::vector<BallSpawn> exported(count);
    for (size_t i = 0; i < count; ++i) {
        exported[i] = {hostX[i], hostY[i], vx[i], vy[i], hostColor[i]};
    }
    return exported;
}
