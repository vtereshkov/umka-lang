#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

#include "umka_compiler.h"
#include "umka_api.h"


Compiler comp;
UmkaError error;
jmp_buf jumper;


void compileError(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    strcpy(error.fileName, comp.lex.fileName);
    error.line = comp.lex.line;
    error.pos = comp.lex.pos;
    vsprintf(error.msg, format, args);

    longjmp(jumper, 1);
    va_end(args);
}


void runtimeError(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Instruction *instr = &comp.vm.fiber->code[comp.vm.fiber->ip];

    strcpy(error.fileName, instr->debug.fileName);
    error.line = instr->debug.line;
    error.pos = 1;
    vsprintf(error.msg, format, args);

    longjmp(jumper, 1);
    va_end(args);
}


// API functions

int umkaInit(char *fileName, int storageCapacity, int argc, char **argv)
{
    if (setjmp(jumper) == 0)
    {
        compilerInit(&comp, fileName, storageCapacity, argc, argv, compileError);
        return 0;
    }
    else
    {
        compilerFree(&comp);
        return 1;
    }
}


int umkaCompile(void)
{
    if (setjmp(jumper) == 0)
    {
        compilerCompile(&comp);
        return 0;
    }
    else
    {
        compilerFree(&comp);
        return 1;
    }
}


int umkaRun(int stackSize)
{
    if (setjmp(jumper) == 0)
    {
        compilerRun(&comp, stackSize, runtimeError);
        return 0;
    }
    else
    {
        compilerFree(&comp);
        return 1;
    }
}


int umkaFree(void)
{
    compilerFree(&comp);
    return 0;
}


int umkaGetError(UmkaError *err)
{
    *err = error;
    return 0;
}


int umkaAsm(char *buf)
{
    compilerAsm(&comp, buf);
    return 0;
}


int umkaAddFunc(char *name, UmkaExternFunc entry)
{
    externalAdd(&comp.externals, name, entry);
    return 0;
}


