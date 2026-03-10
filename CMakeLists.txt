cmake_minimum_required(VERSION 3.20)
project(umka C)
file(GLOB UMKA_LIB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(umka STATIC ${UMKA_LIB_SOURCES})
target_compile_options(umka PRIVATE -fno-strict-aliasing)
target_compile_definitions(umka PRIVATE UMKA_BUILD UMKA_EXT_LIBS)
target_include_directories(umka PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(GNUInstallDirs)
install(TARGETS umka ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/src/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.h"
)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/packaging/umka.pc.in umka.pc @ONLY)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/umka.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
