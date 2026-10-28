#include "sim/BackendWorker.h"

#include <algorithm>
#include <map>

std::optional<BackendHandle> BackendBuild::take() {
    std::lock_guard lock(mutex);
    if (!finished || taken) {
        return std::nullopt;
    }
    taken = true;
    return std::move(result);
}

void BackendBuild::abandon() {
    BackendHandle unwanted{nullptr, BackendRelease{}};
    {
        std::lock_guard lock(mutex);
        abandoned = true;
        if (finished && !taken) {
            taken = true;
            unwanted = std::move(result);
        }
    }
    if (unwanted) {
        BackendWorker::shared().retire(std::move(unwanted));
    }
}

BackendWorker& BackendWorker::shared() {
    // Never destroyed: joining at exit could wait on a kernel compile, and the process ends the thread anyway.
    static BackendWorker* worker = new BackendWorker();
    return *worker;
}

BackendWorker::BackendWorker() : thread([this] { run(); }) {
    thread.detach();
}

std::shared_ptr<BackendBuild> BackendWorker::build(BackendKind kind) {
    auto build = std::make_shared<BackendBuild>(kind);
    {
        std::lock_guard lock(mutex);
        jobs.push_back([build] {
            BackendHandle backend = createBackend(build->kind());
            std::lock_guard buildLock(build->mutex);
            if (build->abandoned) {
                return;
            }
            build->result = std::move(backend);
            build->finished = true;
        });
    }
    wake.notify_one();
    return build;
}

void BackendWorker::retire(BackendHandle backend) {
    if (!backend) {
        return;
    }
    auto retired = std::make_shared<BackendHandle>(std::move(backend));
    {
        std::lock_guard lock(mutex);
        jobs.push_back([retired] { retired->reset(); });
    }
    wake.notify_one();
}

void BackendWorker::run(std::function<void()> job) {
    {
        std::lock_guard lock(mutex);
        jobs.push_back(std::move(job));
    }
    wake.notify_one();
}

void BackendWorker::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [this] { return !jobs.empty(); });
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        job();
    }
}

namespace {

struct StatusCache {
    std::mutex mutex;
    std::map<BackendKind, BackendStatus> statuses;
    std::map<std::pair<BackendKind, int>, BackendStatus> deviceStatuses;
};

StatusCache& statusCache() {
    static StatusCache cache;
    return cache;
}

BackendStatus queryStatus(BackendKind kind) {
    BackendStatus status;
    status.unavailableReason = backendUnavailableReason(kind);
    status.available = status.unavailableReason.empty();
    status.detail = status.available ? backendDetail(kind) : std::string{};
    return status;
}

} // namespace

BackendStatus backendStatus(BackendKind kind) {
    StatusCache& cache = statusCache();
    {
        std::lock_guard lock(cache.mutex);
        if (const auto found = cache.statuses.find(kind); found != cache.statuses.end()) {
            return found->second;
        }
    }
    BackendStatus status = queryStatus(kind);
    std::lock_guard lock(cache.mutex);
    return cache.statuses.try_emplace(kind, std::move(status)).first->second;
}

BackendStatus deviceBackendStatus(BackendKind kind, int pluginDevice) {
    StatusCache& cache = statusCache();
    const std::pair key{kind, pluginDevice};
    {
        std::lock_guard lock(cache.mutex);
        if (const auto found = cache.deviceStatuses.find(key); found != cache.deviceStatuses.end()) {
            return found->second;
        }
    }
    BackendStatus status;
    status.unavailableReason = backendUnavailableReasonOnDevice(kind, pluginDevice);
    status.available = status.unavailableReason.empty();
    status.detail = status.available ? backendDetailOnDevice(kind, pluginDevice) : std::string{};
    std::lock_guard lock(cache.mutex);
    return cache.deviceStatuses.try_emplace(key, std::move(status)).first->second;
}

std::vector<BackendKind> selectableBackends() {
    bool otherBackendAvailable = false;
    for (const BackendKind kind : allBackendKinds) {
        if (kind != BackendKind::Scalar && backendStatus(kind).available) {
            otherBackendAvailable = true;
            break;
        }
    }
    std::vector<BackendKind> kinds;
    for (const BackendKind kind : allBackendKinds) {
        if (kind != BackendKind::Scalar || !otherBackendAvailable) {
            kinds.push_back(kind);
        }
    }
    return kinds;
}

bool isBackendSelectable(BackendKind kind) {
    const std::vector<BackendKind> kinds = selectableBackends();
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

void refreshBackendStatuses() {
    BackendWorker::shared().run([] {
        for (const BackendKind kind : allBackendKinds) {
            BackendStatus status = queryStatus(kind);
            std::lock_guard lock(statusCache().mutex);
            statusCache().statuses[kind] = std::move(status);
        }
    });
}
