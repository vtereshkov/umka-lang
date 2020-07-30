#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


#include "umka_compiler.h"
#include "umka_api.h"


void compileError(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;

    strcpy(comp->error.fileName, comp->lex.fileName);
    comp->error.line = comp->lex.line;
    comp->error.pos = comp->lex.pos;
    vsprintf(comp->error.msg, format, args);

    longjmp(comp->error.jumper, 1);
    va_end(args);
}


void runtimeError(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;
    Instruction *instr = &comp->vm.fiber->code[comp->vm.fiber->ip];

    strcpy(comp->error.fileName, instr->debug.fileName);
    comp->error.line = instr->debug.line;
    comp->error.pos = 1;
    vsprintf(comp->error.msg, format, args);

    longjmp(comp->error.jumper, 1);
    va_end(args);
}


// API functions

void *umkaAlloc(void)
{
    return malloc(sizeof(Compiler));
}


bool umkaInit(void *umka, char *fileName, int storageSize, int stackSize, int argc, char **argv)
{
    Compiler *comp = umka;
    memset(comp, 0, sizeof(Compiler));

    // First set error handlers
    comp->error.handler = compileError;
    comp->error.handlerRuntime = runtimeError;
    comp->error.context = comp;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerInit(comp, fileName, storageSize, stackSize, argc, argv);
        return true;
    }
    return false;
}


bool umkaCompile(void *umka)
{
    Compiler *comp = umka;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerCompile(comp);
        return true;
    }
    return false;
}


bool umkaRun(void *umka)
{
    Compiler *comp = umka;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerRun(comp);
        return true;
    }
    return false;
}


bool umkaCall(void *umka, int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result)
{
    Compiler *comp = umka;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerCall(comp, entryOffset, numParamSlots, (Slot *)params, (Slot *)result);
        return true;
    }
    return false;
}


void umkaFree(void *umka)
{
    Compiler *comp = umka;
    compilerFree(comp);
    free(comp);
}


void umkaGetError(void *umka, UmkaError *err)
{
    Compiler *comp = umka;
    strcpy(err->fileName, comp->error.fileName);
    err->line = comp->error.line;
    err->pos = comp->error.pos;
    strcpy(err->msg, comp->error.msg);
}


void umkaAsm(void *umka, char *buf)
{
    Compiler *comp = umka;
    compilerAsm(comp, buf);
}


void umkaAddFunc(void *umka, char *name, UmkaExternFunc entry)
{
    Compiler *comp = umka;
    externalAdd(&comp->externals, name, entry);
}


int umkaGetFunc(void *umka, char *moduleName, char *funcName)
{
    Compiler *comp = umka;
    return compilerGetFunc(comp, moduleName, funcName);
}

