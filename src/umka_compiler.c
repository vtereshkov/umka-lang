#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "umka_compiler.h"


void parseProgram(Compiler *comp);


static void compilerDeclareBuiltinTypes(Compiler *comp)
{
    comp->voidType          = typeAdd(&comp->types, &comp->blocks, TYPE_VOID);
    comp->nullType          = typeAdd(&comp->types, &comp->blocks, TYPE_NULL);
    comp->int8Type          = typeAdd(&comp->types, &comp->blocks, TYPE_INT8);
    comp->int16Type         = typeAdd(&comp->types, &comp->blocks, TYPE_INT16);
    comp->int32Type         = typeAdd(&comp->types, &comp->blocks, TYPE_INT32);
    comp->intType           = typeAdd(&comp->types, &comp->blocks, TYPE_INT);
    comp->uint8Type         = typeAdd(&comp->types, &comp->blocks, TYPE_UINT8);
    comp->uint16Type        = typeAdd(&comp->types, &comp->blocks, TYPE_UINT16);
    comp->uint32Type        = typeAdd(&comp->types, &comp->blocks, TYPE_UINT32);
    comp->uintType          = typeAdd(&comp->types, &comp->blocks, TYPE_UINT);
    comp->boolType          = typeAdd(&comp->types, &comp->blocks, TYPE_BOOL);
    comp->charType          = typeAdd(&comp->types, &comp->blocks, TYPE_CHAR);
    comp->real32Type        = typeAdd(&comp->types, &comp->blocks, TYPE_REAL32);
    comp->realType          = typeAdd(&comp->types, &comp->blocks, TYPE_REAL);
    comp->strType           = typeAdd(&comp->types, &comp->blocks, TYPE_STR);
    comp->fiberType         = typeAdd(&comp->types, &comp->blocks, TYPE_FIBER);

    comp->ptrVoidType       = typeAddPtrTo(&comp->types, &comp->blocks, comp->voidType);
    comp->ptrNullType       = typeAddPtrTo(&comp->types, &comp->blocks, comp->nullType);
    comp->ptrFiberType      = typeAddPtrTo(&comp->types, &comp->blocks, comp->fiberType);
}


static void compilerDeclareBuiltinIdents(Compiler *comp)
{
    // Constants
    Const trueConst  = {.intVal = true};
    Const falseConst = {.intVal = false};
    Const nullConst  = {.ptrVal = 0};

    identAddConst(&comp->idents, &comp->modules, &comp->blocks, "true",  comp->boolType,    true, trueConst);
    identAddConst(&comp->idents, &comp->modules, &comp->blocks, "false", comp->boolType,    true, falseConst);
    identAddConst(&comp->idents, &comp->modules, &comp->blocks, "null",  comp->ptrNullType, true, nullConst);

    // Types
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "void",     comp->voidType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int8",     comp->int8Type,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int16",    comp->int16Type,   true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int32",    comp->int32Type,   true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int",      comp->intType,     true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "uint8",    comp->uint8Type,   true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "uint16",   comp->uint16Type,  true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "uint32",   comp->uint32Type,  true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "uint",     comp->uintType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "bool",     comp->boolType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "char",     comp->charType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "real32",   comp->real32Type,  true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "real",     comp->realType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "fiber",    comp->fiberType,   true);

    // Built-in functions
    // I/O
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "printf",     comp->intType,     BUILTIN_PRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fprintf",    comp->intType,     BUILTIN_FPRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sprintf",    comp->intType,     BUILTIN_SPRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "scanf",      comp->intType,     BUILTIN_SCANF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fscanf",     comp->intType,     BUILTIN_FSCANF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sscanf",     comp->intType,     BUILTIN_SSCANF);

    // Math
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "round",      comp->intType,     BUILTIN_ROUND);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "trunc",      comp->intType,     BUILTIN_TRUNC);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fabs",       comp->realType,    BUILTIN_FABS);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sqrt",       comp->realType,    BUILTIN_SQRT);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sin",        comp->realType,    BUILTIN_SIN);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "cos",        comp->realType,    BUILTIN_COS);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "atan",       comp->realType,    BUILTIN_ATAN);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "atan2",      comp->realType,    BUILTIN_ATAN2);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "exp",        comp->realType,    BUILTIN_EXP);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "log",        comp->realType,    BUILTIN_LOG);

    // Memory
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "new",        comp->ptrVoidType, BUILTIN_NEW);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "make",       comp->ptrVoidType, BUILTIN_MAKE);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "append",     comp->ptrVoidType, BUILTIN_APPEND);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "delete",     comp->ptrVoidType, BUILTIN_DELETE);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "len",        comp->intType,     BUILTIN_LEN);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sizeof",     comp->intType,     BUILTIN_SIZEOF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sizeofself", comp->intType,     BUILTIN_SIZEOFSELF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "selfhasptr", comp->boolType,    BUILTIN_SELFHASPTR);

    // Fibers
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fiberspawn", comp->ptrVoidType, BUILTIN_FIBERSPAWN);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fibercall",  comp->voidType,    BUILTIN_FIBERCALL);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fiberalive", comp->boolType,    BUILTIN_FIBERALIVE);

    // Misc
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "repr",       comp->strType,     BUILTIN_REPR);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "error",      comp->voidType,    BUILTIN_ERROR);
}


