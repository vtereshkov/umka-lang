#!/bin/sh

./build_linux.sh
./build_linux_mingw.sh
cp umka_linux/umka .
cp umka_linux/libumka.so .
cp umka_windows_mingw/umka.exe .
cp umka_windows_mingw/libumka.dll .
cp src/umka_api.h .
