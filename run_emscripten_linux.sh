#!/bin/sh
emcc -O3 -malign-double -fno-strict-aliasing -fvisibility=hidden -DUMKA_STATIC -sSINGLE_FILE -sASYNCIFY -sSTACK_SIZE=5MB -sALLOW_MEMORY_GROWTH -sEXPORTED_FUNCTIONS=_runPlayground -sEXPORTED_RUNTIME_METHODS=ccall,cwrap -Wall -Wno-format-security -o umka.js umka.c umka_api.c umka_common.c umka_compiler.c umka_const.c umka_decl.c umka_expr.c umka_gen.c umka_ident.c umka_lexer.c umka_runtime.c umka_stmt.c umka_types.c umka_vm.c 
