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
    IDENT_BUILTIN_FN,
    IDENT_MODULE
} IdentKind;


typedef struct tagIdent
{
    IdentKind kind;
    IdentName name;
    unsigned int hash;
    Type *type;
    int module, block;                  // Place of definition (global identifiers are in block 0)
    bool exported, globallyAllocated, used;
    int prototypeOffset;                // For function prototypes
    union
    {
        BuiltinFunc builtin;            // For built-in functions
        void *ptr;                      // For global variables
        int64_t offset;                 // For functions (code offset) or local variables (stack offset)
        Const constant;                 // For constants
        int64_t moduleVal;              // For modules
    };
    struct tagIdent *next;
} Ident;


typedef struct
{
    Ident *first, *last;
    int tempVarNameSuffix;
    Error *error;
} Idents;


void identInit(Idents *idents, Error *error);
void identFree(Idents *idents, int startBlock /* < 0 to free in all blocks*/);

Ident *identFind          (Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, Type *rcvType, bool markAsUsed);
Ident *identAssertFind    (Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, Type *rcvType);

bool identIsOuterLocalVar (Blocks *blocks, Ident *ident);

Ident *identAddConst      (Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported, Const constant);
Ident *identAddGlobalVar  (Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported, void *ptr);
Ident *identAddLocalVar   (Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported, int offset);
Ident *identAddType       (Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported);
Ident *identAddBuiltinFunc(Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, BuiltinFunc builtin);
Ident *identAddModule     (Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, int moduleVal);

int    identAllocStack    (Idents *idents, Types *types, Blocks *blocks, Type *type);
Ident *identAllocVar      (Idents *idents, Types *types, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported);
Ident *identAllocParam    (Idents *idents, Types *types, Modules *modules, Blocks *blocks, Signature *sig, int index);

char *identTempVarName      (Idents *idents, char *buf);
char *identMethodNameWithRcv(Ident *method, char *buf, int size);

void identWarnIfUnused        (Idents *idents, Ident *ident);
void identWarnIfUnusedAll     (Idents *idents, int block);
bool identIsMain              (Ident *ident);

#endif // UMKA_IDENT_H_INCLUDED
