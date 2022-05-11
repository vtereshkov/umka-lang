#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "umka_compiler.h"
#include "umka_api.h"


static void compileError(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;

    strcpy(comp->error.fileName, comp->lex.fileName);
    comp->error.line = comp->lex.line;
    comp->error.pos = comp->lex.pos;
    vsnprintf(comp->error.msg, UMKA_MSG_LEN + 1, format, args);

    va_end(args);
    longjmp(comp->error.jumper, 1);
}


static void runtimeError(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;
    Instruction *instr = &comp->vm.fiber->code[comp->vm.fiber->ip];

    strcpy(comp->error.fileName, instr->debug.fileName);
    strcpy(comp->error.fnName, instr->debug.fnName);
    comp->error.line = instr->debug.line;
    comp->error.pos = 1;
    vsnprintf(comp->error.msg, UMKA_MSG_LEN + 1, format, args);

    va_end(args);
    longjmp(comp->error.jumper, 1);
}


// API functions

UMKA_API void *umkaAlloc(void)
{
    return malloc(sizeof(Compiler));
}


UMKA_API bool umkaInit(void *umka, const char *fileName, const char *sourceString, int reserved, int stackSize, const char *locale, int argc, char **argv)
{
    Compiler *comp = umka;
    memset(comp, 0, sizeof(Compiler));

    // First set error handlers
    comp->error.handler = compileError;
    comp->error.handlerRuntime = runtimeError;
    comp->error.context = comp;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerInit(comp, fileName, sourceString, stackSize, locale, argc, argv);
        return true;
    }
    return false;
}


UMKA_API bool umkaCompile(void *umka)
{
    Compiler *comp = umka;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerCompile(comp);
        return true;
    }
    return false;
}


UMKA_API bool umkaRun(void *umka)
{
    Compiler *comp = umka;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerRun(comp);
        return true;
    }
    return false;
}


UMKA_API bool umkaCall(void *umka, int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result)
{
    Compiler *comp = umka;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerCall(comp, entryOffset, numParamSlots, (Slot *)params, (Slot *)result);
        return true;
    }
    return false;
}


UMKA_API void umkaFree(void *umka)
{
    Compiler *comp = umka;
    compilerFree(comp);
    free(comp);
}


UMKA_API void umkaGetError(void *umka, UmkaError *err)
{
    Compiler *comp = umka;
    strcpy(err->fileName, comp->error.fileName);
    strcpy(err->fnName, comp->error.fnName);
    err->line = comp->error.line;
    err->pos = comp->error.pos;
    strcpy(err->msg, comp->error.msg);
}


UMKA_API void umkaAsm(void *umka, char *buf, int size)
{
    Compiler *comp = umka;
    compilerAsm(comp, buf, size);
}


UMKA_API void umkaAddModule(void *umka, const char *fileName, const char *sourceString)
{
    Compiler *comp = umka;
    moduleAddSource(&comp->modules, fileName, sourceString);
}


UMKA_API void umkaAddFunc(void *umka, const char *name, UmkaExternFunc entry)
{
    Compiler *comp = umka;
    externalAdd(&comp->externals, name, entry);
}


UMKA_API int umkaGetFunc(void *umka, const char *moduleName, const char *funcName)
{
    Compiler *comp = umka;
    return compilerGetFunc(comp, moduleName, funcName);
}


UMKA_API bool umkaGetCallStack(void *umka, int depth, int *offset, char *name, int size)
{
    Compiler *comp = umka;
    Slot *base = comp->vm.fiber->base;
    int ip = comp->vm.fiber->ip;

    while (depth-- > 0)
        if (!vmUnwindCallStack(&comp->vm, &base, &ip))
            return false;

    if (offset)
        *offset = ip;

    if (name)
        snprintf(name, size, "%s", comp->vm.fiber->code[ip].debug.fnName);

    return true;
}


UMKA_API void umkaSetHook(void *umka, UmkaHookEvent event, UmkaHookFunc hook)
{
    Compiler *comp = umka;
    vmSetHook(&comp->vm, (HookEvent)event, hook);
}


UMKA_API const char *umkaGetVersion(void)
{
    if (sizeof(void *) == 8)
        return __DATE__" "__TIME__" 64 bit";
    else if (sizeof(void *) == 4)
        return __DATE__" "__TIME__" 32 bit";
    else
        return __DATE__" "__TIME__;
}

