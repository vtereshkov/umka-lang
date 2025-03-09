#!/bin/sh

./build_linux.sh
./build_linux_mingw.sh

cp umka_linux/umka .
cp umka_linux/libumka.so .
cp umka_linux/libumka_static_linux.a .

cp umka_windows_mingw/umka.exe .
cp umka_windows_mingw/libumka.dll .
cp umka_windows_mingw/libumka_static_windows.a .

cp src/umka_api.h .
