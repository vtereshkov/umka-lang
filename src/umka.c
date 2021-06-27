#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_api.h"


enum
{
    ASM_BUF_SIZE = 2 * 1024 * 1024
};


static void help()
{
    printf("Umka interpreter (C) Vasiliy Tereshkov, 2020-2021\n");
    printf("Usage: umka [<parameters>] <file.um> [<script-parameters>]\n");
    printf("Parameters:\n");
    printf("    -storage <storage-size> - Set static storage size\n");
    printf("    -stack <stack-size>     - Set stack size\n");
    printf("    -asm                    - Write assembly listing\n");
    printf("    -check                  - Compile only\n");
}


typedef struct
{
    const char* umkaLoadPath;
} LoadFnData;

static LoadFnData *getLoadFnData(char **envVars) {
    LoadFnData *loadData = calloc(1, sizeof(LoadFnData));

    for (char **pv = envVars; NULL != *pv; pv += 1) {
        // format of 'v' is: name=value
        const char *v = *pv;

        if (0 == strncmp("UMKA_LOAD_PATH", v, 14)) { // 14 == strlen("UMKA_LOAD_PATH")
            loadData->umkaLoadPath = v + 14 + 1; // + 1 to skip the '='
            break;
        }
    }

    return loadData;
}

static char *readEntireFile(const char *fname) {
    FILE *f = fopen(fname, "rb");
    if (!f) { return NULL; }

    fseek(f, 0, SEEK_END);
    int fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(fsize + 1);
    if (fsize != fread(buf, 1, fsize, f)) {
        fclose(f);
        return NULL;
    }
    buf[fsize] = 0;

    fclose(f);
    return buf;
}

static char *loadFn(void *fnLoadData, const char *importPath) {
    const LoadFnData *loadData = (const LoadFnData*)fnLoadData;
    char *buf;

    // try "importPath"
    buf = readEntireFile(importPath);
    if (buf) return buf;

    // try "UMKA_LOAD_PATH/importPath"
    if (loadData->umkaLoadPath) {
        char fname[4096];
        snprintf(fname, sizeof(fname) - 1, "%s/%s", loadData->umkaLoadPath, importPath);
        buf = readEntireFile(fname);
    }

    return buf;
}


int main(int argc, char **argv, char **envVars)
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

    UmkaConfig cfg = {0};
    cfg.storageSize = storageSize;
    cfg.stackSize = stackSize;
    cfg.argc = argc - i;
    cfg.argv = argv + i;
    cfg.loadFnData = (void*)getLoadFnData(envVars);
    cfg.loadFn = &loadFn;

    bool ok = umkaInit(umka, argv[i], NULL, cfg);
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

