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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(balls_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(balls_imgui PUBLIC SDL3::SDL3-static)

add_executable(ColorBlastBallsSdl WIN32
    app/Main.cpp
    app/App.cpp
    app/Input.cpp
    app/Paths.cpp
    app/LaunchOptions.cpp
    app/FrameTimings.cpp
    app/MachineProfile.cpp
    control/ControlProtocol.cpp
    control/ControlServer.cpp
    render/SdlGpuRenderer.cpp
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
target_include_directories(ColorBlastBallsSdl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ColorBlastBallsSdl PRIVATE balls_core balls_imgui SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(ColorBlastBallsSdl PRIVATE ws2_32)
endif()
target_compile_options(ColorBlastBallsSdl PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(ColorBlastBallsSdl PRIVATE ${BALLS_LINK_OPTIMIZE})
option(BALLS_USE_SOURCE_ASSETS "Read and save levels in the source tree (turn OFF for release packages)" ON)
if(BALLS_USE_SOURCE_ASSETS)
    target_compile_definitions(ColorBlastBallsSdl PRIVATE BALLS_DEV_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
endif()
add_dependencies(ColorBlastBallsSdl balls_physics_plugins)
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET ColorBlastBallsSdl POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "$<TARGET_FILE_DIR:ColorBlastBallsSdl>")
    endif()
endif()
add_custom_command(TARGET ColorBlastBallsSdl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ColorBlastBallsSdl>/assets")
