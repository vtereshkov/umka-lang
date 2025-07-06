set opts=-s -O3
if "%1"=="-debug" set opts=-g3

cd src

gcc %opts% -malign-double -fno-strict-aliasing -fvisibility=hidden -DUMKA_BUILD -DUMKA_EXT_LIBS -Wall -Wno-format-security -c umka_api.c umka_common.c umka_compiler.c umka_const.c umka_decl.c umka_expr.c umka_gen.c umka_ident.c umka_lexer.c umka_runtime.c umka_stmt.c umka_types.c umka_vm.c 
gcc %opts% -shared -Wl,--output-def=libumka.def -Wl,--out-implib=libumka.a -Wl,--dll *.o -o libumka.dll -static-libgcc -static
ar rcs libumka_static_windows.a *.o

gcc %opts% -malign-double -fno-strict-aliasing -Wall -c umka.c 
gcc %opts% umka.o -o umka.exe -static-libgcc -static -L%cd% -lm -lumka

del *.o
cd ..

mkdir umka_windows_mingw

move /y src\libumka* umka_windows_mingw
move /y src\umka.exe umka_windows_mingw
copy src\umka_api.h umka_windows_mingw
copy LICENSE umka_windows_mingw

mkdir umka_windows_mingw\examples
mkdir umka_windows_mingw\examples\3dcam
mkdir umka_windows_mingw\examples\fractal
mkdir umka_windows_mingw\examples\lisp
mkdir umka_windows_mingw\examples\raytracer
mkdir umka_windows_mingw\doc
mkdir umka_windows_mingw\editors

copy examples\3dcam\*.* umka_windows_mingw\examples\3dcam
copy examples\fractal\*.* umka_windows_mingw\examples\fractal
copy examples\lisp\*.* umka_windows_mingw\examples\lisp
copy examples\raytracer\*.* umka_windows_mingw\examples\raytracer
copy doc\*.* umka_windows_mingw\doc
copy editors\*.* umka_windows_mingw\editors
