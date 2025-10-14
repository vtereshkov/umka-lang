#define __USE_MINGW_ANSI_STDIO 1

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
#endif

#include "umka_compiler.h"
#include "umka_runtime_src.h"


void parseProgram(Umka *umka);


static void compilerSetAPI(Umka *umka)
{
    umka->api.umkaAlloc             = umkaAlloc;
    umka->api.umkaInit              = umkaInit;
    umka->api.umkaCompile           = umkaCompile;
    umka->api.umkaRun               = umkaRun;
    umka->api.umkaCall              = umkaCall;
    umka->api.umkaFree              = umkaFree;
    umka->api.umkaGetError          = umkaGetError;
    umka->api.umkaAlive             = umkaAlive;
    umka->api.umkaAsm               = umkaAsm;
    umka->api.umkaAddModule         = umkaAddModule;
    umka->api.umkaAddFunc           = umkaAddFunc;
    umka->api.umkaGetFunc           = umkaGetFunc;
    umka->api.umkaGetCallStack      = umkaGetCallStack;
    umka->api.umkaSetHook           = umkaSetHook;
    umka->api.umkaAllocData         = umkaAllocData;
    umka->api.umkaIncRef            = umkaIncRef;
    umka->api.umkaDecRef            = umkaDecRef;
    umka->api.umkaGetMapItem        = umkaGetMapItem;
    umka->api.umkaMakeStr           = umkaMakeStr;
    umka->api.umkaGetStrLen         = umkaGetStrLen;
    umka->api.umkaMakeDynArray      = umkaMakeDynArray;
    umka->api.umkaGetDynArrayLen    = umkaGetDynArrayLen;
    umka->api.umkaGetVersion        = umkaGetVersion;
    umka->api.umkaGetMemUsage       = umkaGetMemUsage;
    umka->api.umkaMakeFuncContext   = umkaMakeFuncContext;
    umka->api.umkaGetParam          = umkaGetParam;
    umka->api.umkaGetUpvalue        = umkaGetUpvalue;
    umka->api.umkaGetResult         = umkaGetResult;
    umka->api.umkaGetMetadata       = umkaGetMetadata;
    umka->api.umkaSetMetadata       = umkaSetMetadata;
    umka->api.umkaMakeStruct        = umkaMakeStruct;
    umka->api.umkaGetBaseType       = umkaGetBaseType;
}


static void compilerDeclareBuiltinTypes(Umka *umka)
{
    umka->voidType    = typeAdd(&umka->types, &umka->blocks, TYPE_VOID);
    umka->nullType    = typeAdd(&umka->types, &umka->blocks, TYPE_NULL);
    umka->int8Type    = typeAdd(&umka->types, &umka->blocks, TYPE_INT8);
    umka->int16Type   = typeAdd(&umka->types, &umka->blocks, TYPE_INT16);
    umka->int32Type   = typeAdd(&umka->types, &umka->blocks, TYPE_INT32);
    umka->intType     = typeAdd(&umka->types, &umka->blocks, TYPE_INT);
    umka->uint8Type   = typeAdd(&umka->types, &umka->blocks, TYPE_UINT8);
    umka->uint16Type  = typeAdd(&umka->types, &umka->blocks, TYPE_UINT16);
    umka->uint32Type  = typeAdd(&umka->types, &umka->blocks, TYPE_UINT32);
    umka->uintType    = typeAdd(&umka->types, &umka->blocks, TYPE_UINT);
    umka->boolType    = typeAdd(&umka->types, &umka->blocks, TYPE_BOOL);
    umka->charType    = typeAdd(&umka->types, &umka->blocks, TYPE_CHAR);
    umka->real32Type  = typeAdd(&umka->types, &umka->blocks, TYPE_REAL32);
    umka->realType    = typeAdd(&umka->types, &umka->blocks, TYPE_REAL);
    umka->strType     = typeAdd(&umka->types, &umka->blocks, TYPE_STR);

    umka->ptrVoidType = typeAddPtrTo(&umka->types, &umka->blocks, umka->voidType);
    umka->ptrNullType = typeAddPtrTo(&umka->types, &umka->blocks, umka->nullType);

    // any
    Type *anyType = typeAdd(&umka->types, &umka->blocks, TYPE_INTERFACE);

    typeAddField(&umka->types, anyType, umka->ptrVoidType, "#self");
    typeAddField(&umka->types, anyType, umka->ptrVoidType, "#selftype");

    umka->anyType = anyType;

    // fiber
    Type *fiberType = typeAdd(&umka->types, &umka->blocks, TYPE_FIBER);

    Type *fnType = typeAdd(&umka->types, &umka->blocks, TYPE_FN);
    typeAddParam(&umka->types, &fnType->sig, umka->anyType, "#upvalues", (Const){0});

    fnType->sig.resultType = umka->voidType;

    Type *fiberClosureType = typeAdd(&umka->types, &umka->blocks, TYPE_CLOSURE);
    typeAddField(&umka->types, fiberClosureType, fnType, "#fn");
    typeAddField(&umka->types, fiberClosureType, umka->anyType, "#upvalues");
    fiberType->base = fiberClosureType;

    umka->fiberType = fiberType;

    // __file
    Type *fileDataType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
    typeAddField(&umka->types, fileDataType, umka->ptrVoidType, "#data");

    umka->fileType = typeAddPtrTo(&umka->types, &umka->blocks, fileDataType);
}


