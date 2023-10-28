#ifndef UMKA_TYPES_H_INCLUDED
#define UMKA_TYPES_H_INCLUDED

#include <string.h>
#include <limits.h>
#include <float.h>

#include "umka_common.h"
#include "umka_lexer.h"


typedef enum
{
    TYPE_NONE,
    TYPE_FORWARD,
    TYPE_VOID,
    TYPE_NULL,          // Base type for 'null' constant only
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT,
    TYPE_UINT8,
    TYPE_UINT16,
    TYPE_UINT32,
    TYPE_UINT,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_REAL32,
    TYPE_REAL,
    TYPE_PTR,
    TYPE_WEAKPTR,       // Actually a handle that stores the heap page ID and the offset within the page: (pageId << 32) | pageOffset
    TYPE_ARRAY,
    TYPE_DYNARRAY,
    TYPE_STR,           // Pointer of a special kind that admits assignment of string literals, concatenation and comparison by content
    TYPE_MAP,
    TYPE_STRUCT,
    TYPE_INTERFACE,
    TYPE_CLOSURE,
    TYPE_FIBER,         // Pointer of a special kind
    TYPE_FN
} TypeKind;


typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    void *ptrVal;
    uint64_t weakPtrVal;
    double realVal;
} Const;


typedef struct
{
    IdentName name;
    unsigned int hash;
    struct tagType *type;
    int offset;
} Field;


typedef struct
{
    IdentName name;
    unsigned int hash;
    struct tagType *type;
    Const defaultVal;
} Param;


typedef struct
{
    int numParams, numDefaultParams;
    bool method;
    int offsetFromSelf;                     // For interface methods
    Param *param[MAX_PARAMS];
    struct tagType *resultType;
} Signature;


typedef struct tagType
{
    TypeKind kind;
    int block;
    struct tagType *base;                   // For pointers, arrays and maps (for maps, denotes the tree node type)
    int numItems;                           // For arrays, structures and interfaces
    bool isExprList;                        // For structures that represent expression lists
    bool isVariadicParamList;               // For dynamic arrays of interfaces that represent variadic parameter lists
    struct tagIdent *typeIdent;             // For types that have identifiers
    union
    {
        Field *field[MAX_FIELDS];           // For structures and interfaces
        Signature sig;                      // For functions, including methods
    };
    struct tagType *next;
} Type;


typedef struct tagVisitedTypePair
{
    Type *left, *right;
    struct tagVisitedTypePair *next;
} VisitedTypePair;


typedef struct
{
    Type *first, *last;
    bool forwardTypesEnabled;
    Error *error;
} Types;


void typeInit(Types *types, Error *error);
void typeFree(Types *types, int startBlock /* < 0 to free in all blocks*/);

Type *typeAdd       (Types *types, Blocks *blocks, TypeKind kind);
void typeDeepCopy   (Type *dest, Type *src);
Type *typeAddPtrTo  (Types *types, Blocks *blocks, Type *type);

int typeSizeNoCheck (Type *type);
int typeSize        (Types *types, Type *type);

int typeAlignmentNoCheck(Type *type);
int typeAlignment       (Types *types, Type *type);


static inline bool typeKindInteger(TypeKind typeKind)
{
    return typeKind == TYPE_INT8  || typeKind == TYPE_INT16  || typeKind == TYPE_INT32  || typeKind == TYPE_INT ||
           typeKind == TYPE_UINT8 || typeKind == TYPE_UINT16 || typeKind == TYPE_UINT32 || typeKind == TYPE_UINT;
}


static inline bool typeInteger(Type *type)
{
    return typeKindInteger(type->kind);
}


static inline bool typeKindOrdinal(TypeKind typeKind)
{
    return typeKindInteger(typeKind) || typeKind == TYPE_CHAR || typeKind == TYPE_BOOL;
}


static inline bool typeOrdinal(Type *type)
{
    return typeKindOrdinal(type->kind);
}


static inline bool typeKindReal(TypeKind typeKind)
{
    return typeKind == TYPE_REAL32 || typeKind == TYPE_REAL;
}


static inline bool typeReal(Type *type)
{
    return typeKindReal(type->kind);
}


static inline bool typeStructured(Type *type)
{
    return type->kind == TYPE_ARRAY  || type->kind == TYPE_DYNARRAY  || type->kind == TYPE_MAP ||
           type->kind == TYPE_STRUCT || type->kind == TYPE_INTERFACE || type->kind == TYPE_CLOSURE;
}


static inline bool typeKindGarbageCollected(TypeKind typeKind)
{
    return typeKind == TYPE_PTR    || typeKind == TYPE_WEAKPTR   ||
           typeKind == TYPE_STR    || typeKind == TYPE_ARRAY     || typeKind == TYPE_DYNARRAY || typeKind == TYPE_MAP ||
           typeKind == TYPE_STRUCT || typeKind == TYPE_INTERFACE || typeKind == TYPE_CLOSURE  || typeKind == TYPE_FIBER;
}