static void compilerDeclareExternalFuncs(Compiler *comp)
{
    externalAdd(&comp->externals, "rtlfopen",   &rtlfopen);
    externalAdd(&comp->externals, "rtlfclose",  &rtlfclose);
    externalAdd(&comp->externals, "rtlfread",   &rtlfread);
    externalAdd(&comp->externals, "rtlfwrite",  &rtlfwrite);
    externalAdd(&comp->externals, "rtlfseek",   &rtlfseek);
    externalAdd(&comp->externals, "rtlremove",  &rtlremove);
    externalAdd(&comp->externals, "rtltime",    &rtltime);
}


void compilerInit(Compiler *comp, const char *fileName, const char *sourceString, int storageSize, int stackSize, int argc, char **argv)
{
    storageInit  (&comp->storage, storageSize);
    moduleInit   (&comp->modules, &comp->error);
    blocksInit   (&comp->blocks, &comp->error);
    externalInit (&comp->externals);
    typeInit     (&comp->types, &comp->error);
    identInit    (&comp->idents, &comp->error);
    constInit    (&comp->consts, &comp->error);
    genInit      (&comp->gen, &comp->debug, &comp->error);
    vmInit       (&comp->vm, stackSize, &comp->error);
    lexInit      (&comp->lex, &comp->storage, &comp->debug, fileName, sourceString, &comp->error);

    comp->argc  = argc;
    comp->argv  = argv;

    comp->blocks.module = moduleAdd(&comp->modules, "__universe");

    compilerDeclareBuiltinTypes (comp);
    compilerDeclareBuiltinIdents(comp);
    compilerDeclareExternalFuncs(comp);

    // Command-line-arguments
    Ident *rtlargc = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, "rtlargc", comp->intType, true);
    Ident *rtlargv = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, "rtlargv", comp->ptrVoidType, true);

    *(int64_t *)(rtlargc->ptr) = comp->argc;
    *(void *  *)(rtlargv->ptr) = comp->argv;
}


void compilerFree(Compiler *comp)
{
    lexFree      (&comp->lex);
    vmFree       (&comp->vm);
    genFree      (&comp->gen);
    constFree    (&comp->consts);
    identFree    (&comp->idents, -1);
    typeFree     (&comp->types, -1);
    externalFree (&comp->externals);
    blocksFree   (&comp->blocks);
    moduleFree   (&comp->modules);
    storageFree  (&comp->storage);
}


void compilerCompile(Compiler *comp)
{
    parseProgram(comp);
}


void compilerRun(Compiler *comp)
{
    vmReset(&comp->vm, comp->gen.code);
    vmRun(&comp->vm, 0, 0, NULL, NULL);
}


void compilerCall(Compiler *comp, int entryOffset, int numParamSlots, Slot *params, Slot *result)
{
    vmReset(&comp->vm, comp->gen.code);
    vmRun(&comp->vm, entryOffset, numParamSlots, params, result);
}


void compilerAsm(Compiler *comp, char *buf)
{
    genAsm(&comp->gen, buf);
}


int compilerGetFunc(Compiler *comp, const char *moduleName, const char *funcName)
{
    int module = 1;
    if (moduleName)
        module = moduleFindByPath(&comp->modules, moduleName);

    Ident *fn = identFind(&comp->idents, &comp->modules, &comp->blocks, module, funcName, NULL);
    if (fn && fn->kind == IDENT_CONST && fn->type->kind == TYPE_FN)
        return fn->offset;
    return -1;
}



