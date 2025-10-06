#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "umka_vm.h"
#include "umka_types.h"
#include "umka_ident.h"
#include "umka_const.h"


static const char *spelling [] =
{
    "none",
    "forward",
    "void",
    "null",
    "int8",
    "int16",
    "int32",
    "int",
    "uint8",
    "uint16",
    "uint32",
    "uint",
    "bool",
    "char",
    "real32",
    "real",
    "^",
    "weak ^",
    "[...]",
    "[]",
    "str",
    "map",
    "struct",
    "interface",
    "fn |..|",
    "fiber",
    "fn"
};


static int typeSizeRecompute(const Type *type);
static int typeAlignmentRecompute(const Type *type);


void typeInit(Types *types, Storage *storage, Error *error)
{
    types->first = NULL;
    types->forwardTypesEnabled = false;
    types->storage = storage;
    types->error = error;
}


Type *typeAdd(Types *types, const Blocks *blocks, TypeKind kind)
{
    Type *type = storageAdd(types->storage, sizeof(Type));

    type->kind  = kind;
    type->block = blocks->item[blocks->top].block;
    type->sameAs = type;
    type->size = typeSizeRecompute(type);
    type->alignment = typeAlignmentRecompute(type);

    type->next = types->first;
    types->first = type;

    return type;
}


void typeDeepCopy(Storage *storage, Type *dest, const Type *src)
{
    const Type *next = dest->next;
    *dest = *src;
    dest->next = next;

    if ((dest->kind == TYPE_STRUCT || dest->kind == TYPE_INTERFACE || dest->kind == TYPE_CLOSURE) && dest->numItems > 0)
    {
        dest->field = storageAdd(storage, dest->numItems * sizeof(Field *));
        for (int i = 0; i < dest->numItems; i++)
        {
            Field *field = storageAdd(storage, sizeof(Field));
            *field = *(src->field[i]);
            dest->field[i] = field;
        }
    }
    else if (typeEnum(dest) && dest->numItems > 0)
    {
        dest->enumConst = storageAdd(storage, dest->numItems * sizeof(EnumConst *));
        for (int i = 0; i < dest->numItems; i++)
        {
            EnumConst *enumConst = storageAdd(storage, sizeof(EnumConst));
            *enumConst = *(src->enumConst[i]);
            dest->enumConst[i] = enumConst;
        }
    }
    else if (dest->kind == TYPE_FN && dest->sig.numParams > 0)
    {
        for (int i = 0; i < dest->sig.numParams; i++)
        {
            Param *param = storageAdd(storage, sizeof(Param));
            *param = *(src->sig.param[i]);
            dest->sig.param[i] = param;
        }
    }
}


const Type *typeAddPtrTo(Types *types, const Blocks *blocks, const Type *type)
{
    Type *ptrType = typeAdd(types, blocks, TYPE_PTR);
    ptrType->base = type;
    return ptrType;
}


const Type *typeAddWeakPtrTo(Types *types, const Blocks *blocks, const Type *type)
{
    Type *weakPtrType = typeAdd(types, blocks, TYPE_WEAKPTR);
    weakPtrType->base = type;
    return weakPtrType;
}


