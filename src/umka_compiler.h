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


typedef struct tagUmka
{
    // User API - must be the first field
    UmkaAPI     api;

    // Compiler components
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
    const Type
         *voidType,
         *nullType,
         *int8Type,  *int16Type,  *int32Type,  *intType,
         *uint8Type, *uint16Type, *uint32Type, *uintType,
         *boolType,
         *charType,
         *real32Type, *realType,
         *strType,
         *fiberType,
         *ptrVoidType, *ptrNullType,
         *anyType,
         *fileType;

    // Arbitrary metadata
    void *metadata;

    // Original codepages (Windows only)
    unsigned int originalInputCodepage, originalOutputCodepage;

} Umka;


void compilerInit               (Umka *umka, const char *fileName, const char *sourceString, int stackSize, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled);
void compilerFree               (Umka *umka);
void compilerCompile            (Umka *umka);
void compilerRun                (Umka *umka);
void compilerCall               (Umka *umka, UmkaFuncContext *fn);
char *compilerAsm               (Umka *umka);
bool compilerAddModule          (Umka *umka, const char *fileName, const char *sourceString);
bool compilerAddFunc            (Umka *umka, const char *name, UmkaExternFunc func);
bool compilerGetFunc            (Umka *umka, const char *moduleName, const char *funcName, UmkaFuncContext *fn);
void compilerMakeFuncContext    (Umka *umka, const Type *fnType, int entryOffset, UmkaFuncContext *fn);

#endif // UMKA_COMPILER_H_INCLUDED
