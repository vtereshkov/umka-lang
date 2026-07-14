find_package(PkgConfig REQUIRED)
pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)

file(
    COPY ${CMAKE_CURRENT_SOURCE_DIR}/fractal ${CMAKE_CURRENT_SOURCE_DIR}/lisp
    ${CMAKE_CURRENT_SOURCE_DIR}/raytracer DESTINATION
    ${CMAKE_CURRENT_BINARY_DIR})
add_executable(3dcam)
target_sources(3dcam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3dcam/3dcam.c)
target_link_libraries(3dcam PRIVATE umka_lib PkgConfig::RAYLIB)
add_custom_command(
    TARGET 3dcam
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/examples/3dcam/3dcam.um
        ${CMAKE_CURRENT_BINARY_DIR}/3dcam.um
    COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:umka_lib>
        ${CMAKE_CURRENT_BINARY_DIR})
