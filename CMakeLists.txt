cmake_minimum_required(VERSION 3.16)
project(umka_lang)

set(CMAKE_CXX_STANDARD 11)
set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

include_directories(${SOURCE_DIR})

add_subdirectory(exec)
add_subdirectory(lib)

