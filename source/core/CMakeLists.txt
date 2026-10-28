add_library(balls_core STATIC
    common/BallColor.cpp
    common/DynamicLibrary.cpp
    gpu/GpuAdapters.cpp
    level/LevelData.cpp
    level/LevelParser.cpp
    level/LevelWriter.cpp
    level/LevelRepository.cpp
    level/LevelWatcher.cpp
    level/LevelImporter.cpp
    sim/BrickBoard.cpp
    sim/TargetField.cpp
    sim/Simulation.cpp
    sim/BackendFactory.cpp
    sim/HybridBackend.cpp
    sim/BackendBenchmark.cpp
    sim/PhysicsAccuracy.cpp
    sim/ReferenceBackend.cpp
    game/CapacityProbe.cpp
    game/LauncherQueue.cpp
    game/SpawnController.cpp
    game/SpawnPacer.cpp
    level/EdgeCaseLevels.cpp
    sim/EdgeCaseSweep.cpp
    game/GameSession.cpp
    game/Settings.cpp
    ui/GameplayLayout.cpp
    editor/LevelEditor.cpp
)
target_include_directories(balls_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_compile_features(balls_core PUBLIC cxx_std_20)
target_compile_options(balls_core PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
balls_precompile(balls_core)
target_link_options(balls_core INTERFACE ${BALLS_LINK_OPTIMIZE})
# WorkerPool needs threads and DynamicLibrary loads physics plugins with dlopen on POSIX.
find_package(Threads REQUIRED)
target_link_libraries(balls_core PUBLIC Threads::Threads PRIVATE ${CMAKE_DL_LIBS})

add_library(balls_physics_interface INTERFACE)
target_include_directories(balls_physics_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(balls_physics_interface INTERFACE cxx_std_20)

install(TARGETS balls_core ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")

