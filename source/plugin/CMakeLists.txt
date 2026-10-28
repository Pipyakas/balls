# Flat C facade for hosts that own their own main loop. The core links in statically; the physics backends stay
# plugin DLLs beside it, so a host folder holds balls_unity.dll plus balls_physics_*.dll (see
# balls_set_plugin_directory, which tells the core where to look).
add_library(balls_unity SHARED BallsUnityApi.cpp)
target_link_libraries(balls_unity PRIVATE balls_core)
target_compile_options(balls_unity PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(balls_unity PRIVATE ${BALLS_LINK_OPTIMIZE})
add_dependencies(balls_unity balls_physics_plugins)

# Stage the backend plugins beside the facade so a front end can copy one folder.
add_custom_command(TARGET balls_unity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:balls_unity>/balls_plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:balls_physics_cpu>" "$<TARGET_FILE_DIR:balls_unity>/balls_plugins/"
    COMMENT "Staging physics plugins beside balls_unity")