static void compilerDeclareBuiltinIdents(Umka *umka)
{
    // Constants
    Const trueConst  = {.intVal = true};
    Const falseConst = {.intVal = false};
    Const nullConst  = {.ptrVal = 0};

    identAddConst(&umka->idents, &umka->modules, &umka->blocks, "true",  umka->boolType,    true, trueConst);
    identAddConst(&umka->idents, &umka->modules, &umka->blocks, "false", umka->boolType,    true, falseConst);
    identAddConst(&umka->idents, &umka->modules, &umka->blocks, "null",  umka->ptrNullType, true, nullConst);

    // Types
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "void",     umka->voidType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int8",     umka->int8Type,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int16",    umka->int16Type,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int32",    umka->int32Type,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int",      umka->intType,     true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint8",    umka->uint8Type,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint16",   umka->uint16Type,  true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint32",   umka->uint32Type,  true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint",     umka->uintType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "bool",     umka->boolType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "char",     umka->charType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "real32",   umka->real32Type,  true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "real",     umka->realType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "fiber",    umka->fiberType,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "any",      umka->anyType,     true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "__file",   umka->fileType,    true);

    // Built-in functions
    // I/O
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "printf",     umka->intType,     BUILTIN_PRINTF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "fprintf",    umka->intType,     BUILTIN_FPRINTF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sprintf",    umka->strType,     BUILTIN_SPRINTF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "scanf",      umka->intType,     BUILTIN_SCANF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "fscanf",     umka->intType,     BUILTIN_FSCANF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sscanf",     umka->intType,     BUILTIN_SSCANF);

    // Math
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "round",      umka->intType,     BUILTIN_ROUND);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "trunc",      umka->intType,     BUILTIN_TRUNC);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "ceil",       umka->intType,     BUILTIN_CEIL);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "floor",      umka->intType,     BUILTIN_FLOOR);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "abs",        umka->intType,     BUILTIN_ABS);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "fabs",       umka->realType,    BUILTIN_FABS);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sqrt",       umka->realType,    BUILTIN_SQRT);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sin",        umka->realType,    BUILTIN_SIN);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "cos",        umka->realType,    BUILTIN_COS);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "atan",       umka->realType,    BUILTIN_ATAN);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "atan2",      umka->realType,    BUILTIN_ATAN2);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "exp",        umka->realType,    BUILTIN_EXP);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "log",        umka->realType,    BUILTIN_LOG);

    // Memory
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "new",        umka->ptrVoidType, BUILTIN_NEW);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "make",       umka->ptrVoidType, BUILTIN_MAKE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "copy",       umka->ptrVoidType, BUILTIN_COPY);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "append",     umka->ptrVoidType, BUILTIN_APPEND);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "insert",     umka->ptrVoidType, BUILTIN_INSERT);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "delete",     umka->ptrVoidType, BUILTIN_DELETE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "slice",      umka->ptrVoidType, BUILTIN_SLICE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sort",       umka->voidType,    BUILTIN_SORT);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "len",        umka->intType,     BUILTIN_LEN);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "cap",        umka->intType,     BUILTIN_CAP);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sizeof",     umka->intType,     BUILTIN_SIZEOF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sizeofself", umka->intType,     BUILTIN_SIZEOFSELF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "selfptr",    umka->ptrVoidType, BUILTIN_SELFPTR);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "selfhasptr", umka->boolType,    BUILTIN_SELFHASPTR);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "selftypeeq", umka->boolType,    BUILTIN_SELFTYPEEQ);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "typeptr",    umka->ptrVoidType, BUILTIN_TYPEPTR);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "valid",      umka->boolType,    BUILTIN_VALID);

    // Maps
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "validkey",   umka->boolType,    BUILTIN_VALIDKEY);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "keys",       umka->ptrVoidType, BUILTIN_KEYS);

    // Fibers
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "resume",     umka->voidType,    BUILTIN_RESUME);

    // Misc
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "memusage",   umka->intType,     BUILTIN_MEMUSAGE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "exit",       umka->voidType,    BUILTIN_EXIT);
}


