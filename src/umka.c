#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_api.h"


enum
{
    DEFAULT_STACK_SIZE      =  1 * 1024 * 1024,  // Slots
    MAX_CALL_STACK_DEPTH    = 10,
    MAX_STR_LENGTH          = 256
};


void help(void)
{
    printf("%s\n", umkaGetVersion());
    printf("(C) Vasiliy Tereshkov, 2020-2024\n");
    printf("Usage: umka [<parameters>] <file.um> [<script-parameters>]\n");
    printf("Parameters:\n");
    printf("    -stack <stack-size>     - Set stack size\n");
    printf("    -asm                    - Write assembly listing\n");
    printf("    -check                  - Compile only\n");
    printf("    -warn                   - Enable warnings\n");
    printf("    -sandbox                - Run in sandbox mode\n");
}


void printCompileWarning(UmkaError *warning)
{
    fprintf(stderr, "Warning %s (%d, %d): %s\n", warning->fileName, warning->line, warning->pos, warning->msg);
}


void printCompileError(void *umka)
{
    UmkaError *error = umkaGetError(umka);
    fprintf(stderr, "Error %s (%d, %d): %s\n", error->fileName, error->line, error->pos, error->msg);
}


void printRuntimeError(void *umka)
{
    UmkaError *error = umkaGetError(umka);

    if (error->msg[0])
    {
        fprintf(stderr, "\nRuntime error %s (%d): %s\n", error->fileName, error->line, error->msg);
        fprintf(stderr, "Stack trace:\n");

        for (int depth = 0; depth < MAX_CALL_STACK_DEPTH; depth++)
        {
            char fileName[MAX_STR_LENGTH + 1], fnName[MAX_STR_LENGTH + 1];
            int line;

            if (!umkaGetCallStack(umka, depth, MAX_STR_LENGTH + 1, NULL, fileName, fnName, &line))
                break;

            fprintf(stderr, "    %s: %s (%d)\n", fnName, fileName, line);
        }
    }
}


#ifdef __EMSCRIPTEN__

int runPlayground(const char *fileName, const char *sourceString)
{
    void *umka = umkaAlloc();
    bool ok = umkaInit(umka, fileName, sourceString, DEFAULT_STACK_SIZE, NULL, 0, NULL, false, false, printCompileWarning);
    if (ok)
        ok = umkaCompile(umka);

    if (ok)
    {
        ok = umkaRun(umka) == 0;
        if (ok)
            printf("\n");
        else
            printRuntimeError(umka);
    }
    else
        printCompileError(umka);

    umkaFree(umka);
    return !ok;
}

#else


int main(int argc, char **argv)
{
    // Parse interpreter parameters
    int stackSize       = DEFAULT_STACK_SIZE;
    bool writeAsm       = false;
    bool compileOnly    = false;
    bool printWarnings  = false;
    bool isSandbox      = false;

    int i = 1;
    while (i < argc && argv[i][0] == '-')
    {
        if (strcmp(argv[i], "-stack") == 0)
        {
            if (i + 1 == argc)
            {
                fprintf(stderr, "No stack size\n");
                return 1;
            }

            stackSize = strtol(argv[i + 1], NULL, 0);
            if (stackSize <= 0)
            {
                fprintf(stderr, "Illegal stack size\n");
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
        else if (strcmp(argv[i], "-warn") == 0)
        {
            printWarnings = true;
            i += 1;
        }
        else if (strcmp(argv[i], "-sandbox") == 0)
        {
            isSandbox = true;
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
    bool ok = umkaInit(umka, argv[i], NULL, stackSize, NULL, argc - i, argv + i, !isSandbox, !isSandbox, printWarnings ? printCompileWarning : NULL);
    int exitCode = 0;

    if (ok)
        ok = umkaCompile(umka);

    if (ok)
    {
        if (writeAsm)
        {
            char *asmFileName = malloc(strlen(argv[i]) + 4 + 1);
            sprintf(asmFileName, "%s.asm", argv[i]);
            char *asmBuf = umkaAsm(umka);
            if (!asmBuf)
            {
                fprintf(stderr, "Cannot output assembly listing\n");
                return 1;
            }
            FILE *asmFile = fopen(asmFileName, "w");
            if (!asmFile)
            {
                fprintf(stderr, "Cannot open file %s\n", asmFileName);
                return 1;
            }
            if (fwrite(asmBuf, strlen(asmBuf), 1, asmFile) != 1)
            {
                fprintf(stderr, "Cannot write file %s\n", asmFileName);
                return 1;
            }

            fclose(asmFile);
            free(asmBuf);
            free(asmFileName);
        }

        if (!compileOnly)
            exitCode = umkaRun(umka);

        if (exitCode)
            printRuntimeError(umka);
    }
    else
        printCompileError(umka);

    if (!ok)
        exitCode = 1;

    umkaFree(umka);
    return exitCode;
}

#endif // EMSCRIPTEN
