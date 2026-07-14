file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
add_library(lib SHARED ${CMAKE_CURRENT_SOURCE_DIR}/lib/lib.c)
set_target_properties(lib PROPERTIES PREFIX "")
add_custom_command(
    TARGET lib
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lib>
        ${CMAKE_CURRENT_BINARY_DIR}/lib/lib.umi)

if(UNIX)
    add_test(
        NAME umka_tests
        COMMAND
            sh -c
            "$<TARGET_FILE:umka_exe> -warn all.um > actual.log && $<TARGET_FILE:umka_exe> -warn compare.um actual.log ${CMAKE_CURRENT_SOURCE_DIR}/expected.log"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
else()
    add_test(
        NAME umka_tests
        COMMAND
            cmd /c
            "$<TARGET_FILE:umka_exe> -warn all.um > actual.log && $<TARGET_FILE:umka_exe> -warn compare.um actual.log ${CMAKE_CURRENT_SOURCE_DIR}/expected.log"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
