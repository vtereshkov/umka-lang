cd src

gcc -O3 -Wall -Wno-format-security -c umka_api.c umka_common.c umka_compiler.c umka_const.c umka_decl.c umka_expr.c umka_gen.c umka_ident.c umka_lexer.c umka_runtime.c umka_stmt.c umka_types.c umka_vm.c 
gcc -shared -Wl,--output-def=libumka.def -Wl,--out-implib=libumka.a -Wl,--dll *.o -o libumka.dll -static-libgcc -static  

gcc -O3 -Wall -Wno-format-security -c umka.c 
gcc umka.o -o umka.exe -static-libgcc -static -L%cd% -lm -lumka 

del *.o
cd ..

mkdir umka_windows_mingw

move /y src\libumka.* umka_windows_mingw
move /y src\umka.exe umka_windows_mingw
copy src\umka_api.h umka_windows_mingw

mkdir umka_windows_mingw\examples
mkdir umka_windows_mingw\import
 
copy examples\*.* umka_windows_mingw\examples
copy import\*.* umka_windows_mingw\import
