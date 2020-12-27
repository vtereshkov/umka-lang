gcc -O3 -c lib.c
gcc -shared -Wl,--dll *.o -o lib.umi -static-libgcc -static  
del *.o
