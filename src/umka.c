#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_api.h"


enum
{
    ASM_BUF_SIZE = 2 * 1024 * 1024
};


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
        printf("Usage: umka <file.um> [<parameters>] [<script-parameters>]\n");
        printf("Parameters:\n");
        printf("    -storage <storage-size>\n");
        printf("    -stack   <stack-size>\n");
        printf("    -asm     <output.asm>\n");
        return 1;
    }

    int storageSize     = 1024 * 1024;  // Bytes
    int stackSize       = 1024 * 1024;  // Slots
    char *asmFileName   = NULL;

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
            else if (strcmp(argv[2 + i], "-asm") == 0)
            {
                if (argc == 2 + i + 1)
                {
                    printf("Illegal command line parameter\n");
                    return 1;
                }

                asmFileName = argv[2 + i + 1];
            }
        }
    }

    bool ok = umkaInit(argv[1], storageSize, stackSize, argc, argv);
    if (ok)
    {
        umkaAddFunc("meow", &meow);
        ok = umkaCompile();
    }

    if (ok)
    {
        if (asmFileName)
        {
            char *asmBuf = malloc(ASM_BUF_SIZE);
            umkaAsm(asmBuf);

            FILE *asmFile = fopen(asmFileName, "w");
            if (!asmFile)
            {
                printf("Cannot open file %s\n", asmFileName);
                return 1;
            }
            if (fwrite(asmBuf, strlen(asmBuf), 1, asmFile) != 1)
            {
                printf("Cannot write file %s\n", asmFileName);
                return 1;
            }

            fclose(asmFile);
            free(asmBuf);
        }

        ok = umkaRun();
        if (!ok)
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

    if (ok)
        umkaFree();

    return ok ? 0 : 1;
}

