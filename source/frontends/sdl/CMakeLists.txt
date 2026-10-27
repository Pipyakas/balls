include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.30.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)

if(ANDROID OR EMSCRIPTEN OR IOS)
    set(BALLS_DEFAULT_RENDERER "sdlgpu")
else()
    set(BALLS_DEFAULT_RENDERER "nvrhi")
endif()
set(BALLS_RENDERER "${BALLS_DEFAULT_RENDERER}" CACHE STRING "Renderer behind IRenderer: nvrhi (D3D12/Vulkan), sdlgpu, or webgpu (native Dawn spike)")
set_property(CACHE BALLS_RENDERER PROPERTY STRINGS nvrhi sdlgpu webgpu)

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

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
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
target_link_libraries(balls_imgui PUBLIC SDL3::SDL3-static)

add_executable(ColorBlastBalls WIN32
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
target_include_directories(ColorBlastBalls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(BALLS_RENDERER STREQUAL "nvrhi")
    target_sources(ColorBlastBalls PRIVATE
        render/nvrhi/NvrhiRenderer.cpp
        render/nvrhi/GraphicsDevice.cpp
        render/nvrhi/VulkanDevice.cpp
        render/nvrhi/ImguiNvrhi.cpp
        render/nvrhi/ShaderBytecode.cpp)
    target_compile_definitions(ColorBlastBalls PRIVATE BALLS_RENDERER_NVRHI)
    if(MSVC)
        target_link_libraries(ColorBlastBalls PRIVATE nvrhi nvrhi_vk)
    else()
        # nvrhi and nvrhi_vk are static archives with cyclic references, so the GNU and Clang
        # linkers need them rescanned rather than linked in file order.
        target_link_libraries(ColorBlastBalls PRIVATE $<LINK_GROUP:RESCAN,nvrhi,nvrhi_vk>)
    endif()
    if(TARGET Vulkan::Headers)
        target_link_libraries(ColorBlastBalls PRIVATE Vulkan::Headers)
    else()
        target_link_libraries(ColorBlastBalls PRIVATE Vulkan-Headers)
    endif()
    if(WIN32)
        target_sources(ColorBlastBalls PRIVATE render/nvrhi/D3d12Device.cpp)
        target_link_libraries(ColorBlastBalls PRIVATE nvrhi_d3d12 dxgi d3d12)
    endif()
elseif(BALLS_RENDERER STREQUAL "webgpu")
    target_sources(ColorBlastBalls PRIVATE render/webgpu/WebGpuRenderer.cpp)
    target_compile_definitions(ColorBlastBalls PRIVATE BALLS_RENDERER_WEBGPU)
    # TODO(webgpu): FetchContent/add_subdirectory for Dawn (or wgpu-native import) goes here.
else()
    target_sources(ColorBlastBalls PRIVATE render/SdlGpuRenderer.cpp)
endif()
target_link_libraries(ColorBlastBalls PRIVATE balls_core balls_imgui SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(ColorBlastBalls PRIVATE ws2_32)
endif()
target_compile_options(ColorBlastBalls PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(ColorBlastBalls PRIVATE ${BALLS_LINK_OPTIMIZE})
option(BALLS_USE_SOURCE_ASSETS "Read and save levels in the source tree (turn OFF for release packages)" ON)
if(BALLS_USE_SOURCE_ASSETS)
    target_compile_definitions(ColorBlastBalls PRIVATE BALLS_DEV_ASSET_DIR="${BALLS_ASSET_DIR}")
endif()
add_dependencies(ColorBlastBalls balls_physics_plugins)
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET ColorBlastBalls POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "$<TARGET_FILE_DIR:ColorBlastBalls>")
    endif()
endif()
add_custom_command(TARGET ColorBlastBalls POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${BALLS_ASSET_DIR}" "$<TARGET_FILE_DIR:ColorBlastBalls>/assets")