static int typeSizeRecompute(const Type *type)
{
    switch (type->kind)
    {
        case TYPE_VOID:     return 0;
        case TYPE_INT8:     return sizeof(int8_t);
        case TYPE_INT16:    return sizeof(int16_t);
        case TYPE_INT32:    return sizeof(int32_t);
        case TYPE_INT:      return sizeof(int64_t);
        case TYPE_UINT8:    return sizeof(uint8_t);
        case TYPE_UINT16:   return sizeof(uint16_t);
        case TYPE_UINT32:   return sizeof(uint32_t);
        case TYPE_UINT:     return sizeof(uint64_t);
        case TYPE_BOOL:     return sizeof(bool);
        case TYPE_CHAR:     return sizeof(unsigned char);
        case TYPE_REAL32:   return sizeof(float);
        case TYPE_REAL:     return sizeof(double);
        case TYPE_PTR:      return sizeof(void *);
        case TYPE_WEAKPTR:  return sizeof(uint64_t);
        case TYPE_STR:      return sizeof(void *);
        case TYPE_ARRAY:    return type->numItems > 0 ? (type->numItems * typeSizeRecompute(type->base)) : 0;
        case TYPE_DYNARRAY: return sizeof(DynArray);
        case TYPE_MAP:      return sizeof(Map);
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:
        {
            int size = 0;
            for (int i = 0; i < type->numItems; i++)
            {
                const int fieldSize = typeSizeRecompute(type->field[i]->type);
                size = align(size + fieldSize, typeAlignmentRecompute(type->field[i]->type));
            }
            size = align(size, typeAlignmentRecompute(type));
            return size;
        }
        case TYPE_FIBER:    return sizeof(void *);
        case TYPE_FN:       return sizeof(int64_t);
        default:            return -1;
    }
}


int typeSize(const Types *types, const Type *type)
{
    if (type->size < 0)
    {
        char buf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Illegal type %s", typeSpelling(type, buf));
    }
    return type->size;
}


static int typeAlignmentRecompute(const Type *type)
{
    switch (type->kind)
    {
        case TYPE_VOID:     return 1;
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:
        case TYPE_UINT:
        case TYPE_BOOL:
        case TYPE_CHAR:
        case TYPE_REAL32:
        case TYPE_REAL:
        case TYPE_PTR:
        case TYPE_WEAKPTR:
        case TYPE_STR:      return typeSizeRecompute(type);
        case TYPE_ARRAY:    return type->numItems > 0 ? typeAlignmentRecompute(type->base) : 1;
        case TYPE_DYNARRAY:
        case TYPE_MAP:      return sizeof(int64_t);
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:
        {
            int alignment = 1;
            for (int i = 0; i < type->numItems; i++)
            {
                const int fieldAlignment = typeAlignmentRecompute(type->field[i]->type);
                if (fieldAlignment > alignment)
                    alignment = fieldAlignment;
            }
            return alignment;
        }
        case TYPE_FIBER:    return typeSizeRecompute(type);
        case TYPE_FN:       return sizeof(int64_t);
        default:            return 0;
    }
}


int typeAlignment(const Types *types, const Type *type)
{
    if (type->alignment <= 0)
    {
        char buf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Illegal type %s", typeSpelling(type, buf));
    }
    return type->alignment;
}


bool typeHasPtr(const Type *type, bool alsoWeakPtr)
{
    if (type->kind == TYPE_PTR      || type->kind == TYPE_STR       || type->kind == TYPE_MAP     ||
        type->kind == TYPE_DYNARRAY || type->kind == TYPE_INTERFACE || type->kind == TYPE_CLOSURE || type->kind == TYPE_FIBER)
        return true;

    if (type->kind == TYPE_WEAKPTR && alsoWeakPtr)
        return true;

    if (type->kind == TYPE_ARRAY)
        return type->numItems > 0 && typeHasPtr(type->base, alsoWeakPtr);

    if (type->kind == TYPE_STRUCT)
        for (int i = 0; i < type->numItems; i++)
            if (typeHasPtr(type->field[i]->type, alsoWeakPtr))
                return true;

    return false;
}


bool typeComparable(const Type *type)
{
    if (typeOrdinal(type) || typeReal(type) || type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_STR)
        return true;

    if (type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY)
        return typeComparable(type->base);

    if (type->kind == TYPE_STRUCT)
    {
        for (int i = 0; i < type->numItems; i++)
            if (!typeComparable(type->field[i]->type))
                return false;
        return true;
    }

    return false;
}


static bool typeDefaultParamEqual(const Const *left, const Const *right, const Type *type)
{
    if (type->kind == TYPE_INTERFACE)
    {
        const Interface *leftInterface = left->ptrVal;
        const Interface *rightInterface = right->ptrVal;
        return leftInterface && rightInterface && leftInterface->self == rightInterface->self;
    }

    return constCompare(NULL, left, right, type) == 0;
}


