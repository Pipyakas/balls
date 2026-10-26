#include "sim/backends/CudaBackend.h"

#include <algorithm>

namespace {

constexpr unsigned int threadsPerBlock = 256;
constexpr size_t initialBallCapacity = 1 << 16;
constexpr unsigned int writeDiscardFlag = 0x02;
constexpr size_t ballArrayCount = 7;

struct DeviceBoardView {
    CUdeviceptr paddedCells;
    CUdeviceptr brickColors;
    CUdeviceptr targetCells;
};

static_assert(sizeof(DeviceBoardView) == sizeof(BoardView));

unsigned int blocksFor(size_t threads) {
    return static_cast<unsigned int>((threads + threadsPerBlock - 1) / threadsPerBlock);
}

}

std::string CudaBackend::unavailableReason() {
    const CudaDriver& driver = CudaDriver::instance();
    return driver.isReady() ? std::string{} : driver.error();
}

CudaBackend::CudaBackend() : driver(CudaDriver::instance()) {
    driver.makeCurrent();
    stepFunction = driver.function("stepBallsKernel");
    collectHolesFunction = driver.function("collectHolesKernel");
    fillHolesFunction = driver.function("fillHolesKernel");
    if (!stepFunction || !collectHolesFunction || !fillHolesFunction) {
        fault = "CUDA kernels missing from module";
        return;
    }
    ensureBallCapacity(initialBallCapacity);
    ensureArray(counters, sizeof(FrameCounters));
}

