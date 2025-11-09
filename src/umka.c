#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_api.h"

#ifdef _WIN32
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif


enum
{
    DEFAULT_STACK_SIZE      =  1 * 1024 * 1024,  // Slots
    MAX_CALL_STACK_DEPTH    = 10,
    MAX_STR_LENGTH          = 256
};


void help(void)
{
    printf("%s\n", umkaGetVersion());
    printf("(C) Vasiliy Tereshkov, 2020-2025\n");
    printf("Usage: umka [<parameters>] [<file.um>] [<script-parameters>]\n");
    printf("Parameters:\n");
    printf("    -stack <stack-size>     - Set stack size\n");
    printf("    -asm                    - Write assembly listing\n");
    printf("    -check                  - Compile only\n");
    printf("    -warn                   - Enable warnings\n");
    printf("    -sandbox                - Run in sandbox mode\n");
}


bool writeAsmFile(Umka *umka, const char *mainPath)
{
    bool ok = false;

    char *asmFileName = malloc(strlen(mainPath) + 4 + 1);
    if (!asmFileName)
        fprintf(stderr, "Error: Out of memory\n");
    else
    {
        sprintf(asmFileName, "%s.asm", mainPath);
        const char *asmBuf = umkaAsm(umka);
        if (!asmBuf)
            fprintf(stderr, "Error: Cannot output assembly listing\n");
        else
        {
            FILE *asmFile = fopen(asmFileName, "w");
            if (!asmFile)
                fprintf(stderr, "Error: Cannot open file %s\n", asmFileName);
            else
            {
                if (fwrite(asmBuf, strlen(asmBuf), 1, asmFile) != 1)
                    fprintf(stderr, "Error: Cannot write file %s\n", asmFileName);
                else
                    ok = true;
                fclose(asmFile);
            }
        }
        free(asmFileName);
    }

    return ok;
}


void printCompileWarning(UmkaError *warning)
{
    fprintf(stderr, "Warning %s (%d, %d): %s\n", warning->fileName, warning->line, warning->pos, warning->msg);
}


void printCompileError(Umka *umka)
{
    const UmkaError *error = umkaGetError(umka);
    fprintf(stderr, "Error %s (%d, %d): %s\n", error->fileName, error->line, error->pos, error->msg);
}


void printRuntimeError(Umka *umka)
{
    const UmkaError *error = umkaGetError(umka);

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
    Umka *umka = umkaAlloc();
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

#define STDIN_BUF_SIZE 1024

// Allocates a new buffer and reads stdin until EOF
char* readStdinSourceString(void)
{
    char buffer[STDIN_BUF_SIZE];
    size_t sourceStringSize = 1; // includes NULL
    char* sourceString = malloc(sizeof(char) * STDIN_BUF_SIZE);

    if (sourceString == NULL)
    {
        fprintf(stderr, "Error: failed to allocate stdin read buffer\n");
        return NULL;
    }

    sourceString[0] = '\0';
    while(fgets(buffer, STDIN_BUF_SIZE, stdin))
    {
        char *old = sourceString;
        sourceStringSize += strlen(buffer);
        sourceString = realloc(sourceString, sourceStringSize);

        if(sourceString == NULL)
        {
            fprintf(stderr, "Error: Failed to reallocate stdin read buffer\n");
            free(old);
            return NULL;
        }

        strcat(sourceString, buffer);
    }

    return sourceString;
}

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

    char* sourceString = NULL;
    char* fileName;
    int scriptArgc = argc - i;
    char** scriptArgv;

    if (!isatty(fileno(stdin)))
    {
        fileName = argv[0];
        sourceString = readStdinSourceString();
        scriptArgc = argc - i + 1;
        scriptArgv = malloc(scriptArgc * sizeof(char*));
        scriptArgv[0] = argv[0];
        for (int j = 0; j < (argc - i); j++) {
            scriptArgv[j + 1] = argv[j + i];
        }
    }
    else if (i < argc)
    {
        fileName = argv[i];
        scriptArgv = &argv[i];
        scriptArgc = argc - i;
    }
    else
    {
        help();
        return 1;
    }

    Umka *umka = umkaAlloc();

    bool ok = umkaInit(
        umka,
        fileName,
        sourceString,
        stackSize,
        NULL,
        scriptArgc,
        scriptArgv,
        !isSandbox,
        !isSandbox,
        printWarnings ? printCompileWarning : NULL
    );

    int exitCode = 0;

    if (ok)
        ok = umkaCompile(umka);

    if (ok)
    {
        if (writeAsm)
            ok = writeAsmFile(umka, argv[i]);

        if (ok && !compileOnly)
            exitCode = umkaRun(umka);

        if (exitCode)
            printRuntimeError(umka);
    }
    else
        printCompileError(umka);

    if (!ok)
        exitCode = 1;

    umkaFree(umka);

    // sourceString and scriptArgv are freed at the end of program execution

    return exitCode;
}

#endif // EMSCRIPTEN
