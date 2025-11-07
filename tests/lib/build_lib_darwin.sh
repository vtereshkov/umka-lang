#!/bin/sh

clang -fPIC -O3 -c lib.c
clang -dynamiclib -fPIC *.o -o lib.umi
rm -f *.o