static void compilerDeclareExternalFuncs(Umka *umka, bool fileSystemEnabled)
{
    externalAdd(&umka->externals, "rtlmemcpy",      &rtlmemcpy,                                           true);
    externalAdd(&umka->externals, "rtlstdin",       &rtlstdin,                                            true);
    externalAdd(&umka->externals, "rtlstdout",      &rtlstdout,                                           true);
    externalAdd(&umka->externals, "rtlstderr",      &rtlstderr,                                           true);
    externalAdd(&umka->externals, "rtlfopen",       fileSystemEnabled ? &rtlfopen  : &rtlfopenSandbox,    true);
    externalAdd(&umka->externals, "rtlfclose",      fileSystemEnabled ? &rtlfclose : &rtlfcloseSandbox,   true);
    externalAdd(&umka->externals, "rtlfread",       fileSystemEnabled ? &rtlfread  : &rtlfreadSandbox,    true);
    externalAdd(&umka->externals, "rtlfwrite",      fileSystemEnabled ? &rtlfwrite : &rtlfwriteSandbox,   true);
    externalAdd(&umka->externals, "rtlfseek",       fileSystemEnabled ? &rtlfseek  : &rtlfseekSandbox,    true);
    externalAdd(&umka->externals, "rtlftell",       fileSystemEnabled ? &rtlftell  : &rtlftellSandbox,    true);
    externalAdd(&umka->externals, "rtlremove",      fileSystemEnabled ? &rtlremove : &rtlremoveSandbox,   true);
    externalAdd(&umka->externals, "rtlfeof",        fileSystemEnabled ? &rtlfeof   : &rtlfeofSandbox,     true);
    externalAdd(&umka->externals, "rtlfflush",      &rtlfflush,                                           true);
    externalAdd(&umka->externals, "rtltime",        &rtltime,                                             true);
    externalAdd(&umka->externals, "rtlclock",       &rtlclock,                                            true);
    externalAdd(&umka->externals, "rtllocaltime",   &rtllocaltime,                                        true);
    externalAdd(&umka->externals, "rtlgmtime",      &rtlgmtime,                                           true);
    externalAdd(&umka->externals, "rtlmktime",      &rtlmktime,                                           true);
    externalAdd(&umka->externals, "rtlgetenv",      fileSystemEnabled ? &rtlgetenv : &rtlgetenvSandbox,   true);
    externalAdd(&umka->externals, "rtlsystem",      fileSystemEnabled ? &rtlsystem : &rtlsystemSandbox,   true);
    externalAdd(&umka->externals, "rtltrace",       &rtltrace,                                            true);
}


void compilerInit(Umka *umka, const char *fileName, const char *sourceString, int stackSize, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled)
{
#ifdef _WIN32
    umka->originalInputCodepage = GetConsoleCP();
    umka->originalOutputCodepage = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    compilerSetAPI(umka);

    storageInit  (&umka->storage, &umka->error);
    moduleInit   (&umka->modules, &umka->storage, implLibsEnabled, &umka->error);
    blocksInit   (&umka->blocks, &umka->error);
    externalInit (&umka->externals, &umka->storage);
    typeInit     (&umka->types, &umka->storage, &umka->error);
    identInit    (&umka->idents, &umka->storage, &umka->debug, &umka->error);
    constInit    (&umka->consts, &umka->error);
    genInit      (&umka->gen, &umka->storage, &umka->debug, &umka->error);
    vmInit       (&umka->vm, &umka->storage, stackSize, fileSystemEnabled, &umka->error);

    vmReset(&umka->vm, umka->gen.code, umka->gen.debugPerInstr);

    umka->lex.fileName = "<unknown>";
    umka->lex.tok.line = 1;
    umka->lex.tok.pos = 1;
    umka->debug.fnName = "<unknown>";

    char filePath[DEFAULT_STR_LEN + 1] = "";
    moduleAssertRegularizePath(&umka->modules, fileName, umka->modules.curFolder, filePath, DEFAULT_STR_LEN + 1);

    umka->lex.fileName = filePath;

    lexInit(&umka->lex, &umka->storage, &umka->debug, filePath, sourceString, false, &umka->error);

    umka->argc  = argc;
    umka->argv  = argv;

    umka->blocks.module = moduleAdd(&umka->modules, "#universe");

    compilerDeclareBuiltinTypes (umka);
    compilerDeclareBuiltinIdents(umka);
    compilerDeclareExternalFuncs(umka, fileSystemEnabled);

    // Command-line-arguments
    Type *argvType = typeAdd(&umka->types, &umka->blocks, TYPE_ARRAY);
    argvType->base = umka->strType;
    typeResizeArray(argvType, umka->argc);

    Ident *rtlargv = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, "rtlargv", argvType, true);
    char **argArray = (char **)rtlargv->ptr;

    for (int i = 0; i < umka->argc; i++)
    {
        argArray[i] = storageAddStr(&umka->storage, strlen(umka->argv[i]));
        strcpy(argArray[i], umka->argv[i]);
    }

    // Embedded standard library modules
    const int numRuntimeModules = sizeof(runtimeModuleSources) / sizeof(runtimeModuleSources[0]);
    for (int i = 0; i < numRuntimeModules; i++)
    {
        char runtimeModulePath[DEFAULT_STR_LEN + 1] = "";
        moduleAssertRegularizePath(&umka->modules, runtimeModuleNames[i], umka->modules.curFolder, runtimeModulePath, DEFAULT_STR_LEN + 1);

        const bool runtimeModuleTrusted = strcmp(runtimeModuleNames[i], "std.um") == 0;
        moduleAddSource(&umka->modules, runtimeModulePath, runtimeModuleSources[i], runtimeModuleTrusted);
    }
}


