cmake_minimum_required(VERSION 3.25)
project(ColorBlastBalls VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(BALLS_BUILD_GAME "Build the raylib game executable" ON)
option(BALLS_ENABLE_CUDA "Build the CUDA physics backend (needs NVRTC at build time)" ON)
set(BALLS_CUDA_SDK_DIR "${CMAKE_SOURCE_DIR}/.deps/cuda/sdk" CACHE PATH "Directory with CUDA include/ and bin/ (tools/FetchCudaSdk.ps1)")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
if(BALLS_BUILD_GAME)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(raylib)
endif()

if(MSVC)
    set(BALLS_WARNINGS /W4 /permissive- /utf-8 /wd4324)
    set(BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Oi /GL /fp:fast>)
    set(BALLS_LINK_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:/LTCG>)
else()
    set(BALLS_WARNINGS -Wall -Wextra -Wpedantic)
    set(BALLS_OPTIMIZE $<$<CONFIG:Release,RelWithDebInfo>:-O3 -ffast-math>)
    set(BALLS_LINK_OPTIMIZE)
endif()

add_library(balls_sim STATIC
    src/core/BallColor.cpp
    src/level/LevelData.cpp
    src/level/LevelParser.cpp
    src/level/LevelWriter.cpp
    src/level/LevelRepository.cpp
    src/level/LevelWatcher.cpp
    src/sim/BrickBoard.cpp
    src/sim/TargetField.cpp
    src/sim/Simulation.cpp
    src/sim/BackendFactory.cpp
    src/sim/backends/CpuBallStore.cpp
    src/sim/backends/CpuBackendBase.cpp
    src/sim/backends/ScalarBackend.cpp
    src/sim/backends/WorkerPool.cpp
    src/sim/backends/Avx2Backend.cpp
)
target_include_directories(balls_sim PUBLIC src)
target_compile_options(balls_sim PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(balls_sim INTERFACE ${BALLS_LINK_OPTIMIZE})
if(MSVC)
    set_source_files_properties(src/sim/backends/Avx2Backend.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
else()
    set_source_files_properties(src/sim/backends/Avx2Backend.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
endif()

if(BALLS_ENABLE_CUDA AND EXISTS "${BALLS_CUDA_SDK_DIR}/include/nvrtc.h")
    add_executable(balls_ptxgen tools/ptxgen/PtxGen.cpp)
    target_include_directories(balls_ptxgen PRIVATE "${BALLS_CUDA_SDK_DIR}/include")
    target_compile_options(balls_ptxgen PRIVATE ${BALLS_WARNINGS})

    set(BALLS_KERNEL_SOURCES
        "${CMAKE_SOURCE_DIR}/src/sim/kernels/BallStepCore.h"
        "${CMAKE_SOURCE_DIR}/src/sim/kernels/BallKernels.cu")
    set(BALLS_KERNEL_HEADER "${CMAKE_BINARY_DIR}/generated/BallKernelsPtx.h")
    add_custom_command(
        OUTPUT "${BALLS_KERNEL_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${BALLS_CUDA_SDK_DIR}/bin"
                $<TARGET_FILE:balls_ptxgen> "${BALLS_KERNEL_HEADER}" ${BALLS_KERNEL_SOURCES}
        DEPENDS balls_ptxgen ${BALLS_KERNEL_SOURCES}
        COMMENT "Compiling CUDA kernels to PTX with NVRTC")
    add_custom_target(balls_kernels DEPENDS "${BALLS_KERNEL_HEADER}")

    target_sources(balls_sim PRIVATE src/sim/backends/CudaBackend.cpp src/sim/backends/CudaDriver.cpp)
    target_include_directories(balls_sim PRIVATE "${CMAKE_BINARY_DIR}/generated" "${BALLS_CUDA_SDK_DIR}/include")
    target_compile_definitions(balls_sim PUBLIC BALLS_HAS_CUDA=1)
    add_dependencies(balls_sim balls_kernels)
endif()

if(BALLS_BUILD_GAME)
add_executable(ColorBlastBalls WIN32
    src/app/Main.cpp
    src/app/App.cpp
    src/app/Paths.cpp
    src/render/Palette.cpp
    src/render/BallRenderer.cpp
    src/render/BoardRenderer.cpp
    src/render/ScreenLayout.cpp
    src/ui/Widgets.cpp
    src/game/LauncherQueue.cpp
    src/game/GameSession.cpp
    src/game/SpawnController.cpp
    src/screens/MainMenuScreen.cpp
    src/screens/LevelSelectScreen.cpp
    src/screens/GameplayScreen.cpp
    src/editor/MapEditorScreen.cpp
)
target_link_libraries(ColorBlastBalls PRIVATE balls_sim raylib)
target_compile_options(ColorBlastBalls PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(ColorBlastBalls PRIVATE ${BALLS_LINK_OPTIMIZE})
if(MSVC)
    target_link_options(ColorBlastBalls PRIVATE /ENTRY:mainCRTStartup)
endif()
add_custom_command(TARGET ColorBlastBalls POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ColorBlastBalls>/assets")
endif()

add_executable(balls_bench tools/bench/Bench.cpp)
target_link_libraries(balls_bench PRIVATE balls_sim)
target_compile_options(balls_bench PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(balls_bench PRIVATE ${BALLS_LINK_OPTIMIZE})

enable_testing()
add_executable(balls_tests tests/TestMain.cpp tests/LevelTests.cpp tests/SimulationTests.cpp)
target_link_libraries(balls_tests PRIVATE balls_sim)
target_compile_options(balls_tests PRIVATE ${BALLS_WARNINGS})
target_compile_definitions(balls_tests PRIVATE BALLS_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME balls_tests COMMAND balls_tests)
