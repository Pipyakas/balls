#include "rocm/RocmDriver.h"

#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <vector>

namespace {
HsaApi& hsaApiInstance() {
    static HsaApi api;
    static bool initialized = false;
    if (initialized) return api;
    initialized = true;
    static DynamicLibrary lib("libhsa-runtime64.so.1");
    if (!lib.isLoaded()) {
        api.error = "no HSA runtime (libhsa-runtime64.so.1)";
        return api;
    }

    auto resolve = [&]<typename T>(const char* name, T& target) -> bool {
        return lib.resolve(name, target);
    };

    if (!resolve("hsa_init", api.init) ||
        !resolve("hsa_shut_down", api.shutDown) ||
        !resolve("hsa_iterate_agents", api.iterateAgents) ||
        !resolve("hsa_agent_get_info", api.agentGetInfo) ||
        !resolve("hsa_amd_agent_iterate_memory_pools", api.amdAgentIterateMemoryPools) ||
        !resolve("hsa_amd_memory_pool_get_info", api.amdMemoryPoolGetInfo) ||
        !resolve("hsa_amd_memory_pool_allocate", api.amdMemoryPoolAllocate) ||
        !resolve("hsa_amd_memory_pool_free", api.amdMemoryPoolFree) ||
        !resolve("hsa_amd_agents_allow_access", api.amdAgentsAllowAccess) ||
        !resolve("hsa_amd_memory_async_copy", api.amdMemoryAsyncCopy) ||
        !resolve("hsa_amd_memory_fill", api.amdMemoryFill) ||
        !resolve("hsa_queue_create", api.queueCreate) ||
        !resolve("hsa_queue_destroy", api.queueDestroy) ||
        !resolve("hsa_queue_add_write_index_relaxed", api.queueAddWriteIndexRelaxed) ||
        !resolve("hsa_signal_create", api.signalCreate) ||
        !resolve("hsa_signal_destroy", api.signalDestroy) ||
        !resolve("hsa_signal_wait_scacquire", api.signalWaitScacquire) ||
        !resolve("hsa_signal_store_relaxed", api.signalStoreRelaxed) ||
        !resolve("hsa_signal_store_screlease", api.signalStoreScrelease) ||
        !resolve("hsa_signal_load_scacquire", api.signalLoadScacquire) ||
        !resolve("hsa_code_object_reader_create_from_memory", api.codeObjectReaderCreateFromMemory) ||
        !resolve("hsa_code_object_reader_destroy", api.codeObjectReaderDestroy) ||
        !resolve("hsa_executable_create_alt", api.executableCreateAlt) ||
        !resolve("hsa_executable_destroy", api.executableDestroy) ||
        !resolve("hsa_executable_load_agent_code_object", api.executableLoadAgentCodeObject) ||
        !resolve("hsa_executable_freeze", api.executableFreeze) ||
        !resolve("hsa_executable_get_symbol_by_name", api.executableGetSymbolByName) ||
        !resolve("hsa_executable_symbol_get_info", api.executableSymbolGetInfo) ||
        !resolve("hsa_status_string", api.statusString)) {
        api.error = "HSA runtime missing required entry points";
        return api;
    }

    if (api.init() != HSA_STATUS_SUCCESS) {
        api.error = "hsa_init failed";
        return api;
    }
    api.loaded = true;
    return api;
}

struct AgentCtx {
    std::vector<hsa_agent_t> gpus;
    hsa_agent_t cpu{};
    bool foundCpu = false;
};

hsa_status_t agentIterCb(hsa_agent_t agent, void* data) {
    auto* ctx = static_cast<AgentCtx*>(data);
    hsa_device_type_t type = HSA_DEVICE_TYPE_CPU;
    hsaApiInstance().agentGetInfo(agent, HSA_AGENT_INFO_DEVICE, &type);
    if (type == HSA_DEVICE_TYPE_GPU) {
        ctx->gpus.push_back(agent);
    } else if (type == HSA_DEVICE_TYPE_CPU && !ctx->foundCpu) {
        ctx->cpu = agent;
        ctx->foundCpu = true;
    }
    return HSA_STATUS_SUCCESS;
}
}

const HsaApi& RocmDriver::api() const {
    return hsaApiInstance();
}

std::string RocmDriver::describe(hsa_status_t status, const char* what) const {
    const char* str = nullptr;
    if (hsaApiInstance().statusString) {
        hsaApiInstance().statusString(status, &str);
    }
    return std::format("{}: {} (status {})", what, str ? str : "unknown error", static_cast<int>(status));
}

RocmDriver& RocmDriver::forDevice(int ordinal) {
    static std::mutex mutex;
    static std::map<int, std::unique_ptr<RocmDriver>> drivers;
    std::lock_guard lock(mutex);
    auto& d = drivers[ordinal];
    if (!d) d.reset(new RocmDriver(ordinal));
    return *d;
}

