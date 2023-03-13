cd src
cmd /c ..\run_emscripten.bat 
cd ..

mkdir playground
move /y src\umka.js playground

