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
    umka->api.umkaGetParamType      = umkaGetParamType;
    umka->api.umkaGetResultType     = umkaGetResultType;
    umka->api.umkaGetFieldType      = umkaGetFieldType;
    umka->api.umkaGetMapKeyType     = umkaGetMapKeyType;
    umka->api.umkaGetMapItemType    = umkaGetMapItemType;         
}


static void compilerDeclareBuiltinIdents(Umka *umka)
{
    // Constants
    Const trueConst  = {.intVal = true};
    Const falseConst = {.intVal = false};
    Const nullConst  = {.ptrVal = 0};

    identAddConst(&umka->idents, &umka->modules, &umka->blocks, "true",  umka->types.predecl.boolType,    true, trueConst);
    identAddConst(&umka->idents, &umka->modules, &umka->blocks, "false", umka->types.predecl.boolType,    true, falseConst);
    identAddConst(&umka->idents, &umka->modules, &umka->blocks, "null",  umka->types.predecl.ptrNullType, true, nullConst);

    // Types
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "void",     umka->types.predecl.voidType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int8",     umka->types.predecl.int8Type,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int16",    umka->types.predecl.int16Type,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int32",    umka->types.predecl.int32Type,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "int",      umka->types.predecl.intType,     true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint8",    umka->types.predecl.uint8Type,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint16",   umka->types.predecl.uint16Type,  true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint32",   umka->types.predecl.uint32Type,  true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "uint",     umka->types.predecl.uintType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "bool",     umka->types.predecl.boolType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "char",     umka->types.predecl.charType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "real32",   umka->types.predecl.real32Type,  true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "real",     umka->types.predecl.realType,    true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "fiber",    umka->types.predecl.fiberType,   true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "any",      umka->types.predecl.anyType,     true);
    identAddType(&umka->idents, &umka->modules, &umka->blocks,  "__file",   umka->types.predecl.fileType,    true);

    // Built-in functions
    // I/O
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "printf",     umka->types.predecl.intType,     BUILTIN_PRINTF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "fprintf",    umka->types.predecl.intType,     BUILTIN_FPRINTF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sprintf",    umka->types.predecl.strType,     BUILTIN_SPRINTF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "scanf",      umka->types.predecl.intType,     BUILTIN_SCANF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "fscanf",     umka->types.predecl.intType,     BUILTIN_FSCANF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sscanf",     umka->types.predecl.intType,     BUILTIN_SSCANF);

    // Math
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "round",      umka->types.predecl.intType,     BUILTIN_ROUND);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "trunc",      umka->types.predecl.intType,     BUILTIN_TRUNC);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "ceil",       umka->types.predecl.intType,     BUILTIN_CEIL);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "floor",      umka->types.predecl.intType,     BUILTIN_FLOOR);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "abs",        umka->types.predecl.intType,     BUILTIN_ABS);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "fabs",       umka->types.predecl.realType,    BUILTIN_FABS);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sqrt",       umka->types.predecl.realType,    BUILTIN_SQRT);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sin",        umka->types.predecl.realType,    BUILTIN_SIN);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "cos",        umka->types.predecl.realType,    BUILTIN_COS);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "atan",       umka->types.predecl.realType,    BUILTIN_ATAN);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "atan2",      umka->types.predecl.realType,    BUILTIN_ATAN2);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "exp",        umka->types.predecl.realType,    BUILTIN_EXP);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "log",        umka->types.predecl.realType,    BUILTIN_LOG);

    // Memory
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "new",        umka->types.predecl.ptrVoidType, BUILTIN_NEW);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "make",       umka->types.predecl.ptrVoidType, BUILTIN_MAKE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "copy",       umka->types.predecl.ptrVoidType, BUILTIN_COPY);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "append",     umka->types.predecl.ptrVoidType, BUILTIN_APPEND);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "insert",     umka->types.predecl.ptrVoidType, BUILTIN_INSERT);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "delete",     umka->types.predecl.ptrVoidType, BUILTIN_DELETE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "slice",      umka->types.predecl.ptrVoidType, BUILTIN_SLICE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sort",       umka->types.predecl.voidType,    BUILTIN_SORT);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "len",        umka->types.predecl.intType,     BUILTIN_LEN);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "cap",        umka->types.predecl.intType,     BUILTIN_CAP);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sizeof",     umka->types.predecl.intType,     BUILTIN_SIZEOF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "sizeofself", umka->types.predecl.intType,     BUILTIN_SIZEOFSELF);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "selfptr",    umka->types.predecl.ptrVoidType, BUILTIN_SELFPTR);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "selfhasptr", umka->types.predecl.boolType,    BUILTIN_SELFHASPTR);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "selftypeeq", umka->types.predecl.boolType,    BUILTIN_SELFTYPEEQ);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "typeptr",    umka->types.predecl.ptrVoidType, BUILTIN_TYPEPTR);        // Deprecated
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "valid",      umka->types.predecl.boolType,    BUILTIN_VALID);

    // Maps
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "validkey",   umka->types.predecl.boolType,    BUILTIN_VALIDKEY);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "keys",       umka->types.predecl.ptrVoidType, BUILTIN_KEYS);

    // Fibers
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "resume",     umka->types.predecl.voidType,    BUILTIN_RESUME);

    // Misc
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "memusage",   umka->types.predecl.intType,     BUILTIN_MEMUSAGE);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "leaksan",    umka->types.predecl.voidType,    BUILTIN_LEAKSAN);
    identAddBuiltinFunc(&umka->idents, &umka->modules, &umka->blocks, "exit",       umka->types.predecl.voidType,    BUILTIN_EXIT);
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


