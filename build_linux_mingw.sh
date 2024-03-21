#!/bin/sh -e

gccwflags="-Wall -Wno-format-security"
gccflags="-s -fPIC -O3 -malign-double -fno-strict-aliasing -fvisibility=hidden -DUMKA_BUILD -DUMKA_EXT_LIBS $gccwflags"
sourcefiles="umka_api.c umka_common.c umka_compiler.c umka_const.c   umka_decl.c umka_expr.c
             umka_gen.c umka_ident.c  umka_lexer.c    umka_runtime.c umka_stmt.c umka_types.c umka_vm.c"

rm umka_windows_mingw -rf # remove previous build

cd src

rm -f *.o
rm -f *.a

x86_64-w64-mingw32-gcc $gccflags -c $sourcefiles
x86_64-w64-mingw32-gcc -s -shared -fPIC -static-libgcc *.o -o libumka.dll -lm

x86_64-w64-mingw32-gcc $gccflags -c umka.c
x86_64-w64-mingw32-gcc -s umka.o -o umka.exe -static-libgcc -lm -L. -lumka -Wl,-rpath,'$ORIGIN'
ar rcs libumka_static.a *.o

rm -f *.o
rm -f *.a

cd ..

mkdir umka_windows_mingw/examples/3dcam -p
mkdir umka_windows_mingw/examples/fractal -p
mkdir umka_windows_mingw/examples/lisp -p
mkdir umka_windows_mingw/examples/raytracer -p
mkdir umka_windows_mingw/doc

mv src/libumka* src/umka.exe umka_windows_mingw/
cp src/umka_api.h Umka.sublime-syntax LICENSE umka_windows_mingw/

cp examples/* umka_windows_mingw/examples -r
cp doc/* umka_windows_mingw/doc

echo Build successful
