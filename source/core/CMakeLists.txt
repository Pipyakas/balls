set(BALLS_CORE_SOURCES
    common/BallColor.cpp
    common/DynamicLibrary.cpp
    common/LoadProgress.cpp
    common/ParallelChunks.cpp
    common/WorkerPool.cpp
    gpu/GpuAdapters.cpp
    level/LevelData.cpp
    level/LevelParser.cpp
    level/SectionText.cpp
    level/GameplayRules.cpp
    level/LevelWriter.cpp
    level/LevelRepository.cpp
    level/LevelWatcher.cpp
    level/LevelImporter.cpp
    sim/BrickBoard.cpp
    sim/BrickCellImage.cpp
    sim/TargetField.cpp
    sim/Simulation.cpp
    sim/BackendFactory.cpp
    sim/DeviceInventory.cpp
    sim/DeviceComposer.cpp
    sim/BackendSelection.cpp
    sim/BackendWorker.cpp
    sim/HybridBackend.cpp
    sim/BackendBenchmark.cpp
    sim/PhysicsAccuracy.cpp
    sim/ReferenceBackend.cpp
    game/AdditionGovernor.cpp
    game/LauncherQueue.cpp
    game/SpawnController.cpp
    game/SpawnPacer.cpp
    level/EdgeCaseLevels.cpp
    sim/EdgeCaseSweep.cpp
    game/GameSession.cpp
    game/SessionRunner.cpp
    game/BoardSnapshots.cpp
    game/SessionHost.cpp
    game/Settings.cpp
    game/MachineProfile.cpp
    game/GameMode.cpp
    control/ControlProtocol.cpp
    control/ControlApi.cpp
    game/FrameTimings.cpp
    ui/GameplayLayout.cpp
    ui/LoadingScreen.cpp
    ui/MenuModel.cpp
    ui/ScreenFlow.cpp
    ui/Theme.cpp
    editor/EditorSession.cpp
    editor/LevelEditor.cpp
)
if(EMSCRIPTEN)
    list(REMOVE_ITEM BALLS_CORE_SOURCES
        common/DynamicLibrary.cpp
        sim/HybridBackend.cpp
        sim/DeviceComposer.cpp
        sim/BackendFactory.cpp)
    list(APPEND BALLS_CORE_SOURCES sim/BackendFactoryWeb.cpp)
endif()
add_library(balls_core STATIC ${BALLS_CORE_SOURCES})
target_include_directories(balls_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_compile_features(balls_core PUBLIC cxx_std_20)
set_target_properties(balls_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(balls_core PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
balls_precompile(balls_core)
target_link_options(balls_core INTERFACE ${BALLS_LINK_OPTIMIZE})
if(EMSCRIPTEN)
    target_include_directories(balls_core PRIVATE ${CMAKE_SOURCE_DIR}/source/backends)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(balls_core PUBLIC Threads::Threads PRIVATE ${CMAKE_DL_LIBS})
endif()

add_library(balls_physics_interface INTERFACE)
target_include_directories(balls_physics_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(balls_physics_interface INTERFACE cxx_std_20)

install(TARGETS balls_core ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")