static void compilerSetCodepage(Umka *umka)
{
#ifdef _WIN32
    umka->originalInputCodepage = GetConsoleCP();
    umka->originalOutputCodepage = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif    
}


static void compilerRestoreCodepage(Umka *umka)
{
#ifdef _WIN32
    SetConsoleCP(umka->originalInputCodepage);
    SetConsoleOutputCP(umka->originalOutputCodepage);
#endif  
}


void compilerInit(Umka *umka, const char *fileName, const char *sourceString, int stackSize, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled)
{
    compilerSetCodepage(umka);

    compilerSetAPI(umka);

    storageInit  (&umka->storage, &umka->error);
    moduleInit   (&umka->modules, &umka->storage, implLibsEnabled, &umka->error);
    blocksInit   (&umka->blocks, &umka->error);
    externalInit (&umka->externals, &umka->storage);
    typeInit     (&umka->types, &umka->blocks, &umka->storage, &umka->error);
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

    umka->blocks.module = moduleAdd(&umka->modules, "#universe");

    compilerDeclareBuiltinIdents(umka);
    compilerDeclareExternalFuncs(umka, fileSystemEnabled);

    // Command-line-arguments
    Type *argvType = typeAdd(&umka->types, &umka->blocks, TYPE_ARRAY);
    typeSetBase(argvType, umka->types.predecl.strType);
    typeResizeArray(argvType, argc);

    const Ident *rtlargv = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, "rtlargv", argvType, true);
    char **argArray = (char **)rtlargv->ptr;

    for (int i = 0; i < argc; i++)
    {
        argArray[i] = storageAddStr(&umka->storage, strlen(argv[i]));
        strcpy(argArray[i], argv[i]);
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
    if (vmAlive(&umka->vm))
        vmCleanup(&umka->vm);

    vmFree      (&umka->vm);
    moduleFree  (&umka->modules);
    storageFree (&umka->storage);

    compilerRestoreCodepage(umka);
}


void compilerCompile(Umka *umka)
{
    parseProgram(umka);
    vmReset(&umka->vm, umka->gen.code, umka->gen.debugPerInstr);
}


void compilerRun(Umka *umka)
{
    if (umka->mainFn.entryOffset > 0)
        vmCall(&umka->vm, &umka->mainFn);
}


void compilerCall(Umka *umka, UmkaFuncContext *fn)
{
    vmCall(&umka->vm, fn);
}


char *compilerAsm(Umka *umka)
{
    const int chars = genAsm(&umka->gen, &umka->idents, NULL, 0);
    if (chars < 0)
        return NULL;

    char *buf = storageAdd(&umka->storage, chars + 1);
    genAsm(&umka->gen, &umka->idents, buf, chars);
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

    const int paramSlots = typeParamSizeTotal(&umka->types, fnType->sig) / sizeof(Slot);
    fn->params = (UmkaStackSlot *)storageAdd(&umka->storage, (paramSlots + 4) * sizeof(Slot)) + 4;          // + 4 slots for compatibility with umkaGetParam()

    const ParamLayout *paramLayout = typeMakeParamLayout(&umka->types, fnType->sig);
    *vmGetParamLayout(fn->params) = paramLayout;

    fn->result = storageAdd(&umka->storage, sizeof(Slot));
}
