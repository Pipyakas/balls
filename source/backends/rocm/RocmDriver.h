#pragma once

#include "common/DynamicLibrary.h"

#include <hsa.h>
#include <hsa_ext_amd.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct HsaApi {
    decltype(&::hsa_init) init = nullptr;
    decltype(&::hsa_shut_down) shutDown = nullptr;
    decltype(&::hsa_iterate_agents) iterateAgents = nullptr;
    decltype(&::hsa_agent_get_info) agentGetInfo = nullptr;
    decltype(&::hsa_amd_agent_iterate_memory_pools) amdAgentIterateMemoryPools = nullptr;
    decltype(&::hsa_amd_memory_pool_get_info) amdMemoryPoolGetInfo = nullptr;
    decltype(&::hsa_amd_memory_pool_allocate) amdMemoryPoolAllocate = nullptr;
    decltype(&::hsa_amd_memory_pool_free) amdMemoryPoolFree = nullptr;
    decltype(&::hsa_amd_agents_allow_access) amdAgentsAllowAccess = nullptr;
    decltype(&::hsa_amd_memory_async_copy) amdMemoryAsyncCopy = nullptr;
    decltype(&::hsa_amd_memory_fill) amdMemoryFill = nullptr;
    decltype(&::hsa_queue_create) queueCreate = nullptr;
    decltype(&::hsa_queue_destroy) queueDestroy = nullptr;
    decltype(&::hsa_queue_add_write_index_relaxed) queueAddWriteIndexRelaxed = nullptr;
    decltype(&::hsa_signal_create) signalCreate = nullptr;
    decltype(&::hsa_signal_destroy) signalDestroy = nullptr;
    decltype(&::hsa_signal_wait_scacquire) signalWaitScacquire = nullptr;
    decltype(&::hsa_signal_store_relaxed) signalStoreRelaxed = nullptr;
    decltype(&::hsa_signal_store_screlease) signalStoreScrelease = nullptr;
    decltype(&::hsa_signal_load_scacquire) signalLoadScacquire = nullptr;
    decltype(&::hsa_code_object_reader_create_from_memory) codeObjectReaderCreateFromMemory = nullptr;
    decltype(&::hsa_code_object_reader_destroy) codeObjectReaderDestroy = nullptr;
    decltype(&::hsa_executable_create_alt) executableCreateAlt = nullptr;
    decltype(&::hsa_executable_destroy) executableDestroy = nullptr;
    decltype(&::hsa_executable_load_agent_code_object) executableLoadAgentCodeObject = nullptr;
    decltype(&::hsa_executable_freeze) executableFreeze = nullptr;
    decltype(&::hsa_executable_get_symbol_by_name) executableGetSymbolByName = nullptr;
    decltype(&::hsa_executable_symbol_get_info) executableSymbolGetInfo = nullptr;
    decltype(&::hsa_status_string) statusString = nullptr;
    bool loaded = false;
    std::string error;
};

class RocmDriver {
public:
    static RocmDriver& instance() { return forDevice(0); }
    static RocmDriver& forDevice(int ordinal);
    static int deviceCount();

    bool isReady() const { return errorMessage.empty(); }
    const std::string& error() const { return errorMessage; }
    const std::string& deviceName() const { return gpuName; }
    const std::string& modelName() const { return gpuModel; }
    int ordinal() const { return deviceOrdinal; }
    size_t totalMemoryBytes() const { return totalMemory; }
    int pciDomain() const { return pciDomainNumber; }
    int pciBus() const { return pciBusNumber; }
    int pciDevice() const { return pciDeviceNumber; }
    bool isIntegrated() const { return integratedGpu; }

    hsa_agent_t agent() const { return gpuAgent; }
    hsa_agent_t cpuAgent() const { return hostAgent; }
    hsa_amd_memory_pool_t vramPool() const { return vramMemoryPool; }
    hsa_amd_memory_pool_t kernargPool() const { return kernargMemoryPool; }
    hsa_amd_memory_pool_t hostPool() const { return hostMemoryPool; }
    const HsaApi& api() const;

    std::string describe(hsa_status_t status, const char* what) const;

    void makeCurrent() const {}

private:
    explicit RocmDriver(int ordinal);

    int deviceOrdinal = 0;
    int pciDomainNumber = -1, pciBusNumber = -1, pciDeviceNumber = -1;
    bool integratedGpu = false;
    size_t totalMemory = 0;
    std::string gpuName;
    std::string gpuModel;
    std::string errorMessage;

    hsa_agent_t gpuAgent{};
    hsa_agent_t hostAgent{};
    hsa_amd_memory_pool_t vramMemoryPool{};
    hsa_amd_memory_pool_t kernargMemoryPool{};
    hsa_amd_memory_pool_t hostMemoryPool{};
};
