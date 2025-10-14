#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "umka_compiler.h"
#include "umka_api.h"

#define UMKA_VERSION    "1.5.5"


static void compileWarning(Umka *umka, const DebugInfo *debug, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    UmkaError report = {0};
    errorReportInit(&report, &umka->storage,
                    debug ? debug->fileName : umka->lex.fileName,
                    debug ? debug->fnName : umka->debug.fnName,
                    debug ? debug->line : umka->lex.tok.line,
                    debug ? 1 : umka->lex.tok.pos,
                    0,
                    format, args);

    if (umka->error.warningCallback)
        ((UmkaWarningCallback)umka->error.warningCallback)(&report);

    va_end(args);
}


static void compileError(Umka *umka, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    errorReportInit(&umka->error.report, &umka->storage, umka->lex.fileName, umka->debug.fnName, umka->lex.tok.line, umka->lex.tok.pos, 1, format, args);

    vmKill(&umka->vm);

    va_end(args);
    longjmp(umka->error.jumper, 1);
}


static void runtimeError(Umka *umka, int code, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    const DebugInfo *debug = &umka->vm.fiber->debugPerInstr[umka->vm.fiber->ip];
    errorReportInit(&umka->error.report, &umka->storage, debug->fileName, debug->fnName, debug->line, 1, code, format, args);

    vmKill(&umka->vm);

    va_end(args);
    longjmp(umka->error.jumper, 1);
}


// API functions

UMKA_API Umka *umkaAlloc(void)
{
    return malloc(sizeof(Umka));
}


UMKA_API bool umkaInit(Umka *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback)
{
    memset(umka, 0, sizeof(Umka));

    // First set error handlers
    umka->error.handler = compileError;
    umka->error.runtimeHandler = runtimeError;
    umka->error.warningHandler = compileWarning;
    umka->error.warningCallback = warningCallback;
    umka->error.context = umka;

    if (setjmp(umka->error.jumper) == 0)
    {
        compilerInit(umka, fileName, sourceString, stackSize, argc, argv, fileSystemEnabled, implLibsEnabled);
        return true;
    }
    return false;
}


UMKA_API bool umkaCompile(Umka *umka)
{
    if (setjmp(umka->error.jumper) == 0)
    {
        compilerCompile(umka);
        return true;
    }
    return false;
}


UMKA_API int umkaRun(Umka *umka)
{
    if (setjmp(umka->error.jumper) == 0)
    {
        umka->error.jumperNesting++;
        compilerRun(umka);
        umka->error.jumperNesting--;
        return 0;
    }

    return umka->error.report.code;
}


UMKA_API int umkaCall(Umka *umka, UmkaFuncContext *fn)
{
    // Nested calls to umkaCall() should not reset the error jumper
    jmp_buf dummyJumper;
    jmp_buf *jumper = umka->error.jumperNesting == 0 ? &umka->error.jumper : &dummyJumper;

    if (setjmp(*jumper) == 0)
    {
        umka->error.jumperNesting++;
        compilerCall(umka, fn);
        umka->error.jumperNesting--;
        return 0;
    }

    return umka->error.report.code;
}


UMKA_API void umkaFree(Umka *umka)
{
    compilerFree(umka);
    free(umka);
}


UMKA_API UmkaError *umkaGetError(Umka *umka)
{
    return &umka->error.report;
}


UMKA_API bool umkaAlive(Umka *umka)
{
    return vmAlive(&umka->vm);
}


UMKA_API char *umkaAsm(Umka *umka)
{
    return compilerAsm(umka);
}


UMKA_API bool umkaAddModule(Umka *umka, const char *fileName, const char *sourceString)
{
    return compilerAddModule(umka, fileName, sourceString);
}


UMKA_API bool umkaAddFunc(Umka *umka, const char *name, UmkaExternFunc func)
{
    return compilerAddFunc(umka, name, func);
}


UMKA_API bool umkaGetFunc(Umka *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn)
{
    return compilerGetFunc(umka, moduleName, fnName, fn);
}


