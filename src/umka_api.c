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

bool umkaInit(char *fileName, int storageSize, int stackSize, int argc, char **argv)
{
    if (setjmp(jumper) == 0)
    {
        compilerInit(&comp, fileName, storageSize, stackSize, argc, argv, compileError, runtimeError);
        return true;
    }
    else
    {
        compilerFree(&comp);
        return false;
    }
}


bool umkaCompile(void)
{
    if (setjmp(jumper) == 0)
    {
        compilerCompile(&comp);
        return true;
    }
    else
    {
        compilerFree(&comp);
        return false;
    }
}


bool umkaRun(void)
{
    if (setjmp(jumper) == 0)
    {
        compilerRun(&comp);
        return true;
    }
    else
    {
        compilerFree(&comp);
        return false;
    }
}


bool umkaCall(int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result)
{
    if (setjmp(jumper) == 0)
    {
        compilerCall(&comp, entryOffset, numParamSlots, (Slot *)params, (Slot *)result);
        return true;
    }
    else
    {
        compilerFree(&comp);
        return false;
    }
}


void umkaFree(void)
{
    compilerFree(&comp);
}


void umkaGetError(UmkaError *err)
{
    *err = error;
}


void umkaAsm(char *buf)
{
    compilerAsm(&comp, buf);
}


void umkaAddFunc(char *name, UmkaExternFunc entry)
{
    externalAdd(&comp.externals, name, entry);
}


int umkaGetFunc(char *name)
{
    return compilerGetFunc(&comp, name);
}

