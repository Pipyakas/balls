cmake_minimum_required(VERSION 3.22)
project(ColorBlastBalls VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BALLS_FRONTEND "sdl" CACHE STRING "Game front end to build: sdl or none")
set_property(CACHE BALLS_FRONTEND PROPERTY STRINGS sdl none)
option(BALLS_BUILD_TOOLS "Build tests and benchmarks" ON)
option(BALLS_ENABLE_CUDA "Build the CUDA physics backend (needs NVRTC at build time)" ON)
option(BALLS_STATIC_CRT "Link the static MSVC runtime (physics plugins need the shared runtime)" OFF)
option(BALLS_LTO "Enable whole-program optimization (turn OFF when building balls_core for Unreal)" ON)
set(BALLS_CUDA_SDK_DIR "${CMAKE_SOURCE_DIR}/.deps/cuda/sdk" CACHE PATH "Directory with CUDA include/ and bin/ (scripts/FetchCudaSdk.ps1)")

if(BALLS_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(MSVC)
    set(BALLS_WARNINGS /W4 /permissive- /utf-8 /wd4324)
    add_compile_options(/MP)
    set(BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Oi /fp:fast>)
    if(BALLS_LTO)
        list(APPEND BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/GL>)
        set(BALLS_LINK_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/LTCG>)
    endif()
    set(BALLS_AVX2_FLAGS /arch:AVX2)
else()
    set(BALLS_WARNINGS -Wall -Wextra -Wpedantic)
    set(BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:-O3 -ffast-math>)
    set(BALLS_AVX2_FLAGS -mavx2 -mfma)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(BALLS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/source)
set(BALLS_ASSET_DIR ${CMAKE_SOURCE_DIR}/data/assets)

# Fetch NVRHI so the Vulkan physics plugin (source/backends/vulkan) can build its headless device
# and supply Vulkan-Headers to balls_core, regardless of whether the front end renderer is WebGPU or NVRHI.
include(FetchContent)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 ${WIN32} CACHE BOOL "" FORCE)
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(nvrhi)

add_subdirectory(source/core)
add_subdirectory(source/backends)

if(BALLS_FRONTEND STREQUAL "sdl")
    add_subdirectory(source/frontends/sdl)
endif()

# The Vulkan physics plugin is desktop-only: its link group needs CMake 3.24+, and Android runs
# physics through the statically linked CPU backend instead.
if(TARGET nvrhi AND TARGET nvrhi_vk AND NOT ANDROID AND NOT EMSCRIPTEN)
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
    source/tests/GameTests.cpp source/tests/ProbeTests.cpp source/tests/EditorTests.cpp source/tests/SettingsTests.cpp
    source/tests/GpuAdapterTests.cpp)
target_link_libraries(balls_tests PRIVATE balls_core)
add_dependencies(balls_tests balls_physics_plugins)
target_compile_options(balls_tests PRIVATE ${BALLS_WARNINGS})
target_link_options(balls_tests PRIVATE ${BALLS_LINK_OPTIMIZE})
target_compile_definitions(balls_tests PRIVATE BALLS_ASSET_DIR="${BALLS_ASSET_DIR}")
add_test(NAME balls_tests COMMAND balls_tests)
