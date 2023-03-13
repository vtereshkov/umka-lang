cd src

cl /nologo /O2 /MT /LD /Felibumka.dll /DUMKA_BUILD /DUMKA_EXT_LIBS umka_api.c umka_common.c umka_compiler.c umka_const.c umka_decl.c umka_expr.c umka_gen.c umka_ident.c umka_lexer.c umka_runtime.c umka_stmt.c umka_types.c umka_vm.c 
lib /nologo /out:libumka_static.lib *.obj

cl /nologo /O2 /MT /Feumka.exe umka.c libumka.lib

del *.obj
cd ..

mkdir umka_windows_msvc

move /y src\libumka* umka_windows_msvc
move /y src\umka.exe umka_windows_msvc
copy src\umka_api.h umka_windows_msvc
copy LICENSE umka_windows_msvc
copy Umka.sublime-syntax umka_windows_msvc

mkdir umka_windows_msvc\examples
mkdir umka_windows_msvc\examples\3dcam
mkdir umka_windows_msvc\examples\fractal
mkdir umka_windows_msvc\examples\lisp
mkdir umka_windows_msvc\examples\raytracer
mkdir umka_windows_msvc\doc

copy examples\3dcam\*.* umka_windows_msvc\examples\3dcam
copy examples\fractal\*.* umka_windows_msvc\examples\fractal
copy examples\lisp\*.* umka_windows_msvc\examples\lisp
copy examples\raytracer\*.* umka_windows_msvc\examples\raytracer
copy doc\*.* umka_windows_msvc\doc
