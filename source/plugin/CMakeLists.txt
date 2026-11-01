add_library(balls_unity SHARED BallsUnityApi.cpp)
target_link_libraries(balls_unity PRIVATE balls_core)
target_compile_options(balls_unity PRIVATE ${BALLS_WARNINGS} ${BALLS_OPTIMIZE})
target_link_options(balls_unity PRIVATE ${BALLS_LINK_OPTIMIZE})
add_dependencies(balls_unity balls_physics_plugins)

add_custom_command(TARGET balls_unity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:balls_unity>/balls_plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:balls_physics_cpu>" "$<TARGET_FILE_DIR:balls_unity>/balls_plugins/"
    COMMENT "Staging physics plugins beside balls_unity")
