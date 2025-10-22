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


typedef enum
{
    // I/O
    BUILTIN_PRINTF,
    BUILTIN_FPRINTF,
    BUILTIN_SPRINTF,
    BUILTIN_SCANF,
    BUILTIN_FSCANF,
    BUILTIN_SSCANF,

    // Math
    BUILTIN_REAL,           // Integer to real at stack top (right operand)
    BUILTIN_REAL_LHS,       // Integer to real at stack top + 1 (left operand) - implicit calls only
    BUILTIN_ROUND,
    BUILTIN_TRUNC,
    BUILTIN_CEIL,
    BUILTIN_FLOOR,
    BUILTIN_ABS,
    BUILTIN_FABS,
    BUILTIN_SQRT,
    BUILTIN_SIN,
    BUILTIN_COS,
    BUILTIN_ATAN,
    BUILTIN_ATAN2,
    BUILTIN_EXP,
    BUILTIN_LOG,

    // Memory
    BUILTIN_NEW,
    BUILTIN_MAKE,
    BUILTIN_MAKEFROMARR,    // Array to dynamic array - implicit calls only
    BUILTIN_MAKEFROMSTR,    // String to dynamic array - implicit calls only
    BUILTIN_MAKETOARR,      // Dynamic array to array - implicit calls only
    BUILTIN_MAKETOSTR,      // Character or dynamic array to string - implicit calls only
    BUILTIN_COPY,
    BUILTIN_APPEND,
    BUILTIN_INSERT,
    BUILTIN_DELETE,
    BUILTIN_SLICE,
    BUILTIN_SORT,
    BUILTIN_SORTFAST,
    BUILTIN_LEN,
    BUILTIN_CAP,
    BUILTIN_SIZEOF,
    BUILTIN_SIZEOFSELF,
    BUILTIN_SELFPTR,
    BUILTIN_SELFHASPTR,
    BUILTIN_SELFTYPEEQ,
    BUILTIN_TYPEPTR,
    BUILTIN_VALID,

    // Maps
    BUILTIN_VALIDKEY,
    BUILTIN_KEYS,

    // Fibers
    BUILTIN_RESUME,

    // Misc
    BUILTIN_MEMUSAGE,
    BUILTIN_EXIT
} BuiltinFunc;


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
    const struct tagType *type;
    int offset;
} Field;


typedef struct
{
    IdentName name;
    unsigned int hash;
    Const val;
} EnumConst;


typedef struct
{
    IdentName name;
    unsigned int hash;
    const struct tagType *type;
    Const defaultVal;
} Param;


typedef struct
{
    int numParams, numDefaultParams;
    bool isMethod;
    int offsetFromSelf;                     // For interface methods
    const Param *param[MAX_PARAMS];
    const struct tagType *resultType;
} Signature;


typedef struct tagType
{
    TypeKind kind;
    int block;
    const struct tagType *base;                 // For pointers, arrays, maps and fibers (for maps, denotes the tree node type; for fibers, denotes the fiber closure type)
    int numItems;                               // For arrays, structures and interfaces
    bool isExprList;                            // For structures that represent expression lists
    bool isVariadicParamList;                   // For dynamic arrays of interfaces that represent variadic parameter lists
    bool isEnum;                                // For enumerations
    const struct tagIdent *typeIdent;           // For types that have identifiers
    const struct tagType *sameAs;               // For types declared as type T = ...
    union
    {
        const Field **field;                    // For structures, interfaces and closures
        const EnumConst **enumConst;            // For enumerations
        Signature sig;                          // For functions, including methods
    };
    int size;
    int alignment;
    const struct tagType *next;
} Type;


typedef struct tagVisitedTypePair
{
    const Type *left, *right;
    struct tagVisitedTypePair *next;
} VisitedTypePair;


typedef struct
{
    const Type *first;
    bool forwardTypesEnabled;
    Storage *storage;
    Error *error;
} Types;


typedef enum
{
    FORMAT_SIZE_SHORT_SHORT,
    FORMAT_SIZE_SHORT,
    FORMAT_SIZE_NORMAL,
    FORMAT_SIZE_LONG,
    FORMAT_SIZE_LONG_LONG
} FormatStringTypeSize;


void typeInit(Types *types, Storage *storage, Error *error);

Type *typeAdd       (Types *types, const Blocks *blocks, TypeKind kind);
void typeDeepCopy   (Storage *storage, Type *dest, const Type *src);

const Type *typeAddPtrTo    (Types *types, const Blocks *blocks, const Type *type);
const Type *typeAddWeakPtrTo(Types *types, const Blocks *blocks, const Type *type);

int typeSize     (const Types *types, const Type *type);
int typeAlignment(const Types *types, const Type *type);