void compilerFree(Umka *umka)
{
    vmFree          (&umka->vm);
    moduleFree      (&umka->modules);
    storageFree     (&umka->storage);

#ifdef _WIN32
    SetConsoleCP(umka->originalInputCodepage);
    SetConsoleOutputCP(umka->originalOutputCodepage);
#endif
}


void compilerCompile(Umka *umka)
{
    parseProgram(umka);
    vmReset(&umka->vm, umka->gen.code, umka->gen.debugPerInstr);
}


void compilerRun(Umka *umka)
{
    vmRun(&umka->vm, NULL);
}


void compilerCall(Umka *umka, UmkaFuncContext *fn)
{
    vmRun(&umka->vm, fn);
}


char *compilerAsm(Umka *umka)
{
    const int chars = genAsm(&umka->gen, NULL, 0);
    if (chars < 0)
        return NULL;

    char *buf = storageAdd(&umka->storage, chars + 1);
    genAsm(&umka->gen, buf, chars);
    buf[chars] = 0;
    return buf;
}


bool compilerAddModule(Umka *umka, const char *fileName, const char *sourceString)
{
    char modulePath[DEFAULT_STR_LEN + 1] = "";
    moduleAssertRegularizePath(&umka->modules, fileName, umka->modules.curFolder, modulePath, DEFAULT_STR_LEN + 1);

    if (moduleFindSource(&umka->modules, modulePath))
        return false;

    moduleAddSource(&umka->modules, modulePath, sourceString, false);
    return true;
}


bool compilerAddFunc(Umka *umka, const char *name, UmkaExternFunc func)
{
    if (externalFind(&umka->externals, name))
        return false;

    externalAdd(&umka->externals, name, func, false);
    return true;
}


bool compilerGetFunc(Umka *umka, const char *moduleName, const char *funcName, UmkaFuncContext *fn)
{
    int module = 1;
    if (moduleName)
    {
        char modulePath[DEFAULT_STR_LEN + 1] = "";
        moduleAssertRegularizePath(&umka->modules, moduleName, umka->modules.curFolder, modulePath, DEFAULT_STR_LEN + 1);
        module = moduleFind(&umka->modules, modulePath);
    }

    const Ident *fnIdent = identFind(&umka->idents, &umka->modules, &umka->blocks, module, funcName, NULL, false);
    if (!fnIdent || fnIdent->kind != IDENT_CONST || fnIdent->type->kind != TYPE_FN)
        return false;

    identSetUsed(fnIdent);

    compilerMakeFuncContext(umka, fnIdent->type, fnIdent->offset, fn);
    return true;
}


void compilerMakeFuncContext(Umka *umka, const Type *fnType, int entryOffset, UmkaFuncContext *fn)
{
    fn->entryOffset = entryOffset;

    const int paramSlots = typeParamSizeTotal(&umka->types, &fnType->sig) / sizeof(Slot);
    fn->params = (UmkaStackSlot *)storageAdd(&umka->storage, (paramSlots + 4) * sizeof(Slot)) + 4;          // + 4 slots for compatibility with umkaGetParam()

    const ParamLayout *paramLayout = typeMakeParamLayout(&umka->types, &fnType->sig);
    fn->params[-4].ptrVal = (ParamLayout *)paramLayout;

    fn->result = storageAdd(&umka->storage, sizeof(Slot));
}