UMKA_API bool umkaGetCallStack(Umka *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line)
{
    Slot *base = umka->vm.fiber->base;
    int ip = umka->vm.fiber->ip;

    while (depth-- > 0)
        if (!vmUnwindCallStack(&umka->vm, &base, &ip))
            return false;

    if (offset)
        *offset = ip;

    if (fileName)
        snprintf(fileName, nameSize, "%s", umka->vm.fiber->debugPerInstr[ip].fileName);

    if (fnName)
        snprintf(fnName, nameSize, "%s", umka->vm.fiber->debugPerInstr[ip].fnName);

    if (line)
        *line = umka->vm.fiber->debugPerInstr[ip].line;

    return true;
}


UMKA_API void umkaSetHook(Umka *umka, UmkaHookEvent event, UmkaHookFunc hook)
{
    vmSetHook(&umka->vm, event, hook);
}


UMKA_API void *umkaAllocData(Umka *umka, int size, UmkaExternFunc onFree)
{
    return vmAllocData(&umka->vm, size, onFree);
}


UMKA_API void umkaIncRef(Umka *umka, void *ptr)
{
    vmIncRef(&umka->vm, ptr, umka->ptrVoidType);    // We have no actual type info provided by the user, so we can only rely on the type info from the heap chunk header, if any
}


UMKA_API void umkaDecRef(Umka *umka, void *ptr)
{
    vmDecRef(&umka->vm, ptr, umka->ptrVoidType);    // We have no actual type info provided by the user, so we can only rely on the type info from the heap chunk header, if any
}


UMKA_API void *umkaGetMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key)
{
    const Slot *keyPtr = (Slot *)&key;
    return vmGetMapNodeData(&umka->vm, (Map *)map, *keyPtr);
}


UMKA_API char *umkaMakeStr(Umka *umka, const char *str)
{
    return vmMakeStr(&umka->vm, str);
}


UMKA_API int umkaGetStrLen(const char *str)
{
    if (!str)
        return 0;
    return getStrDims(str)->len;
}


UMKA_API void umkaMakeDynArray(Umka *umka, void *array, const UmkaType *type, int len)
{
    vmMakeDynArray(&umka->vm, (DynArray *)array, type, len);
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


UMKA_API int64_t umkaGetMemUsage(Umka *umka)
{
    return vmGetMemUsage(&umka->vm);
}


UMKA_API void umkaMakeFuncContext(Umka *umka, const UmkaType *closureType, int entryOffset, UmkaFuncContext *fn)
{
    compilerMakeFuncContext(umka, closureType->field[0]->type, entryOffset, fn);
}


UMKA_API UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index)
{
    const ParamLayout *paramLayout = (ParamLayout *)params[-4].ptrVal;      // For -4, see the stack layout diagram in umka_vm.c
    if (index < 0 || index >= paramLayout->numParams - paramLayout->numResultParams - 1)
        return NULL;
    return params + paramLayout->firstSlotIndex[index + 1];                                                 // + 1 to skip upvalues
}


UMKA_API UmkaAny *umkaGetUpvalue(UmkaStackSlot *params)
{
    const ParamLayout *paramLayout = (ParamLayout *)params[-4].ptrVal;      // For -4, see the stack layout diagram in umka_vm.c
    return (UmkaAny *)(params + paramLayout->firstSlotIndex[0]);
}


UMKA_API UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const ParamLayout *paramLayout = (ParamLayout *)params[-4].ptrVal;      // For -4, see the stack layout diagram in umka_vm.c
    if (paramLayout->numResultParams == 1)
        result->ptrVal = params[paramLayout->firstSlotIndex[paramLayout->numParams - 1]].ptrVal;
    return result;
}


UMKA_API void *umkaGetMetadata(Umka *umka)
{
    return umka->metadata;
}


UMKA_API void umkaSetMetadata(Umka *umka, void *metadata)
{
    umka->metadata = metadata;
}


UMKA_API void *umkaMakeStruct(Umka *umka, const UmkaType *type)
{
    return vmMakeStruct(&umka->vm, type);
}


UMKA_API const UmkaType *umkaGetBaseType(const UmkaType *type) 
{
    if (type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY)
        return type->base;
    return NULL;
}
