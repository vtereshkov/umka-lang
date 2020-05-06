#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (argc < 2)
    {
        printf("Umka interpreter (C) Vasiliy Tereshkov, 2020\n");
        printf("Usage: umka <file.um> [-storage <storage-size>] [-stack <stack-size>]\n");
        return 1;
    }

    int storageSize = 1024 * 1024;  // Bytes
    int stackSize   = 1024 * 1024;  // Slots

    for (int i = 0; i < 4; i += 2)
    {
        if (argc > 2 + i)
        {
            if (strcmp(argv[2 + i], "-storage") == 0)
            {
                if (argc == 2 + i + 1)
                {
                    printf("Illegal command line parameter\n");
                    return 1;
                }

                storageSize = strtol(argv[2 + i + 1], NULL, 0);
                if (storageSize <= 0)
                {
                    printf("Illegal storage size\n");
                    return 1;
                }
            }
            else if (strcmp(argv[2 + i], "-stack") == 0)
            {
                if (argc == 2 + i + 1)
                {
                    printf("Illegal command line parameter\n");
                    return 1;
                }

                stackSize = strtol(argv[2 + i + 1], NULL, 0);
                if (stackSize <= 0)
                {
                    printf("Illegal stack size\n");
                    return 1;
                }
            }
        }
    }

    int res = umkaInit(argv[1], storageSize, argc, argv);

    if (res == 0)
    {
        umkaAddFunc("meow", &meow);
        res = umkaCompile();
    }

    if (res == 0)
    {
        res = umkaRun(stackSize);

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

