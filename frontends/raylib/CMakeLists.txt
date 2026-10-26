include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(raylib)

add_executable(ColorBlastBalls WIN32
    app/Main.cpp
    app/App.cpp
    app/Paths.cpp
    app/LaunchOptions.cpp
    app/FrameTimings.cpp
    app/MachineProfile.cpp
    control/ControlProtocol.cpp
    control/ControlServer.cpp
    render/Palette.cpp
    render/BallRenderer.cpp
    render/GpuTimer.cpp
    render/GlExtras.cpp
    render/BoardRenderer.cpp
    render/LauncherRenderer.cpp
    render/ScreenLayout.cpp
    ui/Widgets.cpp
    screens/MainMenuScreen.cpp
    screens/LevelSelectScreen.cpp
    screens/GameplayScreen.cpp
    screens/DebugPanel.cpp
    editor/MapEditorScreen.cpp
)
target_include_directories(ColorBlastBalls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ColorBlastBalls PRIVATE balls_core raylib ws2_32)
target_compile_options(ColorBlastBalls PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(ColorBlastBalls PRIVATE ${BALLS_LINK_OPTIMIZE})
if(MSVC)
    target_link_options(ColorBlastBalls PRIVATE /ENTRY:mainCRTStartup)
endif()
option(BALLS_USE_SOURCE_ASSETS "Read and save levels in the source tree (turn OFF for release packages)" ON)
if(BALLS_USE_SOURCE_ASSETS)
    target_compile_definitions(ColorBlastBalls PRIVATE BALLS_DEV_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
endif()
set_target_properties(ColorBlastBalls PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_command(TARGET ColorBlastBalls POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ColorBlastBalls>/assets")
