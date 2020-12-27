#!/bin/sh

gcc -fPIC -O3 -c lib.c
gcc -shared -fPIC -static-libgcc *.o -o lib.umi
rm -f *.o