static inline bool typeKindIntegerOrEnum(TypeKind typeKind)
{
    return typeKind == TYPE_INT8  || typeKind == TYPE_INT16  || typeKind == TYPE_INT32  || typeKind == TYPE_INT ||
           typeKind == TYPE_UINT8 || typeKind == TYPE_UINT16 || typeKind == TYPE_UINT32 || typeKind == TYPE_UINT;
}


static inline bool typeInteger(const Type *type)
{
    return typeKindIntegerOrEnum(type->kind) && !type->isEnum;
}


static inline bool typeEnum(const Type *type)
{
    return typeKindIntegerOrEnum(type->kind) && type->isEnum;
}


static inline bool typeKindOrdinal(TypeKind typeKind)
{
    return typeKindIntegerOrEnum(typeKind) || typeKind == TYPE_CHAR || typeKind == TYPE_BOOL;
}


static inline bool typeOrdinal(const Type *type)
{
    return typeKindOrdinal(type->kind);
}


static inline bool typeKindReal(TypeKind typeKind)
{
    return typeKind == TYPE_REAL32 || typeKind == TYPE_REAL;
}


static inline bool typeReal(const Type *type)
{
    return typeKindReal(type->kind);
}


static inline bool typeNarrow(const Type *type)
{
    // Types that occupy less than 64 bits but are still represented by 64-bit temporaries
    return type->kind == TYPE_INT8  || type->kind == TYPE_INT16  || type->kind == TYPE_INT32  ||
           type->kind == TYPE_UINT8 || type->kind == TYPE_UINT16 || type->kind == TYPE_UINT32 ||
           type->kind == TYPE_CHAR  || type->kind == TYPE_BOOL   ||
           type->kind == TYPE_REAL32;
}


static inline bool typeKindSigned(TypeKind typeKind)
{
    return typeKind == TYPE_INT8 || typeKind == TYPE_INT16 || typeKind == TYPE_INT32 || typeKind == TYPE_INT;
}


static inline bool typeStructured(const Type *type)
{
    return type->kind == TYPE_ARRAY  || type->kind == TYPE_DYNARRAY  || type->kind == TYPE_MAP ||
           type->kind == TYPE_STRUCT || type->kind == TYPE_INTERFACE || type->kind == TYPE_CLOSURE;
}


static inline bool typeKindGarbageCollected(TypeKind typeKind)
{
    return typeKind == TYPE_PTR    ||
           typeKind == TYPE_STR    || typeKind == TYPE_ARRAY     || typeKind == TYPE_DYNARRAY || typeKind == TYPE_MAP ||
           typeKind == TYPE_STRUCT || typeKind == TYPE_INTERFACE || typeKind == TYPE_CLOSURE  || typeKind == TYPE_FIBER;
}


bool typeHasPtr(const Type *type, bool alsoWeakPtr);


static inline bool typeGarbageCollected(const Type *type)
{
    return typeHasPtr(type, false);
}


static inline bool typeExprListStruct(const Type *type)
{
    return type->kind == TYPE_STRUCT && type->isExprList && type->numItems > 0;
}


bool typeComparable                 (const Type *type);
bool typeEquivalent                 (const Type *left, const Type *right);
bool typeSameExceptMaybeIdent       (const Type *left, const Type *right);
bool typeCompatible                 (const Type *left, const Type *right);
void typeAssertCompatible           (const Types *types, const Type *left, const Type *right);
void typeAssertCompatibleParam      (const Types *types, const Type *left, const Type *right, const Type *fnType, int paramIndex);
void typeAssertCompatibleBuiltin    (const Types *types, const Type *type, BuiltinFunc builtin, bool compatible);
void typeAssertCompatibleIOBuiltin  (const Types *types, TypeKind expectedTypeKind, const Type *type, BuiltinFunc builtin, bool allowVoid);


static inline bool typeCompatiblePrintf(TypeKind expectedTypeKind, TypeKind typeKind, bool allowVoid)
{
    if (typeKind == TYPE_VOID && !allowVoid)    
        return false;

    // Skip detailed checks if the expected type is not known at compile time
    if (expectedTypeKind == TYPE_NONE)
        return true;

    return  (typeKind == expectedTypeKind) ||
            (typeKindIntegerOrEnum(typeKind) && typeKindIntegerOrEnum(expectedTypeKind)) ||
            (typeKindReal(typeKind) && typeKindReal(expectedTypeKind)) ||
            (typeKind != TYPE_VOID && expectedTypeKind == TYPE_INTERFACE); 
}


static inline bool typeCompatibleScanf(TypeKind expectedBaseTypeKind, TypeKind baseTypeKind, bool allowVoid)
{
    if (!(typeKindOrdinal(baseTypeKind) || typeKindReal(baseTypeKind) || baseTypeKind == TYPE_STR || (baseTypeKind == TYPE_VOID && allowVoid)))
        return false;

    // Skip detailed checks if the expected type is not known at compile time
    if (expectedBaseTypeKind == TYPE_NONE)
        return true;
    
    return baseTypeKind == expectedBaseTypeKind;
}


