add_library(balls_core STATIC
    common/BallColor.cpp
    common/DynamicLibrary.cpp
    level/LevelData.cpp
    level/LevelParser.cpp
    level/LevelWriter.cpp
    level/LevelRepository.cpp
    level/LevelWatcher.cpp
    sim/BrickBoard.cpp
    sim/TargetField.cpp
    sim/Simulation.cpp
    sim/BackendFactory.cpp
    sim/backends/CpuBallStore.cpp
    sim/backends/CpuBackendBase.cpp
    sim/backends/ScalarBackend.cpp
    sim/backends/WorkerPool.cpp
    sim/backends/Avx2Backend.cpp
    game/CapacityProbe.cpp
    game/LauncherQueue.cpp
    game/SpawnController.cpp
    game/GameSession.cpp
    editor/LevelEditor.cpp
)
target_include_directories(balls_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_compile_features(balls_core PUBLIC cxx_std_20)
target_compile_options(balls_core PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(balls_core INTERFACE ${BALLS_LINK_OPTIMIZE})
set_source_files_properties(sim/backends/Avx2Backend.cpp PROPERTIES COMPILE_OPTIONS "${BALLS_AVX2_FLAGS}")

if(BALLS_ENABLE_CUDA AND EXISTS "${BALLS_CUDA_SDK_DIR}/include/nvrtc.h")
    add_executable(balls_ptxgen ${CMAKE_SOURCE_DIR}/tools/ptxgen/PtxGen.cpp)
    target_include_directories(balls_ptxgen PRIVATE "${BALLS_CUDA_SDK_DIR}/include")
    target_compile_options(balls_ptxgen PRIVATE ${BALLS_WARNINGS})

    set(BALLS_KERNEL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/sim/kernels/BallStepCore.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/sim/kernels/BallKernels.cu")
    set(BALLS_KERNEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BallKernelsPtx.h")
    add_custom_command(
        OUTPUT "${BALLS_KERNEL_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${BALLS_CUDA_SDK_DIR}/bin"
                $<TARGET_FILE:balls_ptxgen> "${BALLS_KERNEL_HEADER}" ${BALLS_KERNEL_SOURCES}
        DEPENDS balls_ptxgen ${BALLS_KERNEL_SOURCES}
        COMMENT "Compiling CUDA kernels to PTX with NVRTC")
    add_custom_target(balls_kernels DEPENDS "${BALLS_KERNEL_HEADER}")

    target_sources(balls_core PRIVATE sim/backends/CudaBackend.cpp sim/backends/CudaDriver.cpp)
    target_include_directories(balls_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${BALLS_CUDA_SDK_DIR}/include")
    target_compile_definitions(balls_core PRIVATE BALLS_HAS_CUDA=1)
    add_dependencies(balls_core balls_kernels)
endif()

install(TARGETS balls_core ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION include
    FILES_MATCHING PATTERN "*.h"
    PATTERN "backends/Cuda*" EXCLUDE)