static bool typeEquivalentRecursive(const Type *left, const Type *right, VisitedTypePair *firstPair)
{
    // Recursively defined types visited before (need to check first in order to break a possible circular definition)
    VisitedTypePair *pair = firstPair;
    while (pair && !(pair->left == left && pair->right == right))
        pair = pair->next;

    if (pair)
        return true;

    VisitedTypePair newPair = {left, right, firstPair};

    // Same types
    if (left == right)
        return true;

    // Identically named types
    if (left->typeIdent && right->typeIdent)
        return left->typeIdent == right->typeIdent && left->block == right->block;

    if (left->kind == right->kind)
    {
        // Pointers or weak pointers
        if (left->kind == TYPE_PTR || left->kind == TYPE_WEAKPTR)
            return typeEquivalentRecursive(left->base, right->base, &newPair);

        // Arrays
        else if (left->kind == TYPE_ARRAY)
        {
            // Number of elements
            if (left->numItems != right->numItems)
                return false;

            return typeEquivalentRecursive(left->base, right->base, &newPair);
        }

        // Dynamic arrays
        else if (left->kind == TYPE_DYNARRAY)
            return typeEquivalentRecursive(left->base, right->base, &newPair);

        // Strings
        else if (left->kind == TYPE_STR)
            return true;

        // Enumerations
        else if (typeEnum(left) || typeEnum(right))
            return false;

        // Maps
        else if (left->kind == TYPE_MAP)
        {
            // Key type
            if (!typeEquivalentRecursive(typeMapKey(left), typeMapKey(right), &newPair))
                return false;

            return typeEquivalentRecursive(left->base, right->base, &newPair);
        }

        // Structures or interfaces
        else if (left->kind == TYPE_STRUCT || left->kind == TYPE_INTERFACE || left->kind == TYPE_CLOSURE)
        {
            // Number of fields
            if (left->numItems != right->numItems)
                return false;

            // Fields
            for (int i = 0; i < left->numItems; i++)
            {
                // Name
                if (left->field[i]->hash != right->field[i]->hash || strcmp(left->field[i]->name, right->field[i]->name) != 0)
                    return false;

                // Type
                if (!typeEquivalentRecursive(left->field[i]->type, right->field[i]->type, &newPair))
                    return false;
            }
            return true;
        }

        // Functions
        else if (left->kind == TYPE_FN)
        {
            // Number of parameters
            if (left->sig.numParams != right->sig.numParams)
                return false;

            // Number of default parameters
            if (left->sig.numDefaultParams != right->sig.numDefaultParams)
                return false;

            // Method flag
            if (left->sig.isMethod != right->sig.isMethod)
                return false;

            // Parameters (skip interface method receiver)
            const int iStart = left->sig.offsetFromSelf == 0 ? 0 : 1;
            for (int i = iStart; i < left->sig.numParams; i++)
            {
                // Type
                if (!typeEquivalentRecursive(left->sig.param[i]->type, right->sig.param[i]->type, &newPair))
                    return false;

                // Default value
                if (i >= left->sig.numParams - left->sig.numDefaultParams)
                    if (!typeDefaultParamEqual(&left->sig.param[i]->defaultVal, &right->sig.param[i]->defaultVal, left->sig.param[i]->type))
                        return false;
            }

            // Result type
            if (!typeEquivalentRecursive(left->sig.resultType, right->sig.resultType, &newPair))
                return false;

            return true;
        }

        // Primitive types
        else
            return true;
    }
    return false;
}


bool typeEquivalent(const Type *left, const Type *right)
{
    return typeEquivalentRecursive(left, right, NULL);
}


bool typeSameExceptMaybeIdent(const Type *left, const Type *right)
{
    return left->sameAs == right->sameAs;
}


bool typeCompatible(const Type *left, const Type *right)
{
    if (typeEquivalent(left, right))
        return true;

    if (typeInteger(left) && typeInteger(right))
        return true;

    if (typeReal(left) && typeReal(right))
        return true;

    return false;
}


