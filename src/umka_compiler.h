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


typedef struct
{
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
    ErrorFunc   error;

    // Pointers to built-in types
    Type *voidType,
         *nullType,
         *int8Type,  *int16Type,  *int32Type,  *intType,
         *uint8Type, *uint16Type, *uint32Type,
         *boolType,
         *charType,
         *real32Type, *realType,
         *ptrVoidType,
         *ptrNullType,
         *strType;

    // Command-line arguments
    int argc;
    char **argv;

} Compiler;


void compilerInit(Compiler *comp, char *fileName, int storageCapacity, int argc, char **argv, ErrorFunc compileError);
void compilerFree(Compiler *comp);
void compilerCompile(Compiler *comp);
void compilerRun(Compiler *comp, int stackSize /* slots */, ErrorFunc runtimeError);
void compilerAsm(Compiler *comp, char *buf);

#endif // UMKA_COMPILER_H_INCLUDED
