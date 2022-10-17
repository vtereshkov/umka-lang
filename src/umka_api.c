#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "umka_compiler.h"
#include "umka_api.h"

#define UMKA_VERSION    "0.10"


static void compileWarning(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;

    UmkaError warning;
    strcpy(warning.fileName, comp->lex.fileName);
    strcpy(warning.fnName, comp->debug.fnName);
    warning.line = comp->lex.tok.line;
    warning.pos = comp->lex.tok.pos;
    vsnprintf(warning.msg, UMKA_MSG_LEN + 1, format, args);

    if (comp->error.warningCallback)
        ((UmkaWarningCallback)comp->error.warningCallback)(&warning);

    va_end(args);
}


static void compileError(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;

    strcpy(comp->error.fileName, comp->lex.fileName);
    strcpy(comp->error.fnName, comp->debug.fnName);
    comp->error.line = comp->lex.tok.line;
    comp->error.pos = comp->lex.tok.pos;
    vsnprintf(comp->error.msg, UMKA_MSG_LEN + 1, format, args);

    va_end(args);
    longjmp(comp->error.jumper, 1);
}


static void runtimeError(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;
    DebugInfo *debug = &comp->vm.fiber->debugPerInstr[comp->vm.fiber->ip];

    strcpy(comp->error.fileName, debug->fileName);
    strcpy(comp->error.fnName, debug->fnName);
    comp->error.line = debug->line;
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


UMKA_API bool umkaInit(void *umka, const char *fileName, const char *sourceString, int stackSize, const char *locale, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback)
{
    Compiler *comp = umka;
    memset(comp, 0, sizeof(Compiler));

    // First set error handlers
    comp->error.handler = compileError;
    comp->error.runtimeHandler = runtimeError;
    comp->error.warningHandler = compileWarning;
    comp->error.warningCallback = (WarningCallback)warningCallback;
    comp->error.context = comp;

    if (setjmp(comp->error.jumper) == 0)
    {
        compilerInit(comp, fileName, sourceString, stackSize, locale, argc, argv, fileSystemEnabled, implLibsEnabled);
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


UMKA_API bool umkaAddModule(void *umka, const char *fileName, const char *sourceString)
{
    Compiler *comp = umka;
    return compilerAddModule(comp, fileName, sourceString);
}


UMKA_API bool umkaAddFunc(void *umka, const char *name, UmkaExternFunc func)
{
    Compiler *comp = umka;
    return compilerAddFunc(comp, name, (ExternFunc)func);
}


UMKA_API int umkaGetFunc(void *umka, const char *moduleName, const char *funcName)
{
    Compiler *comp = umka;
    return compilerGetFunc(comp, moduleName, funcName);
}


UMKA_API bool umkaGetCallStack(void *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line)
{
    Compiler *comp = umka;
    Slot *base = comp->vm.fiber->base;
    int ip = comp->vm.fiber->ip;

    while (depth-- > 0)
        if (!vmUnwindCallStack(&comp->vm, &base, &ip))
            return false;

    if (offset)
        *offset = ip;

    if (fileName)
        snprintf(fileName, nameSize, "%s", comp->vm.fiber->debugPerInstr[ip].fileName);

    if (fnName)
        snprintf(fnName, nameSize, "%s", comp->vm.fiber->debugPerInstr[ip].fnName);

    if (line)
        *line = comp->vm.fiber->debugPerInstr[ip].line;

    return true;
}


UMKA_API void umkaSetHook(void *umka, UmkaHookEvent event, UmkaHookFunc hook)
{
    Compiler *comp = umka;
    vmSetHook(&comp->vm, (HookEvent)event, hook);
}


UMKA_API void *umkaAllocData(void *umka, int size, UmkaExternFunc onFree)
{
    Compiler *comp = umka;
    return vmAllocData(&comp->vm, size, (ExternFunc)onFree);
}


UMKA_API void umkaIncRef(void *umka, void *ptr)
{
    Compiler *comp = umka;
    vmIncRef(&comp->vm, ptr);
}


UMKA_API void umkaDecRef(void *umka, void *ptr)
{
    Compiler *comp = umka;
    vmDecRef(&comp->vm, ptr);
}


UMKA_API void *umkaGetMapItem(void *umka, UmkaMap *map, UmkaStackSlot key)
{
    Compiler *comp = umka;
    return vmGetMapNodeData(&comp->vm, (Map *)map, *(Slot *)&key);
}


UMKA_API int umkaGetDynArrayLen(const void *array)
{
    const DynArray *dynArray = (const DynArray *)array;
    if (!dynArray->data)
        return 0;
    return getDims(dynArray)->len;
}


UMKA_API const char *umkaGetVersion(void)
{
    if (sizeof(void *) == 8)
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__" 64 bit)";
    else if (sizeof(void *) == 4)
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__" 32 bit)";
    else
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__")";
}

