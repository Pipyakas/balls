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

class BackendBuild {
public:
    explicit BackendBuild(BackendKind kind) : requested(kind) {}

    BackendKind kind() const { return requested; }

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

struct BackendStatus {
    bool available = false;
    std::string unavailableReason;
    std::string detail;
};

BackendStatus backendStatus(BackendKind kind);

std::vector<BackendKind> selectableBackends();
bool isBackendSelectable(BackendKind kind);

BackendKind offeredBackend(BackendKind kind);

BackendStatus deviceBackendStatus(BackendKind kind, int pluginDevice);
void refreshBackendStatuses();
