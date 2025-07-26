#!/bin/sh

clangwflags="-Wall -Wno-format-security"
clangflags="-fPIC -O3 -malign-double -fno-strict-aliasing -fvisibility=hidden -DUMKA_BUILD -DUMKA_EXT_LIBS $clangwflags"
sourcefiles="umka_api.c umka_common.c umka_compiler.c umka_const.c   umka_decl.c umka_expr.c
             umka_gen.c umka_ident.c  umka_lexer.c    umka_runtime.c umka_stmt.c umka_types.c umka_vm.c"

[ -d "umka_darwin" ] && rm -rf umka_darwin

cd src

rm -f *.o
rm -f *.a

clang $clangflags -c $sourcefiles
clang -shared -fPIC *.o -o libumka.dylib -lm -ldl
libtool -static -o libumka_static_darwin.a *.o

clang $clangflags -c umka.c
clang umka.o -o umka -L$PWD -lm -lumka -Wl,-rpath,'$ORIGIN'

rm -f *.o

cd ..

mkdir -p umka_darwin/examples/3dcam -p
mkdir -p umka_darwin/examples/fractal -p
mkdir -p umka_darwin/examples/lisp -p
mkdir -p umka_darwin/examples/raytracer -p
mkdir -p umka_darwin/doc
mkdir -p umka_darwin/editors

mv src/libumka* src/umka umka_darwin/
cp src/umka_api.h LICENSE umka_darwin/

cp -r examples/* umka_darwin/examples
cp doc/* umka_darwin/doc
cp editors/* umka_darwin/editors

echo Build finished
