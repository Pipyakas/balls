#pragma once

#include "sim/BackendFactory.h"
#include "sim/BackendHandle.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// One backend being built off the frame. The session that asked keeps it and polls it at the start of each update;
// the worker fills it in. A build nobody wants any more is abandoned, and the worker destroys what it produces.
class BackendBuild {
public:
    explicit BackendBuild(BackendKind kind) : requested(kind) {}

    BackendKind kind() const { return requested; }
    // The finished backend, once; a null handle means it could not be created.
    std::optional<BackendHandle> take();
    void abandon();

private:
    friend class BackendWorker;

    BackendKind requested;
    std::mutex mutex;
    bool finished = false;
    bool taken = false;
    bool abandoned = false;
    BackendHandle result{nullptr, BackendRelease{}};
};

// Loading a plugin, opening a GPU device and compiling kernels take from tens of milliseconds to seconds, and
// freeing gigabytes of device memory is not free either, so neither may happen on the frame. A single process-wide
// thread does both, in the order they were asked for.
class BackendWorker {
public:
    static BackendWorker& shared();

    std::shared_ptr<BackendBuild> build(BackendKind kind);
    void retire(BackendHandle backend);
    void run(std::function<void()> job);

private:
    BackendWorker();
    void run();

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    std::thread thread;
};

// What the UI shows about a backend. Asking a GPU plugin can open a device, which the Vulkan plugin does again after
// the compute GPU preference changes, so the UI reads this cache instead: the first call fills it, and
// refreshBackendStatuses recomputes it on the backend worker while the previous answers stay readable.
struct BackendStatus {
    bool available = false;
    std::string unavailableReason;
    std::string detail;
};

BackendStatus backendStatus(BackendKind kind);
// The backends the game offers and picks from. Scalar is the one-thread fallback and the only backend with ball
// collisions, so it is left out whenever any other backend is available.
std::vector<BackendKind> selectableBackends();
bool isBackendSelectable(BackendKind kind);
void refreshBackendStatuses();
