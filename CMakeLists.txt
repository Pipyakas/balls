cmake_minimum_required(VERSION 3.22)
project(balls VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BALLS_PROFILE "all" CACHE STRING "Target profile to build: all or scoring")
set_property(CACHE BALLS_PROFILE PROPERTY STRINGS all scoring)

# The scoring profile builds only what the target PC (i9-14900K, GTX 1650 sm_75) runs: the CUDA+AVX2 pair, with
# every ball allocated up front. "all" is the extended build with every backend and front-end feature.
if(BALLS_PROFILE STREQUAL "scoring")
    set(BALLS_ENABLE_ROCM OFF CACHE BOOL "ROCm backend disabled in scoring profile" FORCE)
    set(BALLS_ENABLE_CUDA ON CACHE BOOL "CUDA backend required in scoring profile" FORCE)
    set(BALLS_BUILD_UNITY_PLUGIN OFF CACHE BOOL "No Unity plugin in scoring profile" FORCE)
    set(BALLS_WHOLE_PROGRAM_OPTIMIZATION ON CACHE BOOL "Whole-program optimization enabled in scoring profile" FORCE)
endif()

set(BALLS_FRONTEND "sdl" CACHE STRING "Game front end to build: sdl or none")
set_property(CACHE BALLS_FRONTEND PROPERTY STRINGS sdl none)
option(BALLS_BUILD_TOOLS "Build tests and benchmarks" ON)
option(BALLS_BUILD_UNITY_PLUGIN "Build the C ABI plugin the Unity front end loads" ON)
option(BALLS_ENABLE_CUDA "Build the CUDA physics backend (needs NVRTC at build time)" ON)
option(BALLS_STATIC_CRT "Link the static MSVC runtime (physics plugins need the shared runtime)" OFF)
# Off by default: it made no measurable difference to the physics (AVX2 972 vs 985 M ball-steps/s, the CUDA work
# runs on the GPU) and redid code generation for the whole program at every link, which made a one-line change
# take 19 s to rebuild instead of 5. Renamed from BALLS_LTO so existing build trees drop their cached ON.
option(BALLS_WHOLE_PROGRAM_OPTIMIZATION "Enable whole-program optimization (/GL /LTCG)" OFF)
set(BALLS_CUDA_SDK_DIR "${CMAKE_SOURCE_DIR}/.deps/cuda/sdk" CACHE PATH "Directory with CUDA include/ and bin/ (scripts/FetchCudaSdk.ps1)")
option(BALLS_ENABLE_ROCM "Build the ROCm physics backend (needs HSA headers and an amdgcn clang)" ON)
set(BALLS_ROCM_SDK_DIR "${CMAKE_SOURCE_DIR}/.deps/rocm" CACHE PATH "Directory with ROCm HSA include/ (scripts/FetchRocmSdk.sh)")
set(BALLS_ROCM_CLANG "/usr/lib64/rocm/llvm/bin/clang" CACHE PATH "amdgcn-capable clang for ROCm code objects")

if(BALLS_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# The NDK's libc++ keeps std::jthread behind its experimental library.
if(ANDROID)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)
    add_link_options(-fexperimental-library)
endif()

if(MSVC)
    set(BALLS_WARNINGS /W4 /permissive- /utf-8 /wd4324)
    add_compile_options(/MP)
    # Release keeps its optimizations but also writes PDBs, so a crash on a player's machine or in a bench run has a
    # readable stack. /OPT:REF and /OPT:ICF restore the dead-code and folding passes that /DEBUG turns off.
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG:FULL> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    set(BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Oi /fp:fast>)
    if(BALLS_WHOLE_PROGRAM_OPTIMIZATION)
        list(APPEND BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/GL>)
        set(BALLS_LINK_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/LTCG>)
    endif()
    set(BALLS_AVX2_FLAGS /arch:AVX2)
    set(BALLS_AVX512_FLAGS /arch:AVX512)
