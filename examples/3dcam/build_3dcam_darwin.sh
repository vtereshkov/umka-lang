#!/bin/sh

cp ../../umka_darwin/libumka.dylib .
cp ../../umka_darwin/umka_api.h .

clang 3dcam.c -o 3dcam \
    -L$PWD \
    -lraylib -lumka \
    -Wl,-rpath,'$ORIGIN'