int RocmDriver::deviceCount() {
    HsaApi& api = hsaApiInstance();
    if (!api.loaded) return 0;
    AgentCtx ctx;
    api.iterateAgents(agentIterCb, &ctx);
    return static_cast<int>(ctx.gpus.size());
}

RocmDriver::RocmDriver(int ordinal) : deviceOrdinal(ordinal) {
    HsaApi& api = hsaApiInstance();
    if (!api.loaded) {
        errorMessage = api.error;
        return;
    }
    AgentCtx ctx;
    api.iterateAgents(agentIterCb, &ctx);
    if (ordinal < 0 || ordinal >= static_cast<int>(ctx.gpus.size())) {
        errorMessage = "no ROCm device";
        return;
    }
    agent_ = ctx.gpus[ordinal];
    cpuAgent_ = ctx.cpu;

    char name[256]{};
    api.agentGetInfo(agent_, HSA_AGENT_INFO_NAME, name);
    gpuModel = name;
    gpuName = name;
    char product[256]{};
    if (api.agentGetInfo(agent_, static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_PRODUCT_NAME), product) == HSA_STATUS_SUCCESS && product[0]) {
        gpuName = product;
    }

    // Iterate GPU pools to find VRAM and sum size
    struct GpuPoolCtx {
        HsaApi* api;
        size_t total;
        hsa_amd_memory_pool_t vramPool;
        bool foundVram;
    } gpuCtx{&api, 0, {}, false};

    auto gpuPoolCb = [](hsa_amd_memory_pool_t pool, void* data) -> hsa_status_t {
        auto* c = static_cast<GpuPoolCtx*>(data);
        hsa_amd_segment_t seg = static_cast<hsa_amd_segment_t>(0);
        c->api->amdMemoryPoolGetInfo(pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &seg);
        hsa_amd_memory_pool_location_t loc = HSA_AMD_MEMORY_POOL_LOCATION_CPU;
        c->api->amdMemoryPoolGetInfo(pool, HSA_AMD_MEMORY_POOL_INFO_LOCATION, &loc);
        if (seg == HSA_AMD_SEGMENT_GLOBAL && loc == HSA_AMD_MEMORY_POOL_LOCATION_GPU) {
            size_t size = 0;
            c->api->amdMemoryPoolGetInfo(pool, HSA_AMD_MEMORY_POOL_INFO_SIZE, &size);
            c->total += size;
            if (!c->foundVram) {
                c->vramPool = pool;
                c->foundVram = true;
            }
        }
        return HSA_STATUS_SUCCESS;
    };
    api.amdAgentIterateMemoryPools(agent_, gpuPoolCb, &gpuCtx);
    if (gpuCtx.total > 0) totalMemory = gpuCtx.total;
    if (gpuCtx.foundVram) vramPool_ = gpuCtx.vramPool;

    // Iterate CPU agent pools to find kernarg and host pools
    if (ctx.foundCpu) {
        struct CpuPoolCtx {
            HsaApi* api;
            hsa_amd_memory_pool_t kernargPool;
            bool foundKernarg;
            hsa_amd_memory_pool_t hostPool;
            bool foundHost;
        } cpuCtx{&api, {}, false, {}, false};

        auto cpuPoolCb = [](hsa_amd_memory_pool_t pool, void* data) -> hsa_status_t {
            auto* c = static_cast<CpuPoolCtx*>(data);
            hsa_amd_segment_t seg = static_cast<hsa_amd_segment_t>(0);
            c->api->amdMemoryPoolGetInfo(pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &seg);
            uint32_t flags = 0;
            c->api->amdMemoryPoolGetInfo(pool, HSA_AMD_MEMORY_POOL_INFO_GLOBAL_FLAGS, &flags);
            if (seg == HSA_AMD_SEGMENT_GLOBAL) {
                if ((flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT) && !c->foundKernarg) {
                    c->kernargPool = pool;
                    c->foundKernarg = true;
                }
                if ((flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED) && !c->foundHost) {
                    c->hostPool = pool;
                    c->foundHost = true;
                }
            }
            return HSA_STATUS_SUCCESS;
        };
        api.amdAgentIterateMemoryPools(cpuAgent_, cpuPoolCb, &cpuCtx);
        if (cpuCtx.foundKernarg) kernargPool_ = cpuCtx.kernargPool;
        if (cpuCtx.foundHost) hostPool_ = cpuCtx.hostPool;
    }

    uint32_t bdfid = 0;
    if (api.agentGetInfo(agent_, static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_BDFID), &bdfid) == HSA_STATUS_SUCCESS) {
        pciBus_ = (bdfid >> 8) & 0xff;
        pciDevice_ = bdfid & 0xff;
    }
    uint32_t domain = 0;
    if (api.agentGetInfo(agent_, static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_DOMAIN), &domain) == HSA_STATUS_SUCCESS) {
        pciDomain_ = static_cast<int>(domain);
    }
}
