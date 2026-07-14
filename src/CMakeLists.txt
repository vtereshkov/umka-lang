set(sources
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_compiler.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_const.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_decl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_expr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_gen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_ident.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_lexer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_stmt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_types.c
    ${CMAKE_CURRENT_SOURCE_DIR}/umka_vm.c)
add_library(umka_lib ${sources})
target_compile_definitions(umka_lib PRIVATE UMKA_BUILD UMKA_EXT_LIBS)
target_include_directories(umka_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(LINUX)
    target_link_libraries(umka_lib PUBLIC m)
endif()
add_custom_command(
    TARGET umka_lib
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:umka_lib>
        ${CMAKE_CURRENT_BINARY_DIR})

add_executable(umka_exe)
set_target_properties(
    umka_exe
    PROPERTIES
        BUILD_RPATH "$ORIGIN:$ORIGIN/../lib" INSTALL_RPATH
        "$ORIGIN:$ORIGIN/../lib")
target_sources(umka_exe PRIVATE ${sources} ${CMAKE_CURRENT_SOURCE_DIR}/umka.c)
target_compile_definitions(umka_exe PRIVATE UMKA_BUILD UMKA_EXT_LIBS)
target_link_libraries(umka_exe PRIVATE umka_lib)

include(GNUInstallDirs)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.h"
)
set_target_properties(umka_lib PROPERTIES OUTPUT_NAME umka)
install(
    TARGETS umka_lib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set_target_properties(umka_exe PROPERTIES OUTPUT_NAME umka)
install(TARGETS umka_exe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(${CMAKE_SOURCE_DIR}/packaging/umka.pc.in umka.pc @ONLY)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/umka.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
