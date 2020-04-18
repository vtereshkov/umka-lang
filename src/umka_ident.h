#ifndef UMKA_IDENT_H_INCLUDED
#define UMKA_IDENT_H_INCLUDED

#include "umka_common.h"
#include "umka_vm.h"


typedef enum
{
    // Built-in functions are treated specially, all other functions are either constants or variables of "fn" type
    IDENT_CONST,
    IDENT_VAR,
    IDENT_TYPE,
    IDENT_BUILTIN_FN
} IdentKind;


typedef struct tagIdent
{
    IdentKind kind;
    IdentName name;
    int hash;
    Type *type;
    int block;                  // Global identifiers are in block 0
    bool forward, inHeap;
    union
    {
        BuiltinFunc builtin;    // For built-in functions
        void *ptr;              // For functions and global variables
        int offset;             // For local variables
        Const constant;         // For constants
    };
    struct tagIdent *next;
} Ident;


typedef struct
{
    Ident *first, *last;
    ErrorFunc error;
} Idents;


void identInit(Idents *idents, ErrorFunc error);
void identFree(Idents *idents, int startBlock /* < 0 to free in all blocks*/);

Ident *identFind(Idents *idents, Blocks *blocks, char *name);
Ident *identAssertFind      (Idents *idents, Blocks *blocks, char *name);

void identAddConst      (Idents *idents, Blocks *blocks, char *name, Type *type, Const constant);
void identAddGlobalVar  (Idents *idents, Blocks *blocks, char *name, Type *type, void *ptr);
void identAddLocalVar   (Idents *idents, Blocks *blocks, char *name, Type *type, int offset);
void identAddType       (Idents *idents, Blocks *blocks, char *name, Type *type);
void identAddBuiltinFunc(Idents *idents, Blocks *blocks, char *name, Type *type, BuiltinFunc builtin);

int  identAllocStack(Idents *idents, Blocks *blocks, int size);
void identAllocVar  (Idents *idents, Types *types, Blocks *blocks, char *name, Type *type);
void identAllocParam(Idents *idents, Blocks *blocks, Signature *sig, int index);


#endif // UMKA_IDENT_H_INCLUDED
