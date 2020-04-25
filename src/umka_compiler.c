#include <stddef.h>
#include <stdio.h>

#include "umka_compiler.h"


void parseProgram(Compiler *comp);


static void compilerDeclareBuiltinTypes(Compiler *comp)
{
    typeAdd(&comp->types, &comp->blocks, TYPE_VOID);    comp->voidType   = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_INT8);    comp->int8Type   = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_INT16);   comp->int16Type  = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_INT32);   comp->int32Type  = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_INT);     comp->intType    = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_UINT8);   comp->uint8Type  = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_UINT16);  comp->uint16Type = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_UINT32);  comp->uint32Type = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_BOOL);    comp->boolType   = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_CHAR);    comp->charType   = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_REAL32);  comp->real32Type = comp->types.last;
    typeAdd(&comp->types, &comp->blocks, TYPE_REAL);    comp->realType   = comp->types.last;

    comp->ptrVoidType = typeAddPtrTo(&comp->types, &comp->blocks, comp->voidType);

    comp->strType = typeAdd(&comp->types, &comp->blocks, TYPE_STR);
    comp->strType->base = comp->charType;
    comp->strType->numItems = DEFAULT_STR_LEN + 1;
}


static void compilerDeclareBuiltinIdents(Compiler *comp)
{
    // Constants
    Const trueConst  = {.intVal = true};
    Const falseConst = {.intVal = false};
    Const nullConst  = {.ptrVal = NULL};

    identAddConst(&comp->idents, &comp->modules, &comp->blocks, "true",  comp->boolType,    true, trueConst);
    identAddConst(&comp->idents, &comp->modules, &comp->blocks, "false", comp->boolType,    true, falseConst);
    identAddConst(&comp->idents, &comp->modules, &comp->blocks, "null",  comp->ptrVoidType, true, nullConst);

    // Types
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "void",     comp->voidType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int8",     comp->int8Type,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int16",    comp->int16Type,   true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int32",    comp->int32Type,   true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "int",      comp->intType,     true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "uint8",    comp->uint8Type,   true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "uint16",   comp->uint16Type,  true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "uint32",   comp->uint32Type,  true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "bool",     comp->boolType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "char",     comp->charType,    true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "real32",   comp->real32Type,  true);
    identAddType(&comp->idents, &comp->modules, &comp->blocks,  "real",     comp->realType,    true);

    // Built-in functions
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "printf",  comp->intType, BUILTIN_PRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fprintf", comp->intType, BUILTIN_FPRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sprintf", comp->intType, BUILTIN_SPRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "scanf",   comp->intType, BUILTIN_SCANF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fscanf",  comp->intType, BUILTIN_FSCANF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sscanf",  comp->intType, BUILTIN_SSCANF);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "round",   comp->intType,  BUILTIN_ROUND);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "trunc",   comp->intType,  BUILTIN_TRUNC);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "fabs",    comp->realType, BUILTIN_FABS);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sqrt",    comp->realType, BUILTIN_SQRT);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "sin",     comp->realType, BUILTIN_SIN);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "cos",     comp->realType, BUILTIN_COS);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "atan",    comp->realType, BUILTIN_ATAN);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "exp",     comp->realType, BUILTIN_EXP);
    identAddBuiltinFunc(&comp->idents, &comp->modules, &comp->blocks, "log",     comp->realType, BUILTIN_LOG);
}


void compilerInit(Compiler *comp, char *fileName, int storageCapacity, ErrorFunc compileError)
{
    storageInit  (&comp->storage, storageCapacity);
    moduleInit   (&comp->modules, compileError);
    blocksInit   (&comp->blocks, compileError);
    externalInit (&comp->externals);
    lexInit      (&comp->lex, fileName, &comp->storage, compileError);
    typeInit     (&comp->types, compileError);
    identInit    (&comp->idents, compileError);
    constInit    (&comp->consts, compileError);
    genInit      (&comp->gen, compileError);

    comp->error = compileError;
}


void compilerFree(Compiler *comp)
{
    genFree      (&comp->gen);
    constFree    (&comp->consts);
    identFree    (&comp->idents, -1);
    typeFree     (&comp->types, -1);
    lexFree      (&comp->lex);
    externalFree (&comp->externals);
    blocksFree   (&comp->blocks);
    moduleFree   (&comp->modules);
    storageFree  (&comp->storage);
}



void extfn(Slot *params)
{
    int x = params->intVal;
    printf("External call: %d\n", x);
}



void compilerCompile(Compiler *comp)
{
    comp->blocks.module = moduleAdd(&comp->modules, "__universe");

    compilerDeclareBuiltinTypes(comp);
    compilerDeclareBuiltinIdents(comp);

    externalAdd(&comp->externals, "extfn", &extfn);

    parseProgram(comp);
}


void compilerRun(Compiler *comp, int stackSize, ErrorFunc runtimeError)
{
    vmInit(&comp->vm, comp->gen.code, stackSize, runtimeError);
    vmRun (&comp->vm);
    vmFree(&comp->vm);
}


void compilerAsm(Compiler *comp, char *buf)
{
    genAsm(&comp->gen, buf);
}



