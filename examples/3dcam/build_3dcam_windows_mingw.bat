copy ..\..\libumka.dll .
copy ..\..\libumka.a .
copy ..\..\umka_api.h .

gcc 3dcam.c -o 3dcam.exe -lumka -lraylib -L%cd%