else()
    set(BALLS_WARNINGS -Wall -Wextra -Wpedantic)
    set(BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:-O3 -ffast-math>)
    if(BALLS_PROFILE STREQUAL "scoring")
        set(BALLS_AVX2_FLAGS -mavx2 -mfma -mtune=raptorlake)
    else()
        set(BALLS_AVX2_FLAGS -mavx2 -mfma)
    endif()
    set(BALLS_AVX512_FLAGS -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vbmi2 -mavx512vpopcntdq -mfma -mbmi2 -mtune=znver4)
endif()

if(BALLS_PROFILE STREQUAL "scoring")
    # Global fixed-target configuration: target PC is known (i9-14900K + GTX 1650), so AVX2 and FMA are universal.
    add_compile_definitions(BALLS_FIXED_TARGET=1)
    add_compile_options(${BALLS_AVX2_FLAGS})
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Every translation unit here is small, but each one re-parsed the same standard headers (<format> and
# <filesystem> alone are about a second each with MSVC), so they are precompiled once per target.
option(BALLS_PRECOMPILED_HEADERS "Precompile the common standard headers" ON)
set(BALLS_COMMON_PCH
    <algorithm> <array> <atomic> <chrono> <cmath> <cstdint> <cstdio> <cstring> <filesystem> <format> <functional>
    <map> <memory> <mutex> <numbers> <optional> <random> <span> <sstream> <string> <string_view> <thread>
    <unordered_map> <unordered_set> <vector>)
function(balls_precompile target)
    if(BALLS_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} PRIVATE ${BALLS_COMMON_PCH} ${ARGN})
    endif()
endfunction()

# Lets a target's objects compile while the static libraries it links are still building (Ninja), instead of
# waiting for them; only the link has to wait.
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

set(BALLS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/source)
set(BALLS_ASSET_DIR ${CMAKE_SOURCE_DIR}/data/assets)

# Fetch NVRHI so the Vulkan physics plugin (source/backends/vulkan) can build its headless device
# and supply Vulkan-Headers to balls_core, regardless of whether the front end renderer is WebGPU or NVRHI.
include(FetchContent)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
# Only the GPU compute tool (balls_gpucompute) uses D3D11; the game renders through D3D12 or Vulkan.
if(WIN32 AND BALLS_BUILD_TOOLS)
    set(NVRHI_WITH_DX11 ON CACHE BOOL "" FORCE)
else()
    set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
