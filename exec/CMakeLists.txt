cmake_minimum_required(VERSION 3.16)
project(umka)

add_executable(umka-cli
        ${SOURCE_DIR}/umka.c)
target_link_libraries(umka-cli umka m)