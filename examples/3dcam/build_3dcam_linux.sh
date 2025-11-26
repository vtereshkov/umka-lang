#!/bin/sh

cp ../../umka_api.h .
cp ../../libumka.so .
if [ $? -ne 0 ]; then
    echo "Use build_linux.sh in the root umka directory,"
    echo "then run the example in the created umka_linux directory."
    return 1
fi

gcc 3dcam.c -o 3dcam -lumka -lraylib -L$PWD -lm -Wl,-rpath,'$ORIGIN'
