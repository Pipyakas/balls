add_library(balls_physics_cpu SHARED
    cpu/CpuBallStore.cpp
    cpu/CompactBallStore.cpp
    cpu/ScalarBackend.cpp
    cpu/WorkerPool.cpp
    cpu/CpuPlugin.cpp
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x86|i[3-6]86)$")
    target_sources(balls_physics_cpu PRIVATE cpu/Avx2Backend.cpp)
    target_compile_definitions(balls_physics_cpu PRIVATE BALLS_HAS_AVX2_BACKEND=1)
    set_source_files_properties(cpu/Avx2Backend.cpp PROPERTIES COMPILE_OPTIONS "${BALLS_AVX2_FLAGS}")
endif()
target_include_directories(balls_physics_cpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(balls_physics_cpu PRIVATE balls_physics_interface)
target_compile_options(balls_physics_cpu PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(balls_physics_cpu PRIVATE ${BALLS_LINK_OPTIMIZE})
set(BALLS_PHYSICS_PLUGINS balls_physics_cpu)

if(BALLS_ENABLE_CUDA AND EXISTS "${BALLS_CUDA_SDK_DIR}/include/nvrtc.h")
    add_executable(balls_ptxgen ${CMAKE_SOURCE_DIR}/tools/ptxgen/PtxGen.cpp)
    target_include_directories(balls_ptxgen PRIVATE "${BALLS_CUDA_SDK_DIR}/include")
    target_compile_options(balls_ptxgen PRIVATE ${BALLS_WARNINGS})

    set(BALLS_KERNEL_SOURCES
        "${CMAKE_SOURCE_DIR}/core/sim/kernels/BallStepCore.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/cuda/BallKernels.cu")
    set(BALLS_KERNEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BallKernelImages.h")
    add_custom_command(
        OUTPUT "${BALLS_KERNEL_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${BALLS_CUDA_SDK_DIR}/bin"
                $<TARGET_FILE:balls_ptxgen> "${BALLS_KERNEL_HEADER}" ${BALLS_KERNEL_SOURCES}
        DEPENDS balls_ptxgen ${BALLS_KERNEL_SOURCES}
        COMMENT "Compiling CUDA kernels to SASS and PTX with NVRTC")
    add_custom_target(balls_kernels DEPENDS "${BALLS_KERNEL_HEADER}")

    add_library(balls_physics_cuda SHARED
        cuda/CudaBackend.cpp
        cuda/CudaDriver.cpp
        cuda/CudaPlugin.cpp
        ${CMAKE_SOURCE_DIR}/core/common/DynamicLibrary.cpp
    )
    target_include_directories(balls_physics_cuda PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${BALLS_CUDA_SDK_DIR}/include")
    target_link_libraries(balls_physics_cuda PRIVATE balls_physics_interface)
    target_compile_options(balls_physics_cuda PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
    target_link_options(balls_physics_cuda PRIVATE ${BALLS_LINK_OPTIMIZE})
    add_dependencies(balls_physics_cuda balls_kernels)
    list(APPEND BALLS_PHYSICS_PLUGINS balls_physics_cuda)
endif()

add_custom_target(balls_physics_plugins DEPENDS ${BALLS_PHYSICS_PLUGINS})
