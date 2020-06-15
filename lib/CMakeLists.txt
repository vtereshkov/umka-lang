cmake_minimum_required(VERSION 3.16)
project(umkalib)

add_library(umka STATIC
        ${SOURCE_DIR}/umka_api.c
        ${SOURCE_DIR}/umka_common.c
        ${SOURCE_DIR}/umka_compiler.c
        ${SOURCE_DIR}/umka_const.c
        ${SOURCE_DIR}/umka_decl.c
        ${SOURCE_DIR}/umka_expr.c
        ${SOURCE_DIR}/umka_gen.c
        ${SOURCE_DIR}/umka_ident.c
        ${SOURCE_DIR}/umka_lexer.c
        ${SOURCE_DIR}/umka_runtime.c
        ${SOURCE_DIR}/umka_stmt.c
        ${SOURCE_DIR}/umka_types.c
        ${SOURCE_DIR}/umka_vm.c)