CudaBackend::~CudaBackend() {
    driver.makeCurrent();
    releaseRenderBuffer();
    const auto& api = driver.api();
    for (CUdeviceptr pointer :
         {balls.x, balls.y, balls.vx, balls.vy, balls.color, balls.bounces, balls.packed, paddedCells.pointer,
          brickColors.pointer, targetCells.pointer, brickHitsLeft.pointer, killedIndices.pointer, holes.pointer,
          counters.pointer}) {
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

bool CudaBackend::launch(CUfunction function, size_t threads, void** arguments, const char* what) {
    if (threads == 0) {
        return true;
    }
    return succeeded(driver.api().launchKernel(function, blocksFor(threads), 1, 1, threadsPerBlock, 1, 1, 0, nullptr,
                                               arguments, nullptr),
                     what);
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
    if (isFaulted() || bytes == 0) {
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
    const std::array<CUdeviceptr*, ballArrayCount> fields{&balls.x,     &balls.y,       &balls.vx,    &balls.vy,
                                                         &balls.color, &balls.bounces, &balls.packed};
    for (CUdeviceptr* field : fields) {
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
    ensureArray(killedIndices, capacity * 4);
    ensureArray(holes, capacity * 4);
}

void CudaBackend::uploadBoard(const BrickBoard& board, const TargetField& field) {
    if (isFaulted()) {
        return;
    }
    driver.makeCurrent();
    upload(paddedCells, board.paddedCells().data(), board.paddedCells().size() * sizeof(int32_t));
    std::vector<int32_t> colors = board.brickColors();
    colors.push_back(-1);
    upload(brickColors, colors.data(), colors.size() * sizeof(int32_t));
    upload(targetCells, field.targetCells().data(), field.targetCells().size() * sizeof(int32_t));
    hostBrickHitsLeft = board.hitsLeftPerBrick();
    brickCount = hostBrickHitsLeft.size();
    hostBrickHitsLeft.push_back(0);
    upload(brickHitsLeft, hostBrickHitsLeft.data(), hostBrickHitsLeft.size() * sizeof(int32_t));
    hostBrickHitsLeft.pop_back();
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

void CudaBackend::step(const StepParams& params, std::vector<BallHit>&) {
    snapshotFresh = false;
    lastCounters = {};
    if (count == 0 || isFaulted()) {
        return;
    }
    driver.makeCurrent();
    const auto& api = driver.api();
    if (!succeeded(api.memsetD32(counters.pointer, 0, sizeof(FrameCounters) / 4), "reset counters")) {
        return;
    }
    DeviceBoardView board{paddedCells.pointer, brickColors.pointer, targetCells.pointer};
    int ballCountArgument = static_cast<int>(count);
    StepParams stepArgument = params;
    CUdeviceptr hitsLeftPointer = brickHitsLeft.pointer;
    CUdeviceptr killedPointer = killedIndices.pointer;
    CUdeviceptr countersPointer = counters.pointer;
    void* arguments[] = {&balls, &ballCountArgument, &board, &stepArgument, &hitsLeftPointer, &killedPointer,
                         &countersPointer};
    if (!launch(stepFunction, count, arguments, "stepBallsKernel")) {
        return;
    }
    if (!succeeded(api.memcpyDeviceToHost(&lastCounters, counters.pointer, sizeof(FrameCounters)), "download counters")) {
        return;
    }
    if (lastCounters.killedCount == 0) {
        return;
    }
    compactKilledBalls(lastCounters.killedCount);
    if (brickCount > 0) {
        succeeded(api.memcpyDeviceToHost(hostBrickHitsLeft.data(), brickHitsLeft.pointer, brickCount * sizeof(int32_t)),
                  "download brick hits");
    }
}

void CudaBackend::compactKilledBalls(unsigned int killed) {
    const unsigned int oldCount = static_cast<unsigned int>(count);
    unsigned int survivorCount = oldCount - killed;
    CUdeviceptr killedPointer = killedIndices.pointer;
    CUdeviceptr holesPointer = holes.pointer;
    CUdeviceptr countersPointer = counters.pointer;
    void* collectArguments[] = {&killedPointer, &killed, &survivorCount, &holesPointer, &countersPointer};
    if (!launch(collectHolesFunction, killed, collectArguments, "collectHolesKernel")) {
        return;
    }
    unsigned int oldCountArgument = oldCount;
    void* fillArguments[] = {&balls, &survivorCount, &oldCountArgument, &holesPointer, &countersPointer};
    if (!launch(fillHolesFunction, killed, fillArguments, "fillHolesKernel")) {
        return;
    }
    count = survivorCount;
}

DeviceHitResults CudaBackend::deviceHitResults() const {
    DeviceHitResults results;
    if (lastCounters.killedCount == 0) {
        return results;
    }
    results.brickHitsLeft = std::span<const int32_t>(hostBrickHitsLeft.data(), brickCount);
    for (int color = 0; color < ballColorCount; ++color) {
        results.killsByColor[color] = lastCounters.killsByColor[color];
    }
    results.killed = lastCounters.killedCount;
    return results;
}

void CudaBackend::removeBalls(std::span<const uint32_t>) {}

void CudaBackend::releaseRenderBuffer() {
    if (renderResource) {
        driver.api().graphicsUnregisterResource(renderResource);
        renderResource = nullptr;
    }
    renderCapacityBytes = 0;
}

bool CudaBackend::shareRenderBuffer(unsigned int openGlBuffer, size_t capacityBytes) {
    if (isFaulted() || !driver.api().graphicsGlRegisterBuffer) {
        return false;
    }
    driver.makeCurrent();
    releaseRenderBuffer();
    if (driver.api().graphicsGlRegisterBuffer(&renderResource, openGlBuffer, writeDiscardFlag) != CUDA_SUCCESS) {
        renderResource = nullptr;
        return false;
    }
    renderCapacityBytes = capacityBytes;
    return true;
}

bool CudaBackend::copyIntoRenderBuffer() {
    if (!renderResource || count * 4 > renderCapacityBytes) {
        return false;
    }
    const auto& api = driver.api();
    if (api.graphicsMapResources(1, &renderResource, nullptr) != CUDA_SUCCESS) {
        return false;
    }
    CUdeviceptr mapped = 0;
    size_t mappedBytes = 0;
    bool copied = api.graphicsResourceGetMappedPointer(&mapped, &mappedBytes, renderResource) == CUDA_SUCCESS &&
                  mappedBytes >= count * 4 &&
                  succeeded(api.memcpyDeviceToDevice(mapped, balls.packed, count * 4), "copy to render buffer");
    api.graphicsUnmapResources(1, &renderResource, nullptr);
    return copied;
}

void CudaBackend::downloadPacked() {
    hostPacked.resize(count);
    if (count > 0) {
        succeeded(driver.api().memcpyDeviceToHost(hostPacked.data(), balls.packed, count * 4), "download packed");
    }
}

BallSnapshot CudaBackend::snapshot() {
    if (isFaulted()) {
        return {};
    }
    driver.makeCurrent();
    if (count > 0 && copyIntoRenderBuffer()) {
        return {nullptr, count, fieldWidth, fieldHeight, true};
    }
    if (!snapshotFresh) {
        downloadPacked();
        snapshotFresh = true;
    }
    if (isFaulted()) {
        return {};
    }
    return {hostPacked.data(), count, fieldWidth, fieldHeight, false};
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
