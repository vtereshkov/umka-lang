#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_api.h"


enum
{
    ASM_BUF_SIZE = 2 * 1024 * 1024
};


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

    void *umka = umkaAlloc();
    bool ok = umkaInit(umka, argv[1], NULL, storageSize, stackSize, argc, argv);
    if (ok)
        ok = umkaCompile(umka);

    if (ok)
    {
        if (asmFileName)
        {
            char *asmBuf = malloc(ASM_BUF_SIZE);
            umkaAsm(umka, asmBuf);

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

        ok = umkaRun(umka);
        if (!ok)
        {
            UmkaError error;
            umkaGetError(umka, &error);
            printf("\nRuntime error %s (%d): %s\n", error.fileName, error.line, error.msg);
        }
    }
    else
    {
        UmkaError error;
        umkaGetError(umka, &error);
        printf("Error %s (%d, %d): %s\n", error.fileName, error.line, error.pos, error.msg);
    }

    umkaFree(umka);
    return ok ? 0 : 1;
}

