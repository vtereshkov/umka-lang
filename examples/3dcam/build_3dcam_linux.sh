#!/bin/sh

cp ../../libumka.so .
cp ../../umka_api.h .

gcc 3dcam.c -o 3dcam -lumka -lraylib -L$PWD -Wl,-rpath,'$ORIGIN'
