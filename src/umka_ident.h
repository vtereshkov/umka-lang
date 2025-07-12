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
    const Type *type;
    int module, block;                  // Place of definition (global identifiers are in block 0)
    bool exported, globallyAllocated, used, temporary;
    int prototypeOffset;                // For function prototypes
    union
    {
        BuiltinFunc builtin;            // For built-in functions
        void *ptr;                      // For global variables
        int64_t offset;                 // For functions (code offset) or local variables (stack offset)
        Const constant;                 // For constants
        int64_t moduleVal;              // For modules
    };
    DebugInfo debug;
    struct tagIdent *next;
} Ident;


typedef struct
{
    Ident *first;
    Ident *lastTempVarForResult;
    int tempVarNameSuffix;
    Storage *storage;
    DebugInfo *debug;
    Error *error;
} Idents;


void identInit(Idents *idents, Storage *storage, DebugInfo *debug, Error *error);
void identFree(Idents *idents, int block);

const Ident *identFind            (const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name, const Type *rcvType, bool markAsUsed);
const Ident *identAssertFind      (const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name, const Type *rcvType);
const Ident *identFindModule      (const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name, bool markAsUsed);
const Ident *identAssertFindModule(const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name);

bool identIsOuterLocalVar (const Blocks *blocks, const Ident *ident);

Ident *identAddConst      (Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported, Const constant);
Ident *identAddTempConst  (Idents *idents, const Modules *modules, const Blocks *blocks, const Type *type, Const constant);
Ident *identAddGlobalVar  (Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported, void *ptr);
Ident *identAddLocalVar   (Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported, int offset);
Ident *identAddType       (Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported);
Ident *identAddBuiltinFunc(Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, BuiltinFunc builtin);
Ident *identAddModule     (Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, int moduleVal);

int    identAllocStack    (Idents *idents, const Types *types, Blocks *blocks, const Type *type);
Ident *identAllocVar      (Idents *idents, const Types *types, const Modules *modules, Blocks *blocks, const char *name, const Type *type, bool exported);
Ident *identAllocTempVar  (Idents *idents, const Types *types, const Modules *modules, Blocks *blocks, const Type *type, bool isFuncResult);
Ident *identAllocParam    (Idents *idents, const Types *types, const Modules *modules, const Blocks *blocks, const Signature *sig, int index);

const char *identMethodNameWithRcv(const Idents *idents, const Ident *method);

void identWarnIfUnused        (const Idents *idents, const Ident *ident);
void identWarnIfUnusedAll     (const Idents *idents, int block);
bool identIsMain              (const Ident *ident);

static inline bool identIsHidden(const char *name)
{
    return name[0] == '#';
}

static inline bool identIsPlaceholder(const char *name)
{
    return name[0] == '_' && name[1] == 0;
}

static inline void identSetUsed(const Ident *ident)
{
    ((Ident *)ident)->used = true;
}

#endif // UMKA_IDENT_H_INCLUDED
