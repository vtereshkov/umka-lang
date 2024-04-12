cd tests

cd lib
cmd /r build_lib_windows_mingw.bat
cd ..

..\umka_windows_mingw\umka -warn all.um > actual.log
..\umka_windows_mingw\umka -warn compare.um actual.log expected.log
cd ..

cd benchmarks
..\umka_windows_mingw\umka -warn allbench.um > actual.log
..\umka_windows_mingw\umka -warn ..\tests\compare.um actual.log expected.log
cd ..
