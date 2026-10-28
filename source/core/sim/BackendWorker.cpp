#include "sim/BackendWorker.h"

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