void typeAssertCompatible(const Types *types, const Type *left, const Type *right)
{
    if (!typeCompatible(left, right))
    {
        char leftBuf[DEFAULT_STR_LEN + 1], rightBuf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Incompatible types %s and %s", typeSpelling(left, leftBuf), typeSpelling(right, rightBuf));
    }
}


void typeAssertCompatibleParam(const Types *types, const Type *left, const Type *right, const Type *fnType, int paramIndex)
{
    if (!typeCompatible(left, right))
    {
        char rightBuf[DEFAULT_STR_LEN + 1], fnTypeBuf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Incompatible type %s for parameter %d to %s", typeSpelling(right, rightBuf), paramIndex, typeSpelling(fnType, fnTypeBuf));
    }
}


void typeAssertCompatibleBuiltin(const Types *types, const Type *type, BuiltinFunc builtin, bool compatible)
{
    if (!compatible)
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Incompatible type %s in %s", typeSpelling(type, typeBuf), vmBuiltinSpelling(builtin));
    }
}


void typeAssertCompatibleIOBuiltin(const Types *types, TypeKind expectedTypeKind, const Type *type, BuiltinFunc builtin, bool allowVoid)
{
    bool compatible = false;
    if (builtin == BUILTIN_PRINTF || builtin == BUILTIN_FPRINTF || builtin == BUILTIN_SPRINTF)
        compatible = typeCompatiblePrintf(expectedTypeKind, type->kind, allowVoid);
    else
    {
        if (type->kind != TYPE_PTR)
            types->error->handler(types->error->context, "Pointer expected in %s", vmBuiltinSpelling(builtin));
        type = type->base;
        compatible = typeCompatibleScanf(expectedTypeKind, type->kind, allowVoid);
    }

    if (!compatible)
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        if (expectedTypeKind == TYPE_NONE)
            types->error->handler(types->error->context, "Incompatible type %s in %s", typeSpelling(type, typeBuf), vmBuiltinSpelling(builtin));
        else
            types->error->handler(types->error->context, "Incompatible types %s and %s in %s", typeKindSpelling(expectedTypeKind), typeSpelling(type, typeBuf), vmBuiltinSpelling(builtin));
    }        
}


bool typeValidOperator(const Type *type, TokenKind op)
{
    switch (op)
    {
        case TOK_PLUS:      return typeInteger(type) || typeReal(type) || type->kind == TYPE_STR;
        case TOK_MINUS:
        case TOK_MUL:
        case TOK_DIV:
        case TOK_MOD:       return typeInteger(type) || typeReal(type);
        case TOK_AND:
        case TOK_OR:
        case TOK_XOR:
        case TOK_SHL:
        case TOK_SHR:       return typeInteger(type);
        case TOK_PLUSEQ:    return typeInteger(type) || typeReal(type) || type->kind == TYPE_STR;
        case TOK_MINUSEQ:
        case TOK_MULEQ:
        case TOK_DIVEQ:
        case TOK_MODEQ:     return typeInteger(type) || typeReal(type);
        case TOK_ANDEQ:
        case TOK_OREQ:
        case TOK_XOREQ:
        case TOK_SHLEQ:
        case TOK_SHREQ:     return typeInteger(type);
        case TOK_ANDAND:
        case TOK_OROR:      return type->kind == TYPE_BOOL;
        case TOK_PLUSPLUS:
        case TOK_MINUSMINUS:return typeInteger(type);
        case TOK_EQEQ:
        case TOK_LESS:
        case TOK_GREATER:   return typeComparable(type);
        case TOK_EQ:        return true;
        case TOK_NOT:       return type->kind == TYPE_BOOL;
        case TOK_NOTEQ:
        case TOK_LESSEQ:
        case TOK_GREATEREQ: return typeComparable(type);
        default:            return false;
    }
}


void typeAssertValidOperator(const Types *types, const Type *type, TokenKind op)
{
    if (!typeValidOperator(type, op))
    {
        char buf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Operator %s is not applicable to %s", lexSpelling(op), typeSpelling(type, buf));
    }
}


