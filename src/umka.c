#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_api.h"


enum
{
    ASM_BUF_SIZE = 2 * 1024 * 1024
};


void help()
{
    printf("Umka interpreter (C) Vasiliy Tereshkov, 2020-2021\n");
    printf("Usage: umka [<parameters>] <file.um> [<script-parameters>]\n");
    printf("Parameters:\n");
    printf("    -storage <storage-size> - Set static storage size\n");
    printf("    -stack <stack-size>     - Set stack size\n");
    printf("    -asm                    - Write assembly listing\n");
    printf("    -check                  - Compile only\n");
}


int main(int argc, char **argv)
{
    // Parse interpreter parameters
    int storageSize     = 1024 * 1024;  // Bytes
    int stackSize       = 1024 * 1024;  // Slots
    bool writeAsm       = false;
    bool compileOnly    = false;

    int i = 1;
    while (i < argc && argv[i][0] == '-')
    {
        if (strcmp(argv[i], "-storage") == 0)
        {
            if (i + 1 == argc)
            {
                printf("No storage size\n");
                return 1;
            }

            storageSize = strtol(argv[i + 1], NULL, 0);
            if (storageSize <= 0)
            {
                printf("Illegal storage size\n");
                return 1;
            }

            i += 2;
        }
        else if (strcmp(argv[i], "-stack") == 0)
        {
            if (i + 1 == argc)
            {
                printf("No stack size\n");
                return 1;
            }

            stackSize = strtol(argv[i + 1], NULL, 0);
            if (stackSize <= 0)
            {
                printf("Illegal stack size\n");
                return 1;
            }

            i += 2;
        }
        else if (strcmp(argv[i], "-asm") == 0)
        {
            writeAsm = true;
            i += 1;
        }
        else if (strcmp(argv[i], "-check") == 0)
        {
            compileOnly = true;
            i += 1;
        }
        else
            break;
    }

    // Parse file name
    if (i >= argc)
    {
        help();
        return 1;
    }

    void *umka = umkaAlloc();
    bool ok = umkaInit(umka, argv[i], NULL, storageSize, stackSize, argc - i, argv + i);
    if (ok)
        ok = umkaCompile(umka);

    if (ok)
    {
        if (writeAsm)
        {
            char *asmFileName = malloc(strlen(argv[i]) + 4 + 1);
            sprintf(asmFileName, "%s.asm", argv[i]);
            char *asmBuf = malloc(ASM_BUF_SIZE);
            umkaAsm(umka, asmBuf, ASM_BUF_SIZE);

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
            free(asmFileName);
        }

        if (!compileOnly)
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
    return !ok;
}

