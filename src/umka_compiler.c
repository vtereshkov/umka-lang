#include <stddef.h>

#include "umka_compiler.h"


void parseModule(Compiler *comp);


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
    comp->strType->numItems = DEFAULT_STR_LEN;
}


static void compilerDeclareBuiltinIdents(Compiler *comp)
{
    // Constants
    Const trueConst  = {.intVal = true};
    Const falseConst = {.intVal = false};
    Const nullConst  = {.ptrVal = NULL};

    identAddConst(&comp->idents, &comp->blocks, "true",  comp->boolType, trueConst);
    identAddConst(&comp->idents, &comp->blocks, "false", comp->boolType, falseConst);
    identAddConst(&comp->idents, &comp->blocks, "null",  comp->ptrVoidType, nullConst);

    // Types
    identAddType(&comp->idents, &comp->blocks,  "void",   comp->voidType);
    identAddType(&comp->idents, &comp->blocks,  "int8",   comp->int8Type);
    identAddType(&comp->idents, &comp->blocks,  "int16",  comp->int16Type);
    identAddType(&comp->idents, &comp->blocks,  "int32",  comp->int32Type);
    identAddType(&comp->idents, &comp->blocks,  "int",    comp->intType);
    identAddType(&comp->idents, &comp->blocks,  "uint8",  comp->uint8Type);
    identAddType(&comp->idents, &comp->blocks,  "uint16", comp->uint16Type);
    identAddType(&comp->idents, &comp->blocks,  "uint32", comp->uint32Type);
    identAddType(&comp->idents, &comp->blocks,  "bool",   comp->boolType);
    identAddType(&comp->idents, &comp->blocks,  "char",   comp->charType);
    identAddType(&comp->idents, &comp->blocks,  "real32", comp->real32Type);
    identAddType(&comp->idents, &comp->blocks,  "real",   comp->realType);

    // Built-in functions
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "printf",  comp->voidType, BUILTIN_PRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "fprintf", comp->voidType, BUILTIN_FPRINTF);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "scanf",   comp->voidType, BUILTIN_SCANF);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "fscanf",  comp->voidType, BUILTIN_FSCANF);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "round",   comp->intType,  BUILTIN_ROUND);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "trunc",   comp->intType,  BUILTIN_TRUNC);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "fabs",    comp->realType, BUILTIN_FABS);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "sqrt",    comp->realType, BUILTIN_SQRT);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "sin",     comp->realType, BUILTIN_SIN);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "cos",     comp->realType, BUILTIN_COS);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "atan",    comp->realType, BUILTIN_ATAN);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "exp",     comp->realType, BUILTIN_EXP);
    identAddBuiltinFunc(&comp->idents, &comp->blocks, "log",     comp->realType, BUILTIN_LOG);
}


void compilerInit(Compiler *comp, char *fileName, int storageCapacity, ErrorFunc compileError)
{
    storageInit (&comp->storage, storageCapacity);
    blocksInit  (&comp->blocks, compileError);
    lexInit     (&comp->lex, fileName, &comp->storage, compileError);
    typeInit    (&comp->types, compileError);
    identInit   (&comp->idents, compileError);
    constInit   (&comp->consts, compileError);
    genInit     (&comp->gen, compileError);

    comp->error = compileError;

    compilerDeclareBuiltinTypes(comp);
    compilerDeclareBuiltinIdents(comp);
}


void compilerFree(Compiler *comp)
{
    genFree     (&comp->gen);
    constFree   (&comp->consts);
    identFree   (&comp->idents, -1);
    typeFree    (&comp->types, -1);
    lexFree     (&comp->lex);
    blocksFree  (&comp->blocks);
    storageFree (&comp->storage);
}


void compilerCompile(Compiler *comp)
{
    lexNext(&comp->lex);
    parseModule(comp);
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