void typeEnableForward(Types *types, bool enable)
{
    types->forwardTypesEnabled = enable;

    if (!enable)
        for (const Type *type = types->first; type; type = type->next)
            if (type->kind == TYPE_FORWARD)
                types->error->handler(types->error->context, "Unresolved forward declaration of %s", (Ident *)(type->typeIdent)->name);
}


const Field *typeFindField(const Type *structType, const char *name, int *index)
{
    if (structType->kind == TYPE_STRUCT || structType->kind == TYPE_INTERFACE || structType->kind == TYPE_CLOSURE)
    {
        unsigned int nameHash = hash(name);
        for (int i = 0; i < structType->numItems; i++)
            if (structType->field[i]->hash == nameHash && strcmp(structType->field[i]->name, name) == 0)
            {
                if (index)
                    *index = i;
                return structType->field[i];
            }
    }
    return NULL;
}


const Field *typeAssertFindField(const Types *types, const Type *structType, const char *name, int *index)
{
    const Field *res = typeFindField(structType, name, index);
    if (!res)
        types->error->handler(types->error->context, "Unknown field %s", name);
    return res;
}


const Field *typeAddField(const Types *types, Type *structType, const Type *fieldType, const char *fieldName)
{
    IdentName fieldNameBuf;
    const char *name;

    if (fieldName)
        name = fieldName;
    else
    {
        // Automatic field naming
        snprintf(fieldNameBuf, DEFAULT_STR_LEN + 1, "item%d", structType->numItems);
        name = fieldNameBuf;
    }

    if (typeFindField(structType, name, NULL))
        types->error->handler(types->error->context, "Duplicate field %s", name);

    if (fieldType->kind == TYPE_FORWARD)
        types->error->handler(types->error->context, "Unresolved forward type declaration for field %s", name);

    if (fieldType->kind == TYPE_VOID)
        types->error->handler(types->error->context, "Void field %s is not allowed", name);

    int minNextFieldOffset = 0;
    if (structType->numItems > 0)
    {
        const Field *lastField = structType->field[structType->numItems - 1];
        minNextFieldOffset = lastField->offset + lastField->type->size;
    }

    if (typeSize(types, fieldType) > INT_MAX - minNextFieldOffset)
        types->error->handler(types->error->context, "Structure is too large");

    Field *field = storageAdd(types->storage, sizeof(Field));

    strncpy(field->name, name, MAX_IDENT_LEN);
    field->name[MAX_IDENT_LEN] = 0;

    field->hash = hash(name);
    field->type = fieldType;
    field->offset = align(minNextFieldOffset, typeAlignment(types, fieldType));

    if (structType->numItems > 0)
        structType->field = storageRealloc(types->storage, structType->field, (structType->numItems + 1) * sizeof(Field *));
    else
        structType->field = storageAdd(types->storage, sizeof(Field *));

    structType->numItems++;
    structType->field[structType->numItems - 1] = field;

    if (structType->alignment < fieldType->alignment)
        structType->alignment = fieldType->alignment;

    structType->size = align(field->offset + fieldType->size, structType->alignment);

    return field;
}


const EnumConst *typeFindEnumConst(const Type *enumType, const char *name)
{
    if (typeEnum(enumType))
    {
        unsigned int nameHash = hash(name);
        for (int i = 0; i < enumType->numItems; i++)
            if (enumType->enumConst[i]->hash == nameHash && strcmp(enumType->enumConst[i]->name, name) == 0)
                return enumType->enumConst[i];
    }
    return NULL;
}


const EnumConst *typeAssertFindEnumConst(const Types *types, const Type *enumType, const char *name)
{
    const EnumConst *res = typeFindEnumConst(enumType, name);
    if (!res)
        types->error->handler(types->error->context, "Unknown enumeration constant %s", name);
    return res;
}


