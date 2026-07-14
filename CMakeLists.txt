cmake_minimum_required(VERSION 4.4)
project(
  cli
  VERSION 1.5.6
  DESCRIPTION "Umka: a statically typed embeddable scripting language "
  LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
add_subdirectory(src)
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