static inline bool typeCompatibleRcv(const Type *left, const Type *right)
{
    return left->kind  == TYPE_PTR && right->kind == TYPE_PTR && left->base->typeIdent == right->base->typeIdent;
}


static inline bool typeImplicitlyConvertibleBaseTypes(const Type *left, const Type *right)
{
   return left->kind == TYPE_VOID || right->kind == TYPE_NULL;
}


static inline bool typeExplicitlyConvertibleBaseTypes(const Types *types, const Type *left, const Type *right)
{
    return typeSize(types, left) <= typeSize(types, right) && !typeHasPtr(left, true) && !typeHasPtr(right, true);
}


bool typeValidOperator      (const Type *type, TokenKind op);
void typeAssertValidOperator(const Types *types, const Type *type, TokenKind op);

void typeEnableForward(Types *types, bool enable);


static inline bool typeConvOverflow(TypeKind destTypeKind, TypeKind srcTypeKind, Const val)
{
    const bool fromVeryBigUInt = val.intVal < 0 && srcTypeKind == TYPE_UINT;
    const bool fromNegativeInt = val.intVal < 0 && typeKindSigned(srcTypeKind);

    switch (destTypeKind)
    {
        case TYPE_VOID:     return true;
        case TYPE_INT8:     return val.intVal  < -128            || val.intVal  > 127        || fromVeryBigUInt;
        case TYPE_INT16:    return val.intVal  < -32768          || val.intVal  > 32767      || fromVeryBigUInt;
        case TYPE_INT32:    return val.intVal  < -2147483647 - 1 || val.intVal  > 2147483647 || fromVeryBigUInt;
        case TYPE_INT:      return fromVeryBigUInt;
        case TYPE_UINT8:    return val.intVal  < 0               || val.intVal  > 255;
        case TYPE_UINT16:   return val.intVal  < 0               || val.intVal  > 65535;
        case TYPE_UINT32:   return val.intVal  < 0               || val.intVal  > 4294967295;
        case TYPE_UINT:     return fromNegativeInt;
        case TYPE_BOOL:     return val.intVal  < 0               || val.intVal  > 1;
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


static inline bool typeOverflow(TypeKind typeKind, Const val)
{
    return typeConvOverflow(typeKind, TYPE_VOID, val);
}


static inline void typeResizeArray(Type *type, int numItems)
{
    if (type->kind == TYPE_ARRAY)
    {
        type->numItems = numItems;
        type->size = type->numItems * type->base->size;
        type->alignment = type->base->alignment;
    }
}


static inline Type typeMakeDetachedArray(const Type *base, int numItems)
{
    Type type = {.kind = TYPE_ARRAY, .base = base};
    typeResizeArray(&type, numItems);
    return type;
}


const Field *typeFindField        (const Type *structType, const char *name, int *index);
const Field *typeAssertFindField  (const Types *types, const Type *structType, const char *name, int *index);
const Field *typeAddField         (const Types *types, Type *structType, const Type *fieldType, const char *fieldName);

const EnumConst *typeFindEnumConst        (const Type *enumType, const char *name);
const EnumConst *typeAssertFindEnumConst  (const Types *types, const Type *enumType, const char *name);
const EnumConst *typeFindEnumConstByVal   (const Type *enumType, Const val);
const EnumConst *typeAddEnumConst         (const Types *types, Type *enumType, const char *name, Const val);

const Param *typeFindParam    (const Signature *sig, const char *name);
const Param *typeAddParam     (const Types *types, Signature *sig, const Type *type, const char *name, Const defaultVal);

int typeParamSizeUpTo   (const Types *types, const Signature *sig, int index);
int typeParamSizeTotal  (const Types *types, const Signature *sig);
int typeParamOffset     (const Types *types, const Signature *sig, int index);

const ParamLayout            *typeMakeParamLayout           (const Types *types, const Signature *sig);
const ParamAndLocalVarLayout *typeMakeParamAndLocalVarLayout(const Types *types, const ParamLayout *paramLayout, int localVarSlots);

const char *typeKindSpelling(TypeKind kind);
char *typeSpelling          (const Type *type, char *buf);

bool typeFormatStringValid(const char *format, int *formatLen, int *typeLetterPos, TypeKind *typeKind, FormatStringTypeSize *size);


static inline const Type *typeMapKey(const Type *mapType)
{
    return mapType->base->field[MAP_NODE_FIELD_KEY]->type->base;
}


static inline const Type *typeMapItem(const Type *mapType)
{
    return mapType->base->field[MAP_NODE_FIELD_DATA]->type->base;
}


static inline const Type *typeMapNodePtr(const Type *mapType)
{
    return mapType->base->field[MAP_NODE_FIELD_LEFT]->type;
}


#endif // UMKA_TYPES_H_INCLUDED
