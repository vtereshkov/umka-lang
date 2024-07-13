#ifndef UMKA_COMPILER_H_INCLUDED
#define UMKA_COMPILER_H_INCLUDED

#include "umka_common.h"
#include "umka_lexer.h"
#include "umka_types.h"
#include "umka_vm.h"
#include "umka_gen.h"
#include "umka_ident.h"
#include "umka_const.h"
#include "umka_runtime.h"
#include "umka_api.h"


typedef struct
{
    UmkaAPI     api;        // Must be the first field

    Storage     storage;
    Modules     modules;
    Blocks      blocks;
    Externals   externals;
    Lexer       lex;
    Types       types;
    Idents      idents;
    Consts      consts;
    CodeGen     gen;
    VM          vm;
    DebugInfo   debug;
    Error       error;

    // Pointers to built-in types
    Type *voidType,
         *nullType,
         *int8Type,  *int16Type,  *int32Type,  *intType,
         *uint8Type, *uint16Type, *uint32Type, *uintType,
         *boolType,
         *charType,
         *real32Type, *realType,
         *strType,
         *fiberType,
         *ptrVoidType, *ptrNullType,
         *anyType;

    // Command-line arguments
    int argc;
    char **argv;

    // Original codepage (Windows only)
    unsigned int originalCodepage;

} Compiler;


void compilerInit               (Compiler *comp, const char *fileName, const char *sourceString, int stackSize, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled);
void compilerFree               (Compiler *comp);
void compilerCompile            (Compiler *comp);
void compilerRun                (Compiler *comp);
void compilerCall               (Compiler *comp, FuncContext *fn);
char *compilerAsm               (Compiler *comp);
bool compilerAddModule          (Compiler *comp, const char *fileName, const char *sourceString);
bool compilerAddFunc            (Compiler *comp, const char *name, ExternFunc func);
bool compilerGetFunc            (Compiler *comp, const char *moduleName, const char *funcName, FuncContext *fn);
void compilerMakeFuncContext    (Compiler *comp, Type *fnType, int entryOffset, FuncContext *fn);

#endif // UMKA_COMPILER_H_INCLUDED
