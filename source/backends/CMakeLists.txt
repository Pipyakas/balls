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
balls_precompile(balls_physics_cpu)
# Compiled with /arch:AVX2, which a header precompiled without it cannot be used with.
set_source_files_properties(cpu/Avx2Backend.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_link_options(balls_physics_cpu PRIVATE ${BALLS_LINK_OPTIMIZE})
set(BALLS_PHYSICS_PLUGINS balls_physics_cpu)

if(BALLS_ENABLE_CUDA AND EXISTS "${BALLS_CUDA_SDK_DIR}/include/nvrtc.h")
    add_executable(balls_ptxgen ${BALLS_SOURCE_DIR}/tools/ptxgen/PtxGen.cpp)
    target_include_directories(balls_ptxgen PRIVATE "${BALLS_CUDA_SDK_DIR}/include")
    target_compile_options(balls_ptxgen PRIVATE ${BALLS_WARNINGS})

    set(BALLS_KERNEL_SOURCES
        "${BALLS_SOURCE_DIR}/core/sim/kernels/BallStepCore.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/cuda/BallKernels.cu")
    set(BALLS_KERNEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BallKernelImages.h")
    add_custom_command(
        OUTPUT "${BALLS_KERNEL_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${BALLS_CUDA_SDK_DIR}/bin" "LD_LIBRARY_PATH=${BALLS_CUDA_SDK_DIR}/lib"
                $<TARGET_FILE:balls_ptxgen> "${BALLS_KERNEL_HEADER}" ${BALLS_KERNEL_SOURCES}
        DEPENDS balls_ptxgen ${BALLS_KERNEL_SOURCES}
        COMMENT "Compiling CUDA kernels to SASS and PTX with NVRTC")
    add_custom_target(balls_kernels DEPENDS "${BALLS_KERNEL_HEADER}")

    add_library(balls_physics_cuda SHARED
        cuda/CudaBackend.cpp
        cuda/CudaDriver.cpp
        cuda/CudaPlugin.cpp
        ${BALLS_SOURCE_DIR}/core/common/DynamicLibrary.cpp
    )
    target_include_directories(balls_physics_cuda PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${BALLS_CUDA_SDK_DIR}/include")
    target_link_libraries(balls_physics_cuda PRIVATE balls_physics_interface)
    target_compile_options(balls_physics_cuda PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
    balls_precompile(balls_physics_cuda)
    target_link_options(balls_physics_cuda PRIVATE ${BALLS_LINK_OPTIMIZE})
    add_dependencies(balls_physics_cuda balls_kernels)
    list(APPEND BALLS_PHYSICS_PLUGINS balls_physics_cuda)
endif()

if(NOT WIN32 AND NOT APPLE AND BALLS_ENABLE_ROCM AND EXISTS "${BALLS_ROCM_SDK_DIR}/include/hsa.h")
    # Find clang
    if(NOT EXISTS "${BALLS_ROCM_CLANG}")
        if(EXISTS "/usr/lib64/rocm/llvm/bin/clang")
            set(BALLS_ROCM_CLANG "/usr/lib64/rocm/llvm/bin/clang" CACHE PATH "" FORCE)
        elseif(EXISTS "/opt/rocm/llvm/bin/clang")
            set(BALLS_ROCM_CLANG "/opt/rocm/llvm/bin/clang" CACHE PATH "" FORCE)
        endif()
    endif()
    if(NOT DEFINED BALLS_ROCM_DEVICE_LIB_DIR)
        if(EXISTS "/usr/lib64/rocm/llvm/lib/clang/20/lib/amdgcn/bitcode")
            set(BALLS_ROCM_DEVICE_LIB_DIR "/usr/lib64/rocm/llvm/lib/clang/20/lib/amdgcn/bitcode" CACHE PATH "" FORCE)
        elseif(EXISTS "/opt/rocm/lib/amdgcn/bitcode")
            set(BALLS_ROCM_DEVICE_LIB_DIR "/opt/rocm/lib/amdgcn/bitcode" CACHE PATH "" FORCE)
        endif()
    endif()
    if(EXISTS "${BALLS_ROCM_CLANG}")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(ROCM_KERNEL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/rocm/BallKernels.cl")
        set(ROCM_KERNEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/BallKernelImages_rocm.h")
        add_custom_command(
            OUTPUT "${ROCM_KERNEL_HEADER}"
            COMMAND ${Python3_EXECUTABLE} "${BALLS_SOURCE_DIR}/../scripts/CompileRocmKernels.py"
                    --clang "${BALLS_ROCM_CLANG}"
                    --dev-libs "${BALLS_ROCM_DEVICE_LIB_DIR}"
                    --source "${ROCM_KERNEL_SOURCE}"
                    --output "${ROCM_KERNEL_HEADER}"
                    --targets "gfx1030,gfx1031"
            DEPENDS "${ROCM_KERNEL_SOURCE}" "${BALLS_SOURCE_DIR}/../scripts/CompileRocmKernels.py"
            COMMENT "Compiling ROCm OpenCL kernels for gfx1030, gfx1031"
        )
        add_custom_target(balls_rocm_kernels DEPENDS "${ROCM_KERNEL_HEADER}")

        add_library(balls_physics_rocm SHARED
            rocm/RocmBackend.cpp
            rocm/RocmDriver.cpp
            rocm/RocmPlugin.cpp
            ${BALLS_SOURCE_DIR}/core/common/DynamicLibrary.cpp
        )
        target_include_directories(balls_physics_rocm PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            "${BALLS_ROCM_SDK_DIR}/include"
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
            ${BALLS_SOURCE_DIR}/core
            ${BALLS_SOURCE_DIR}/backends
            ${BALLS_SOURCE_DIR}
        )
        target_link_libraries(balls_physics_rocm PRIVATE balls_physics_interface)
        target_compile_options(balls_physics_rocm PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
        balls_precompile(balls_physics_rocm)
        target_link_options(balls_physics_rocm PRIVATE ${BALLS_LINK_OPTIMIZE})
        add_dependencies(balls_physics_rocm balls_rocm_kernels)
        list(APPEND BALLS_PHYSICS_PLUGINS balls_physics_rocm)
    endif()
endif()

add_custom_target(balls_physics_plugins DEPENDS ${BALLS_PHYSICS_PLUGINS})