bool typeGarbageCollected(Type *type);


static inline bool typeExprListStruct(Type *type)
{
    return type->kind == TYPE_STRUCT && type->isExprList && type->numItems > 0;
}


static inline bool typeFiberFunc(Type *type)
{
    return type->kind                            == TYPE_FN         &&
           type->sig.numParams                   == 3               &&
           type->sig.numDefaultParams            == 0               &&
           type->sig.param[0]->type->kind        == TYPE_INTERFACE  &&
           type->sig.param[1]->type->kind        == TYPE_FIBER      &&
           type->sig.param[2]->type->kind        == TYPE_PTR        &&
           type->sig.param[2]->type->base->kind  != TYPE_VOID       &&
           type->sig.resultType->kind            == TYPE_VOID       &&
           !type->sig.method;
}


bool typeEquivalent             (Type *left, Type *right);
bool typeEquivalentExceptIdent  (Type *left, Type *right);
void typeAssertEquivalent       (Types *types, Type *left, Type *right);
bool typeCompatible             (Type *left, Type *right, bool symmetric);
void typeAssertCompatible       (Types *types, Type *left, Type *right, bool symmetric);
void typeAssertCompatibleParam  (Types *types, Type *left, Type *right, Type *fnType, int paramIndex);
void typeAssertCompatibleBuiltin(Types *types, Type *type, /*BuiltinFunc*/ int builtin, bool condition);


static inline bool typeCompatibleRcv(Type *left, Type *right)
{
    return left->kind  == TYPE_PTR && right->kind == TYPE_PTR && left->base->typeIdent == right->base->typeIdent;
}


static inline bool typeCastablePtrs(Types *types, Type *left, Type *right)
{
    return left->kind  == TYPE_PTR && right->kind == TYPE_PTR &&
           (left->base->kind == TYPE_VOID ||
           (typeSize(types, left->base) <= typeSize(types, right->base) &&
           !typeGarbageCollected(left->base) && !typeGarbageCollected(right->base)));
}


bool typeValidOperator      (Type *type, TokenKind op);
void typeAssertValidOperator(Types *types, Type *type, TokenKind op);

void typeEnableForward(Types *types, bool enable);


static inline bool typeOverflow(TypeKind typeKind, Const val)
{
    switch (typeKind)
    {
        case TYPE_VOID:     return true;
        case TYPE_INT8:     return val.intVal  < -128            || val.intVal  > 127;
        case TYPE_INT16:    return val.intVal  < -32768          || val.intVal  > 32767;
        case TYPE_INT32:    return val.intVal  < -2147483647 - 1 || val.intVal  > 2147483647;
        case TYPE_INT:      return false;
        case TYPE_UINT8:    return val.intVal  < 0               || val.intVal  > 255;
        case TYPE_UINT16:   return val.intVal  < 0               || val.intVal  > 65535;
        case TYPE_UINT32:   return val.intVal  < 0               || val.intVal  > 4294967295;
        case TYPE_UINT:     return false;
        case TYPE_BOOL:     return false;
        case TYPE_CHAR:     return val.intVal  < 0               || val.intVal  > 255;
        case TYPE_REAL32:   return val.realVal < -FLT_MAX        || val.realVal > FLT_MAX;
        case TYPE_REAL:     return val.realVal < -DBL_MAX        || val.realVal > DBL_MAX;
        case TYPE_PTR:
        case TYPE_WEAKPTR:
        case TYPE_STR:
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_MAP:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:
        case TYPE_FIBER:
        case TYPE_FN:       return false;
        default:            return true;
    }
}


Field *typeFindField        (Type *structType, const char *name);
Field *typeAssertFindField  (Types *types, Type *structType, const char *name);
Field *typeAddField         (Types *types, Type *structType, Type *fieldType, const char *fieldName);

Param *typeFindParam        (Signature *sig, const char *name);
Param *typeAddParam         (Types *types, Signature *sig, Type *type, const char *name);

int typeParamSizeUpTo   (Types *types, Signature *sig, int index);
int typeParamSizeTotal  (Types *types, Signature *sig);

const char *typeKindSpelling(TypeKind kind);
char *typeSpelling          (Type *type, char *buf);


static inline Type *typeMapKey(Type *mapType)
{
    return mapType->base->field[MAP_NODE_FIELD_KEY]->type->base;
}


static inline Type *typeMapItem(Type *mapType)
{
    return mapType->base->field[MAP_NODE_FIELD_DATA]->type->base;
}


static inline Type *typeMapNodePtr(Type *mapType)
{
    return mapType->base->field[MAP_NODE_FIELD_LEFT]->type;
}


#endif // UMKA_TYPES_H_INCLUDED
