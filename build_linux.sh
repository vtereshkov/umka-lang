#!/bin/sh

mkdir umka_linux
cd src

gcc -shared -fPIC -O3 -static-libgcc -Wall -Wno-format-security -o ../umka_linux/libumka.so umka_api.c umka_common.c umka_compiler.c umka_const.c umka_decl.c umka_expr.c umka_gen.c umka_ident.c umka_lexer.c umka_runtime.c umka_stmt.c umka_types.c umka_vm.c -lm -lumka

gcc -O3 -static-libgcc -Wall -Wno-format-security -o ../umka_linux/umka umka.c -lm -lumka -Wl,-rpath,'$ORIGIN'

rm -f *.o
cd ..

cp -r examples umka_linux/
cp -r import umka_linux/





