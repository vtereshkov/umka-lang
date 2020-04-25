#ifndef UMKA_TYPES_H_INCLUDED
#define UMKA_TYPES_H_INCLUDED

#include "umka_common.h"
#include "umka_lexer.h"


typedef enum
{
    TYPE_NONE,
    TYPE_FORWARD,
    TYPE_VOID,
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT,
    TYPE_UINT8,
    TYPE_UINT16,
    TYPE_UINT32,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_REAL32,
    TYPE_REAL,
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_STR,
    TYPE_STRUCT,
    TYPE_FN
} TypeKind;


typedef union
{
    int64_t intVal;
    void *ptrVal;
    double realVal;
} Const;


typedef struct
{
    IdentName name;
    int hash;
    struct tagType *type;
    int offset;
} Field;


typedef struct
{
    IdentName name;
    int hash;
    struct tagType *type;
    Const defaultVal;
} Param;


typedef struct
{
    int numParams, numResults;
    Param *param[MAX_PARAMS];
    struct tagType *resultType[MAX_RESULTS];
} Signature;


typedef struct tagType
{
    TypeKind kind;
    int block;
    struct tagType *base;
    int numItems;
    union
    {
        Field *field[MAX_FIELDS];
        Signature sig;
        struct tagIdent *forwardIdent;
    };
    struct tagType *next;
} Type;


typedef struct
{
    Type *first, *last;
    ErrorFunc error;
} Types;


void typeInit(Types *types, ErrorFunc error);
void typeFree(Types *types, int startBlock /* < 0 to free in all blocks*/);

Type *typeAdd(Types *types, Blocks *blocks, TypeKind kind);
Type *typeAddPtrTo(Types *types, Blocks *blocks, Type *type);
int typeSize(Types *types, Type *type);

bool typeInteger(Type *type);
bool typeOrdinal(Type *type);
bool typeReal(Type *type);
bool typeStructured(Type *type);

bool typeEquivalent(Type *left, Type *right);
bool typeAssertEquivalent(Types *types, Type *left, Type *right);
bool typeCompatible(Type *left, Type *right);
bool typeAssertCompatible(Types *types, Type *left, Type *right);

bool typeValidOperator(Type *type, TokenKind op);
bool typeAssertValidOperator(Types *types, Type *type, TokenKind op);

bool typeAssertForwardResolved(Types *types);

Field *typeFindField(Type *structType, char *name);
Field *typeAssertFindField(Types *types, Type *structType, char *name);
void typeAddField(Types *types, Type *structType, Type *fieldType, char *name);

Param *typeFindParam(Signature *sig, char *name);
void typeAddParam(Types *types, Signature *sig, Type *type, char *name);

int typeParamSizeUpTo(Types *types, Signature *sig, int index);
int typeParamSizeTotal(Types *types, Signature *sig);

char *typeKindSpelling(TypeKind kind);
char *typeSpelling(Type *type, char *buf);


#endif // UMKA_TYPES_H_INCLUDED
