add_library(balls_physics_vulkan SHARED
    VulkanBackend.cpp
    VulkanComputeDevice.cpp
    VulkanPlugin.cpp
    ${CMAKE_SOURCE_DIR}/core/common/DynamicLibrary.cpp
    ${CMAKE_SOURCE_DIR}/core/gpu/GpuAdapters.cpp
    ${CMAKE_SOURCE_DIR}/tools/gpucompute/BallStepVariants.cpp
)
target_include_directories(balls_physics_vulkan PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/physics
    ${CMAKE_SOURCE_DIR}/tools/gpucompute
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(balls_physics_vulkan PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
if(WIN32)
    set_source_files_properties(VulkanBackend.cpp VulkanComputeDevice.cpp VulkanPlugin.cpp
        PROPERTIES COMPILE_DEFINITIONS "NOMINMAX;WIN32_LEAN_AND_MEAN")
endif()
target_link_libraries(balls_physics_vulkan PRIVATE balls_physics_interface)
if(TARGET Vulkan::Headers)
    target_link_libraries(balls_physics_vulkan PRIVATE Vulkan::Headers)
else()
    target_link_libraries(balls_physics_vulkan PRIVATE Vulkan-Headers)
endif()
if(MSVC)
    target_link_libraries(balls_physics_vulkan PRIVATE nvrhi nvrhi_vk)
    set_property(TARGET balls_physics_vulkan PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    target_link_libraries(balls_physics_vulkan PRIVATE $<LINK_GROUP:RESCAN,nvrhi,nvrhi_vk>)
endif()
target_compile_options(balls_physics_vulkan PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(balls_physics_vulkan PRIVATE ${BALLS_LINK_OPTIMIZE})
add_dependencies(balls_physics_plugins balls_physics_vulkan)
