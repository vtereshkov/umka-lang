cd tests

cd lib
cmd /r build_lib_windows_mingw.bat
cd ..

..\umka_windows_mingw\umka all.um > actual.log
..\umka_windows_mingw\umka compare.um actual.log expected.log
cd .. 
