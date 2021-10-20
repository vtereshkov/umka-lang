set(LIBRARY_NAME "umkalang")
set(EXEC_NAME "umka")

set(Header_Files
        "umka_api.h"
        "umka_common.h"
        "umka_compiler.h"
        "umka_const.h"
        "umka_decl.h"
        "umka_expr.h"
        "umka_gen.h"
        "umka_ident.h"
        "umka_lexer.h"
        "umka_runtime.h"
        "umka_stmt.h"
        "umka_types.h"
        "umka_vm.h"
        "../import_embed/umka_runtime_src.h"
)
source_group("Header Files" FILES "${Header_Files}")

set(Source_Files
        "umka_api.c"
        "umka_common.c"
        "umka_compiler.c"
        "umka_const.c"
        "umka_decl.c"
        "umka_expr.c"
        "umka_gen.c"
        "umka_ident.c"
        "umka_lexer.c"
        "umka_runtime.c"
        "umka_stmt.c"
        "umka_types.c"
        "umka_vm.c"
)
source_group("Source Files" FILES "${Source_Files}")

add_library("${LIBRARY_NAME}" "${LIBRARY_TYPE_FLAG}" "${Header_Files}" "${Source_Files}")

target_include_directories(
        "${LIBRARY_NAME}"
        PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/import_embed>"
        "$<INSTALL_INTERFACE:include>"
)
set_target_properties(
        "${LIBRARY_NAME}"
        PROPERTIES
        LINKER_LANGUAGE
        C
)

# Symbol exporter
include(GenerateExportHeader)
set(_export_file "${CMAKE_CURRENT_SOURCE_DIR}/${LIBRARY_NAME}_export.h")
generate_export_header("${LIBRARY_NAME}" EXPORT_FILE_NAME "${_export_file}")

source_group("${EXEC_NAME} Source Files" FILES "${Source_Files}")
add_executable("${EXEC_NAME}" "umka.c")
target_link_libraries("${EXEC_NAME}" PRIVATE "${LIBRARY_NAME}")

# install rules


# setup the version numbering
set_property(TARGET "${LIBRARY_NAME}" PROPERTY VERSION "1.0.0")
set_property(TARGET "${LIBRARY_NAME}" PROPERTY SOVERSION "1")

set(installable_libs "${LIBRARY_NAME}" "${EXEC_NAME}" "${PROJECT_NAME}_compiler_flags")
install(FILES "${Header_Files}" DESTINATION "include")

if (TARGET "${DEPENDANT_LIBRARY}")
    list(APPEND installable_libs "${DEPENDANT_LIBRARY}")
endif ()
install(TARGETS ${installable_libs}
        DESTINATION "bin"
        EXPORT "${LIBRARY_NAME}Targets")
