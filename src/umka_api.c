#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "umka_compiler.h"
#include "umka_api.h"

#define UMKA_VERSION    "1.5"


static void compileWarning(void *context, DebugInfo *debug, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;

    ErrorReport report = {0};
    errorReportInit(&report,
                    debug ? debug->fileName : comp->lex.fileName,
                    debug ? debug->fnName : comp->debug.fnName,
                    debug ? debug->line : comp->lex.tok.line,
                    debug ? 1 : comp->lex.tok.pos,
                    0,
                    format, args);

    if (comp->error.warningCallback)
        ((UmkaWarningCallback)comp->error.warningCallback)((UmkaError *)&report);

    errorReportFree(&report);

    va_end(args);
}


static void compileError(void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;
    errorReportInit(&comp->error.report, comp->lex.fileName, comp->debug.fnName, comp->lex.tok.line, comp->lex.tok.pos, 1, format, args);

    vmKill(&comp->vm);

    va_end(args);
    longjmp(comp->error.jumper, 1);
}


static void runtimeError(void *context, int code, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    Compiler *comp = context;
    DebugInfo *debug = &comp->vm.fiber->debugPerInstr[comp->vm.fiber->ip];
    errorReportInit(&comp->error.report, debug->fileName, debug->fnName, debug->line, 1, code, format, args);

    vmKill(&comp->vm);

    va_end(args);
    longjmp(comp->error.jumper, 1);
}


// API functions

UMKA_API void *umkaAlloc(void)
{
    return malloc(sizeof(Compiler));
}


UMKA_API bool umkaInit(void *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback)
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
        compilerInit(comp, fileName, sourceString, stackSize, argc, argv, fileSystemEnabled, implLibsEnabled);
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


UMKA_API int umkaRun(void *umka)
{
    Compiler *comp = umka;

    if (setjmp(comp->error.jumper) == 0)
    {
        comp->error.jumperNesting++;
        compilerRun(comp);
        comp->error.jumperNesting--;
        return 0;
    }

    return comp->error.report.code;
}


UMKA_API int umkaCall(void *umka, UmkaFuncContext *fn)
{
    Compiler *comp = umka;

    // Nested calls to umkaCall() should not reset the error jumper
    jmp_buf dummyJumper;
    jmp_buf *jumper = comp->error.jumperNesting == 0 ? &comp->error.jumper : &dummyJumper;

    if (setjmp(*jumper) == 0)
    {
        comp->error.jumperNesting++;
        compilerCall(comp, (FuncContext *)fn);
        comp->error.jumperNesting--;
        return 0;
    }

    return comp->error.report.code;
}


UMKA_API void umkaFree(void *umka)
{
    Compiler *comp = umka;
    compilerFree(comp);
    free(comp);
}


UMKA_API UmkaError *umkaGetError(void *umka)
{
    Compiler *comp = umka;
    return (UmkaError *)(&comp->error.report);
}


UMKA_API bool umkaAlive(void *umka)
{
    Compiler *comp = umka;
    return vmAlive(&comp->vm);
}


UMKA_API char *umkaAsm(void *umka)
{
    Compiler *comp = umka;
    return compilerAsm(comp);
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


UMKA_API bool umkaGetFunc(void *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn)
{
    Compiler *comp = umka;
    return compilerGetFunc(comp, moduleName, fnName, (FuncContext *)fn);
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


UMKA_API char *umkaMakeStr(void *umka, const char *str)
{
    Compiler *comp = umka;
    return vmMakeStr(&comp->vm, str);
}


UMKA_API int umkaGetStrLen(const char *str)
{
    if (!str)
        return 0;
    return getStrDims(str)->len;
}


UMKA_API void umkaMakeDynArray(void *umka, void *array, void *type, int len)
{
    Compiler *comp = umka;
    vmMakeDynArray(&comp->vm, (DynArray *)array, (Type *)type, len);
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


UMKA_API int64_t umkaGetMemUsage(void *umka)
{
    Compiler *comp = umka;
    return vmGetMemUsage(&comp->vm);
}


UMKA_API void umkaMakeFuncContext(void *umka, void *closureType, int entryOffset, UmkaFuncContext *fn)
{
    Compiler *comp = umka;
    compilerMakeFuncContext(comp, ((Type *)closureType)->field[0]->type, entryOffset, (FuncContext *)fn);
}