endif()
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(nvrhi)
# NVRHI's Vulkan backend spent ~9 s per file parsing vulkan.hpp; its sources define the dynamic dispatch macro
# before including it, so the same definition on the target lets the header be precompiled once.
if(TARGET nvrhi_vk AND BALLS_PRECOMPILED_HEADERS)
    target_compile_definitions(nvrhi_vk PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
    target_precompile_headers(nvrhi_vk PRIVATE <nvrhi/vulkan.h> <vulkan/vulkan.hpp> <mutex> <list>)
endif()

add_subdirectory(source/core)
add_subdirectory(source/backends)

if(BALLS_BUILD_UNITY_PLUGIN)
    add_subdirectory(source/plugin)
endif()

if(BALLS_FRONTEND STREQUAL "sdl")
    add_subdirectory(source/frontends/sdl)
endif()

# The Vulkan physics plugin is desktop-only: its link group needs CMake 3.24+, and Android runs
# physics through the statically linked CPU backend instead. The scoring profile runs physics on CUDA and AVX2 only.
if(TARGET nvrhi AND TARGET nvrhi_vk AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT BALLS_PROFILE STREQUAL "scoring")
    add_subdirectory(source/backends/vulkan)
endif()

# Vulkan-Headers is already fetched by the NVRHI renderer. This interface-only target
# supplies declarations to balls_core without linking Vulkan loader libraries.
if(TARGET Vulkan-Headers)
    target_link_libraries(balls_core PRIVATE Vulkan-Headers)
elseif(TARGET Vulkan::Headers)
    target_link_libraries(balls_core PRIVATE Vulkan::Headers)
endif()

if(NOT BALLS_BUILD_TOOLS)
    return()
endif()

add_executable(balls_bench source/tools/bench/Bench.cpp)
target_link_libraries(balls_bench PRIVATE balls_core)
add_dependencies(balls_bench balls_physics_plugins)
target_compile_options(balls_bench PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
balls_precompile(balls_bench)
target_link_options(balls_bench PRIVATE ${BALLS_LINK_OPTIMIZE})

set(BALLS_CUPTI_DIR "${CMAKE_SOURCE_DIR}/.deps/cupti" CACHE PATH "CUPTI headers for the instruction counter (scripts/FetchCudaSdk.ps1 -ProfilerTools)")
if(WIN32 AND EXISTS "${BALLS_CUPTI_DIR}/include/cupti.h" AND EXISTS "${BALLS_CUDA_SDK_DIR}/include/cuda.h")
    add_executable(balls_insncount source/tools/bench/InstructionCount.cpp)
    target_include_directories(balls_insncount PRIVATE "${BALLS_CUPTI_DIR}/include" "${BALLS_CUDA_SDK_DIR}/include")
    target_link_libraries(balls_insncount PRIVATE balls_core)
    add_dependencies(balls_insncount balls_physics_plugins)
    target_compile_options(balls_insncount PRIVATE ${BALLS_WARNINGS})
    target_link_options(balls_insncount PRIVATE ${BALLS_LINK_OPTIMIZE})
endif()

if(TARGET nvrhi AND TARGET nvrhi_vk)
    add_executable(balls_gpucompute source/tools/gpucompute/GpuComputeBench.cpp source/tools/gpucompute/BallStepVariants.cpp)
    target_include_directories(balls_gpucompute PRIVATE source/tools/gpucompute)
    target_link_libraries(balls_gpucompute PRIVATE balls_core)
    if(MSVC)
        target_link_libraries(balls_gpucompute PRIVATE nvrhi nvrhi_vk)
    else()
        # nvrhi and nvrhi_vk are static archives with cyclic references, so the GNU and Clang
        # linkers need them rescanned rather than linked in file order.
        target_link_libraries(balls_gpucompute PRIVATE $<LINK_GROUP:RESCAN,nvrhi,nvrhi_vk>)
    endif()
    if(WIN32)
        target_link_libraries(balls_gpucompute PRIVATE nvrhi_d3d11 nvrhi_d3d12 dxgi d3d11 d3d12)
    endif()
    if(TARGET Vulkan::Headers)
        target_link_libraries(balls_gpucompute PRIVATE Vulkan::Headers)
    else()
        target_link_libraries(balls_gpucompute PRIVATE Vulkan-Headers)
    endif()
    add_dependencies(balls_gpucompute balls_physics_plugins)
    target_compile_options(balls_gpucompute PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
    target_link_options(balls_gpucompute PRIVATE ${BALLS_LINK_OPTIMIZE})
endif()

enable_testing()
add_executable(balls_tests source/tests/TestMain.cpp source/tests/LevelTests.cpp source/tests/SimulationTests.cpp
    source/tests/GameTests.cpp source/tests/SessionRunnerTests.cpp source/tests/ProbeTests.cpp source/tests/EditorTests.cpp source/tests/SettingsTests.cpp
    source/tests/BrickBoardTests.cpp
    source/tests/LayoutTests.cpp
    source/tests/EditorSessionTests.cpp
    source/tests/MenuModelTests.cpp
    source/tests/ScreenFlowTests.cpp
    source/tests/PhysicsRateTests.cpp
    source/tests/ThemeTests.cpp
    source/tests/FreeFlightTests.cpp
    source/tests/LevelImportTests.cpp
    source/tests/BenchmarkTests.cpp
    source/tests/GpuAdapterTests.cpp
    source/tests/DeviceTests.cpp
    source/tests/ComposerTests.cpp
    source/tests/MachineProfileTests.cpp
    source/tests/FrameTimingsTests.cpp
    source/tests/LevelWatcherTests.cpp
    source/tests/ControlApiTests.cpp)
target_link_libraries(balls_tests PRIVATE balls_core)
add_dependencies(balls_tests balls_physics_plugins)
target_compile_options(balls_tests PRIVATE ${BALLS_WARNINGS})
balls_precompile(balls_tests)
target_link_options(balls_tests PRIVATE ${BALLS_LINK_OPTIMIZE})
target_compile_definitions(balls_tests PRIVATE BALLS_ASSET_DIR="${BALLS_ASSET_DIR}")
add_test(NAME balls_tests COMMAND balls_tests)
