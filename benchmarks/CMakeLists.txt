file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
if(UNIX)
    add_test(
        NAME umka_benchmarks
        COMMAND
            sh -c
            "$<TARGET_FILE:umka_exe> -warn allbench.um > actual.log && $<TARGET_FILE:umka_exe> -warn ${CMAKE_SOURCE_DIR}/tests/compare.um actual.log ${CMAKE_CURRENT_SOURCE_DIR}/expected.log"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/benchmarks)
else()
    add_test(
        NAME umka_benchmarks
        COMMAND
            cmd /c
            "$<TARGET_FILE:umka_exe> -warn allbench.um > actual.log && $<TARGET_FILE:umka_exe> -warn ${CMAKE_SOURCE_DIR}/tests/compare.um actual.log ${CMAKE_CURRENT_SOURCE_DIR}/expected.log"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/benchmarks)
endif()
