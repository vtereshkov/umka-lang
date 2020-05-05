#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>

#include "umka_api.h"


// Umka extension example
void meow(UmkaStackSlot *params, UmkaStackSlot *result)
{
    int i = params[0].intVal;
    printf("Meow! (%d)\n", i);
    result->intVal = i;
}


int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Umka interpreter (C) Vasiliy Tereshkov, 2020\n");
        printf("Usage: umka <file.um>\n");
        return 1;
    }

    int res = umkaInit(argv[1], 1024 * 1024, argc, argv);

    if (res == 0)
    {
        umkaAddFunc("meow", &meow);
        res = umkaCompile();
    }

    if (res == 0)
    {
        res = umkaRun(1024 * 1024);

        if (res != 0)
        {
            UmkaError error;
            umkaGetError(&error);
            printf("Runtime error %s (%d): %s\n", error.fileName, error.line, error.msg);
        }
    }
    else
    {
        UmkaError error;
        umkaGetError(&error);
        printf("Error %s (%d, %d): %s\n", error.fileName, error.line, error.pos, error.msg);
    }

    if (res == 0) res = umkaFree();

    return res;
}

