copy import\embed\umka_runtime_src.h src

cd src
cmd /c ..\run_emscripten.bat 
cd ..

mkdir playground
move /y src\umka.js playground

