include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.30.tar.gz)
FetchContent_MakeAvailable(SDL3)
if(SDL_SHARED)
    set(BALLS_SDL3_LINK_TARGET SDL3::SDL3)
else()
    set(BALLS_SDL3_LINK_TARGET ${BALLS_SDL3_LINK_TARGET})
endif()

if(ANDROID)
    set(BALLS_DEFAULT_RENDERER "webgpu")
elseif(EMSCRIPTEN OR IOS)
    set(BALLS_DEFAULT_RENDERER "sdlgpu")
else()
    set(BALLS_DEFAULT_RENDERER "nvrhi")
endif()
set(BALLS_RENDERER "${BALLS_DEFAULT_RENDERER}" CACHE STRING "Renderer behind IRenderer: nvrhi (D3D12/Vulkan), sdlgpu, or webgpu (native Dawn spike)")
set_property(CACHE BALLS_RENDERER PROPERTY STRINGS nvrhi sdlgpu webgpu)

option(BALLS_WITH_WEBGPU "Build WebGPU desktop renderer alongside NVRHI" OFF)

if(BALLS_RENDERER STREQUAL "nvrhi")
    set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX11 ${WIN32} CACHE BOOL "" FORCE)
    set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
    set(NVRHI_WITH_VALIDATION OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nvrhi
        GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
        GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(nvrhi)
endif()

if(BALLS_WITH_WEBGPU OR BALLS_RENDERER STREQUAL "webgpu")
    if(ANDROID)
        set(WGPU_PREBUILT_DIR "${CMAKE_SOURCE_DIR}/third_party/wgpu-native")
        add_library(wgpu_native_lib INTERFACE)
        target_include_directories(wgpu_native_lib INTERFACE "${WGPU_PREBUILT_DIR}/include")
        target_link_directories(wgpu_native_lib INTERFACE "${WGPU_PREBUILT_DIR}/android/${ANDROID_ABI}")
        target_link_libraries(wgpu_native_lib INTERFACE wgpu_native)
    else()
        set(WGPU_NATIVE_VERSION "v24.0.3.1")
        if(WIN32)
            set(WGPU_ARCHIVE_URL "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/wgpu-windows-x86_64-msvc-release.zip")
        elseif(APPLE)
            set(WGPU_ARCHIVE_URL "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/wgpu-macos-aarch64-release.zip")
        else()
            set(WGPU_ARCHIVE_URL "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/wgpu-linux-x86_64-release.zip")
        endif()
        FetchContent_Declare(wgpu_native
            URL ${WGPU_ARCHIVE_URL}
            )
        FetchContent_MakeAvailable(wgpu_native)

        add_library(wgpu_native_lib INTERFACE)
        target_include_directories(wgpu_native_lib INTERFACE ${wgpu_native_SOURCE_DIR}/include)
        if(WIN32)
            target_link_libraries(wgpu_native_lib INTERFACE ${wgpu_native_SOURCE_DIR}/lib/wgpu_native.dll.lib)
        else()
            target_link_libraries(wgpu_native_lib INTERFACE ${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.so)
        endif()
    endif()
endif()

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz)
FetchContent_MakeAvailable(imgui)

add_library(balls_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
if(BALLS_RENDERER STREQUAL "sdlgpu")
    target_sources(balls_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
endif()
target_include_directories(balls_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(balls_imgui PUBLIC ${BALLS_SDL3_LINK_TARGET})

if(ANDROID)
    set(BALLS_SDL_TARGET main)
else()
    set(BALLS_SDL_TARGET ${BALLS_SDL_TARGET})
endif()

set(BALLS_SDL_SOURCES
    app/Main.cpp
    app/App.cpp
    app/Input.cpp
    app/Paths.cpp
    app/LaunchOptions.cpp
    app/FrameTimings.cpp
    app/MachineProfile.cpp
    control/ControlProtocol.cpp
    control/ControlServer.cpp
    render/RendererFactory.cpp
    render/BallView.cpp
    render/Palette.cpp
    render/BoardRenderer.cpp
    render/LauncherRenderer.cpp
    render/ScreenLayout.cpp
    ui/Painter.cpp
    ui/Widgets.cpp
    screens/MainMenuScreen.cpp
    screens/LevelSelectScreen.cpp
    screens/GameplayScreen.cpp
    screens/DebugPanel.cpp
    editor/MapEditorScreen.cpp
)
if(ANDROID)
    add_library(main SHARED ${BALLS_SDL_SOURCES})
    set_target_properties(main PROPERTIES OUTPUT_NAME main)
else()
    add_executable(${BALLS_SDL_TARGET} WIN32 ${BALLS_SDL_SOURCES})
endif()
target_include_directories(${BALLS_SDL_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(BALLS_RENDERER STREQUAL "nvrhi")
    target_sources(${BALLS_SDL_TARGET} PRIVATE
        render/nvrhi/NvrhiRenderer.cpp
        render/nvrhi/GraphicsDevice.cpp
        render/nvrhi/VulkanDevice.cpp
        render/nvrhi/ImguiNvrhi.cpp
        render/nvrhi/ShaderBytecode.cpp)
    target_compile_definitions(${BALLS_SDL_TARGET} PRIVATE BALLS_RENDERER_NVRHI)
    if(MSVC)
        target_link_libraries(${BALLS_SDL_TARGET} PRIVATE nvrhi nvrhi_vk)
    else()
        # nvrhi and nvrhi_vk are static archives with cyclic references, so the GNU and Clang
        # linkers need them rescanned rather than linked in file order.
        target_link_libraries(${BALLS_SDL_TARGET} PRIVATE $<LINK_GROUP:RESCAN,nvrhi,nvrhi_vk>)
    endif()
    if(TARGET Vulkan::Headers)
        target_link_libraries(${BALLS_SDL_TARGET} PRIVATE Vulkan::Headers)
    else()
        target_link_libraries(${BALLS_SDL_TARGET} PRIVATE Vulkan-Headers)
    endif()
    if(WIN32)
        target_sources(${BALLS_SDL_TARGET} PRIVATE render/nvrhi/D3d12Device.cpp)
        target_link_libraries(${BALLS_SDL_TARGET} PRIVATE nvrhi_d3d12 dxgi d3d12)
    endif()
elseif(BALLS_RENDERER STREQUAL "webgpu")
    target_sources(${BALLS_SDL_TARGET} PRIVATE render/webgpu/WebGpuRenderer.cpp)
    target_compile_definitions(${BALLS_SDL_TARGET} PRIVATE BALLS_RENDERER_WEBGPU)
    target_link_libraries(${BALLS_SDL_TARGET} PRIVATE wgpu_native_lib)
    if(WIN32)
        add_custom_command(TARGET ${BALLS_SDL_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${wgpu_native_SOURCE_DIR}/lib/wgpu_native.dll"
                "$<TARGET_FILE_DIR:${BALLS_SDL_TARGET}>")
    endif()
else()
    target_sources(${BALLS_SDL_TARGET} PRIVATE render/SdlGpuRenderer.cpp)
endif()

if(BALLS_WITH_WEBGPU AND NOT BALLS_RENDERER STREQUAL "webgpu")
    target_sources(${BALLS_SDL_TARGET} PRIVATE render/webgpu/WebGpuRenderer.cpp)
    target_compile_definitions(${BALLS_SDL_TARGET} PRIVATE BALLS_WITH_WEBGPU)
    target_link_libraries(${BALLS_SDL_TARGET} PRIVATE wgpu_native_lib)
    if(WIN32)
        add_custom_command(TARGET ${BALLS_SDL_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${wgpu_native_SOURCE_DIR}/lib/wgpu_native.dll"
                "$<TARGET_FILE_DIR:${BALLS_SDL_TARGET}>")
    endif()
endif()

target_link_libraries(${BALLS_SDL_TARGET} PRIVATE balls_core balls_imgui ${BALLS_SDL3_LINK_TARGET})
if(ANDROID)
    target_sources(${BALLS_SDL_TARGET} PRIVATE
        ${CMAKE_SOURCE_DIR}/source/backends/cpu/CpuBallStore.cpp
        ${CMAKE_SOURCE_DIR}/source/backends/cpu/CompactBallStore.cpp
        ${CMAKE_SOURCE_DIR}/source/backends/cpu/ScalarBackend.cpp
        ${CMAKE_SOURCE_DIR}/source/backends/cpu/WorkerPool.cpp
        ${CMAKE_SOURCE_DIR}/source/backends/cpu/CpuPlugin.cpp
    )
    target_include_directories(${BALLS_SDL_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/source/backends)
    target_compile_definitions(${BALLS_SDL_TARGET} PRIVATE BALLS_STATIC_CPU_BACKEND=1)
    target_link_libraries(${BALLS_SDL_TARGET} PRIVATE android)
endif()
if(WIN32)
    target_link_libraries(${BALLS_SDL_TARGET} PRIVATE ws2_32)
endif()
target_compile_options(${BALLS_SDL_TARGET} PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(${BALLS_SDL_TARGET} PRIVATE ${BALLS_LINK_OPTIMIZE})
if(ANDROID)
    set(BALLS_USE_SOURCE_ASSETS OFF CACHE BOOL "" FORCE)
endif()
option(BALLS_USE_SOURCE_ASSETS "Read and save levels in the source tree (turn OFF for release packages)" ON)
if(BALLS_USE_SOURCE_ASSETS)
    target_compile_definitions(${BALLS_SDL_TARGET} PRIVATE BALLS_DEV_ASSET_DIR="${BALLS_ASSET_DIR}")
endif()
add_dependencies(${BALLS_SDL_TARGET} balls_physics_plugins)
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET ${BALLS_SDL_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "$<TARGET_FILE_DIR:${BALLS_SDL_TARGET}>")
    endif()
endif()
add_custom_command(TARGET ${BALLS_SDL_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${BALLS_ASSET_DIR}" "$<TARGET_FILE_DIR:${BALLS_SDL_TARGET}>/assets")