const EnumConst *typeFindEnumConstByVal(const Type *enumType, Const val)
{
    if (typeEnum(enumType))
    {
        for (int i = 0; i < enumType->numItems; i++)
            if (enumType->enumConst[i]->val.intVal == val.intVal)
                return enumType->enumConst[i];
    }
    return NULL;
}


const EnumConst *typeAddEnumConst(const Types *types, Type *enumType, const char *name, Const val)
{
    if (typeFindEnumConst(enumType, name))
        types->error->handler(types->error->context, "Duplicate enumeration constant %s", name);

    if (typeFindEnumConstByVal(enumType, val))
        types->error->handler(types->error->context, "Duplicate enumeration constant value %lld", val.intVal);

    EnumConst *enumConst = storageAdd(types->storage, sizeof(EnumConst));

    strncpy(enumConst->name, name, MAX_IDENT_LEN);
    enumConst->name[MAX_IDENT_LEN] = 0;

    enumConst->hash = hash(name);
    enumConst->val = val;

    if (enumType->numItems > 0)
        enumType->enumConst = storageRealloc(types->storage, enumType->enumConst, (enumType->numItems + 1) * sizeof(EnumConst *));
    else
        enumType->enumConst = storageAdd(types->storage, sizeof(EnumConst *));

    enumType->numItems++;
    enumType->enumConst[enumType->numItems - 1] = enumConst;

    return enumConst;
}


const Param *typeFindParam(const Signature *sig, const char *name)
{
    const unsigned int nameHash = hash(name);
    for (int i = 0; i < sig->numParams; i++)
        if (sig->param[i]->hash == nameHash && strcmp(sig->param[i]->name, name) == 0)
            return sig->param[i];

    return NULL;
}


const Param *typeAddParam(const Types *types, Signature *sig, const Type *type, const char *name, Const defaultVal)
{
    if (typeFindParam(sig, name))
        types->error->handler(types->error->context, "Duplicate parameter %s", name);

    if (sig->numParams > MAX_PARAMS)
        types->error->handler(types->error->context, "Too many parameters");

    Param *param = storageAdd(types->storage, sizeof(Param));

    strncpy(param->name, name, MAX_IDENT_LEN);
    param->name[MAX_IDENT_LEN] = 0;

    param->hash = hash(name);
    param->type = type;
    param->defaultVal = defaultVal;

    sig->param[sig->numParams++] = param;
    return param;
}


int typeParamSizeUpTo(const Types *types, const Signature *sig, int index)
{
    // All parameters are slot-aligned
    int size = 0;
    for (int i = 0; i <= index; i++)
        size += align(typeSize(types, sig->param[i]->type), sizeof(Slot));
    return size;
}


int typeParamSizeTotal(const Types *types, const Signature *sig)
{
    return typeParamSizeUpTo(types, sig, sig->numParams - 1);
}


int typeParamOffset(const Types *types, const Signature *sig, int index)
{
    const int paramSizeUpToIndex = typeParamSizeUpTo(types, sig, index);
    const int paramSizeTotal     = typeParamSizeTotal(types, sig);
    return (paramSizeTotal - paramSizeUpToIndex) + 2 * sizeof(Slot);  // + 2 slots for old base pointer and return address
}


const ParamLayout *typeMakeParamLayout(const Types *types, const Signature *sig)
{
    ParamLayout *layout = storageAdd(types->storage, sizeof(ParamLayout) + sig->numParams * sizeof(int64_t));

    layout->numParams = sig->numParams;
    layout->numResultParams = typeStructured(sig->resultType) ? 1 : 0;
    layout->numParamSlots = typeParamSizeTotal(types, sig) / sizeof(Slot);

    for (int i = 0; i < sig->numParams; i++)
        layout->firstSlotIndex[i] = typeParamOffset(types, sig, i) / sizeof(Slot) - 2;   // - 2 slots for old base pointer and return address

    return layout;
}


