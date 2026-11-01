#pragma once

#include "common/DynamicLibrary.h"
#include "sim/PhysicsBackend.h"

#include <memory>

struct BackendRelease {
    DestroyBackendFunction destroy = nullptr;
    std::shared_ptr<DynamicLibrary> library;

    void operator()(PhysicsBackend* backend) const {
        if (backend && destroy) {
            destroy(backend);
        }
    }
};

using BackendHandle = std::unique_ptr<PhysicsBackend, BackendRelease>;