const ParamAndLocalVarLayout *typeMakeParamAndLocalVarLayout(const Types *types, const ParamLayout *paramLayout, int localVarSlots)
{
    ParamAndLocalVarLayout *layout = storageAdd(types->storage, sizeof(ParamAndLocalVarLayout));
    layout->paramLayout = paramLayout;
    layout->localVarSlots = localVarSlots;
    return layout;
}


const char *typeKindSpelling(TypeKind kind)
{
    return spelling[kind];
}


static char *typeSpellingRecursive(const Type *type, char *buf, int size, int depth)
{
    if (type->block == 0 && type->typeIdent)
        snprintf(buf, size, "%s", type->typeIdent->name);
    else
    {
        int len = 0;

        if (type->kind == TYPE_ARRAY)
        {
            len += snprintf(buf + len, nonneg(size - len), "[%d]", type->numItems);
        }
        else if (typeEnum(type))
        {
            len += snprintf(buf + len, nonneg(size - len), "enum(%s)", typeKindSpelling(type->kind));
        }
        else if (type->kind == TYPE_MAP)
        {
            char keyBuf[DEFAULT_STR_LEN + 1];
            len += snprintf(buf + len, nonneg(size - len), "map[%s]", typeSpellingRecursive(typeMapKey(type), keyBuf, DEFAULT_STR_LEN + 1, depth - 1));
        }
        else if (typeExprListStruct(type))
        {
            len += snprintf(buf + len, nonneg(size - len), "{ ");
            for (int i = 0; i < type->numItems; i++)
            {
                char fieldBuf[DEFAULT_STR_LEN + 1];
                len += snprintf(buf + len, nonneg(size - len), "%s ", typeSpellingRecursive(type->field[i]->type, fieldBuf, DEFAULT_STR_LEN + 1, depth - 1));
            }
            len += snprintf(buf + len, nonneg(size - len), "}");
        }
        else if (type->kind == TYPE_FN || type->kind == TYPE_CLOSURE)
        {
            const bool isClosure = type->kind == TYPE_CLOSURE;
            if (isClosure)
                type = type->field[0]->type;

            len += snprintf(buf + len, nonneg(size - len), "fn (");

            if (type->sig.isMethod)
            {
                char paramBuf[DEFAULT_STR_LEN + 1];
                len += snprintf(buf + len, nonneg(size - len), "%s) (", typeSpellingRecursive(type->sig.param[0]->type, paramBuf, DEFAULT_STR_LEN + 1, depth - 1));
            }

            const int numPreHiddenParams = 1;                                                 // #self or #upvalues
            const int numPostHiddenParams = typeStructured(type->sig.resultType) ? 1 : 0;     // #result

            for (int i = numPreHiddenParams; i < type->sig.numParams - numPostHiddenParams; i++)
            {
                if (i > numPreHiddenParams)
                    len += snprintf(buf + len, nonneg(size - len), ", ");

                char paramBuf[DEFAULT_STR_LEN + 1];
                len += snprintf(buf + len, nonneg(size - len), "%s", typeSpellingRecursive(type->sig.param[i]->type, paramBuf, DEFAULT_STR_LEN + 1, depth - 1));
            }

            len += snprintf(buf + len, nonneg(size - len), ")");

            if (type->sig.resultType->kind != TYPE_VOID)
            {
                char resultBuf[DEFAULT_STR_LEN + 1];
                len += snprintf(buf + len, nonneg(size - len), ": %s", typeSpellingRecursive(type->sig.resultType, resultBuf, DEFAULT_STR_LEN + 1, depth - 1));
            }

            if (isClosure)
                len += snprintf(buf + len, nonneg(size - len), " |...|");
        }
        else
        {
            snprintf(buf + len, nonneg(size - len), "%s", spelling[type->kind]);
        }

        if (type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY || type->kind == TYPE_MAP)
        {
            const Type *itemType = (type->kind == TYPE_MAP) ? typeMapItem(type) : type->base;

            char itemBuf[DEFAULT_STR_LEN + 1];
            if (depth > 0)
                strncat(buf, typeSpellingRecursive(itemType, itemBuf, DEFAULT_STR_LEN + 1, depth - 1), nonneg(size - len - 1));
            else
                strncat(buf, "...", nonneg(size - len - 1));
        }
    }
    return buf;
}


char *typeSpelling(const Type *type, char *buf)
{
    enum {MAX_TYPE_SPELLING_DEPTH = 10};
    return typeSpellingRecursive(type, buf, DEFAULT_STR_LEN + 1, MAX_TYPE_SPELLING_DEPTH);
}


bool typeFormatStringValid(const char *format, int *formatLen, int *typeLetterPos, TypeKind *typeKind, FormatStringTypeSize *size)
{
    *size = FORMAT_SIZE_NORMAL;
    *typeKind = TYPE_VOID;
    int i = 0;

    while (format[i])
    {
        *size = FORMAT_SIZE_NORMAL;
        *typeKind = TYPE_VOID;

        while (format[i] && format[i] != '%')
            i++;

        // "%" [flags] [width] ["." precision] [length] type
        // "%"
        if (format[i] == '%')
        {
            i++;

            // [flags]
            while (format[i] == '+' || format[i] == '-'  || format[i] == ' ' ||
                   format[i] == '0' || format[i] == '\'' || format[i] == '#')
                i++;

            // [width]
            while (format[i] >= '0' && format[i] <= '9')
                i++;

            // [.precision]
            if (format[i] == '.')
            {
                i++;
                while (format[i] >= '0' && format[i] <= '9')
                    i++;
            }

            // [length]
            if (format[i] == 'h')
            {
                *size = FORMAT_SIZE_SHORT;
                i++;

                if (format[i] == 'h')
                {
                    *size = FORMAT_SIZE_SHORT_SHORT;
                    i++;
                }
            }
            else if (format[i] == 'l')
            {
                *size = FORMAT_SIZE_LONG;
                i++;

                if (format[i] == 'l')
                {
                    *size = FORMAT_SIZE_LONG_LONG;
                    i++;
                }
            }

            // type
            *typeLetterPos = i;
            switch (format[i])
            {
                case '%': i++; continue;
                case 'd':
                case 'i':
                {
                    switch (*size)
                    {
                        case FORMAT_SIZE_SHORT_SHORT:  *typeKind = TYPE_INT8;      break;
                        case FORMAT_SIZE_SHORT:        *typeKind = TYPE_INT16;     break;
                        case FORMAT_SIZE_NORMAL:
                        case FORMAT_SIZE_LONG:         *typeKind = TYPE_INT32;     break;
                        case FORMAT_SIZE_LONG_LONG:    *typeKind = TYPE_INT;       break;
                    }
                    break;
                }
                case 'u':
                case 'x':
                case 'X':
                {
                    switch (*size)
                    {
                        case FORMAT_SIZE_SHORT_SHORT:  *typeKind = TYPE_UINT8;      break;
                        case FORMAT_SIZE_SHORT:        *typeKind = TYPE_UINT16;     break;
                        case FORMAT_SIZE_NORMAL:
                        case FORMAT_SIZE_LONG:         *typeKind = TYPE_UINT32;     break;
                        case FORMAT_SIZE_LONG_LONG:    *typeKind = TYPE_UINT;       break;
                    }
                    break;
                }
                case 'f':
                case 'F':
                case 'e':
                case 'E':
                case 'g':
                case 'G':
                {
                    switch (*size)
                    {
                        case FORMAT_SIZE_NORMAL:        *typeKind = TYPE_REAL32;    break;
                        case FORMAT_SIZE_LONG:          *typeKind = TYPE_REAL;      break;
                        default:                        return false;
                    }
                    break;
                }
                case 's':
                case 'c':
                {
                    *typeKind = format[i] == 's' ? TYPE_STR : TYPE_CHAR;
                    if (*size != FORMAT_SIZE_NORMAL)
                        return false;
                    break;
                }
                case 'v': *typeKind = TYPE_INTERFACE;  /* Actually any type */      break;

                default: return false;
            }
            i++;
        }
        break;
    }
    *formatLen = i;
    return true;
}
