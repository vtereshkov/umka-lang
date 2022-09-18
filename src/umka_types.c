#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "umka_vm.h"
#include "umka_types.h"
#include "umka_ident.h"


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
    "fiber",
    "fn"
};


void typeInit(Types *types, Error *error)
{
    types->first = types->last = NULL;
    types->error = error;
}


void typeFreeFieldsAndParams(Type *type)
{
    if (type->kind == TYPE_STRUCT || type->kind == TYPE_INTERFACE)
        for (int i = 0; i < type->numItems; i++)
            free(type->field[i]);

    else if (type->kind == TYPE_FN)
        for (int i = 0; i < type->sig.numParams; i++)
            free(type->sig.param[i]);
}


void typeFree(Types *types, int startBlock)
{
    Type *type = types->first;

    // If block is specified, fast forward to the first type in this block (assuming this is the last block in the list)
    if (startBlock >= 0)
    {
        while (type && type->next && type->next->block != startBlock)
            type = type->next;

        Type *next = type->next;
        types->last = type;
        types->last->next = NULL;
        type = next;
    }

    while (type)
    {
        Type *next = type->next;
        typeFreeFieldsAndParams(type);
        free(type);
        type = next;
    }
}


Type *typeAdd(Types *types, Blocks *blocks, TypeKind kind)
{
    Type *type = malloc(sizeof(Type));

    type->kind                  = kind;
    type->block                 = blocks->item[blocks->top].block;
    type->base                  = NULL;
    type->numItems              = 0;
    type->isExprList            = false;
    type->isVariadicParamList   = false;
    type->typeIdent             = NULL;
    type->next                  = NULL;

    if (kind == TYPE_FN)
    {
        type->sig.method            = false;
        type->sig.offsetFromSelf    = 0;
        type->sig.numParams         = 0;
        type->sig.numDefaultParams  = 0;
    }

    // Add to list
    if (!types->first)
        types->first = types->last = type;
    else
    {
        types->last->next = type;
        types->last = type;
    }
    return types->last;
}


void typeDeepCopy(Type *dest, Type *src)
{
    typeFreeFieldsAndParams(dest);

    Type *next = dest->next;
    *dest = *src;
    dest->next = next;

    if (dest->kind == TYPE_STRUCT || dest->kind == TYPE_INTERFACE)
        for (int i = 0; i < dest->numItems; i++)
        {
            dest->field[i] = malloc(sizeof(Field));
            *(dest->field[i]) = *(src->field[i]);
        }

    else if (dest->kind == TYPE_FN)
        for (int i = 0; i < dest->sig.numParams; i++)
        {
            dest->sig.param[i] = malloc(sizeof(Param));
            *(dest->sig.param[i]) = *(src->sig.param[i]);
        }
}


Type *typeAddPtrTo(Types *types, Blocks *blocks, Type *type)
{
    typeAdd(types, blocks, TYPE_PTR);
    types->last->base = type;
    return types->last;
}


int typeSizeNoCheck(Type *type)
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
        case TYPE_ARRAY:    return type->numItems * typeSizeNoCheck(type->base);
        case TYPE_DYNARRAY: return sizeof(DynArray);
        case TYPE_MAP:      return sizeof(Map);
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        {
            int size = 0;
            for (int i = 0; i < type->numItems; i++)
            {
                const int fieldSize = typeSizeNoCheck(type->field[i]->type);
                size = align(size + fieldSize, typeAlignmentNoCheck(type->field[i]->type));
            }
            size = align(size, typeAlignmentNoCheck(type));
            return size;
        }
        case TYPE_FIBER:    return sizeof(void *);
        case TYPE_FN:       return sizeof(int64_t);
        default:            return -1;
    }
}


int typeSize(Types *types, Type *type)
{
    int size = typeSizeNoCheck(type);
    if (size < 0)
    {
        char buf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Illegal type %s", typeSpelling(type, buf));
    }
    return size;
}


int typeAlignmentNoCheck(Type *type)
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
        case TYPE_STR:      return typeSizeNoCheck(type);
        case TYPE_ARRAY:    return typeAlignmentNoCheck(type->base);
        case TYPE_DYNARRAY:
        case TYPE_MAP:      return sizeof(int64_t);
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        {
            int alignment = 1;
            for (int i = 0; i < type->numItems; i++)
            {
                const int fieldAlignment = typeAlignmentNoCheck(type->field[i]->type);
                if (fieldAlignment > alignment)
                    alignment = fieldAlignment;
            }
            return alignment;
        }
        case TYPE_FIBER:    return typeSizeNoCheck(type);
        case TYPE_FN:       return sizeof(int64_t);
        default:            return 0;
    }
}


int typeAlignment(Types *types, Type *type)
{
    int alignment = typeAlignmentNoCheck(type);
    if (alignment <= 0)
    {
        char buf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Illegal type %s", typeSpelling(type, buf));
    }
    return alignment;
}


bool typeGarbageCollected(Type *type)
{
    if (type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_STR || type->kind == TYPE_MAP ||
        type->kind == TYPE_DYNARRAY || type->kind == TYPE_INTERFACE || type->kind == TYPE_FIBER)
        return true;

    if (type->kind == TYPE_ARRAY)
        return typeGarbageCollected(type->base);

    if (type->kind == TYPE_STRUCT)
        for (int i = 0; i < type->numItems; i++)
            if (typeGarbageCollected(type->field[i]->type))
                return true;

    return false;
}


static bool typeDefaultParamEqual(Const *left, Const *right, Type *type)
{
    if (typeOrdinal(type) || type->kind == TYPE_FN)
        return left->intVal == right->intVal;

    if (typeReal(type))
        return left->realVal == right->realVal;

    if (type->kind == TYPE_WEAKPTR)
        return left->weakPtrVal == right->weakPtrVal;

    if (!left->ptrVal || !right->ptrVal)
        return left->ptrVal == right->ptrVal;

    if (type->kind == TYPE_PTR)
        return left->ptrVal == right->ptrVal;

    if (type->kind == TYPE_STR)
        return strcmp((char *)left->ptrVal, (char *)right->ptrVal) == 0;

    if (type->kind == TYPE_ARRAY || type->kind == TYPE_STRUCT)
        return memcmp(left->ptrVal, right->ptrVal, typeSizeNoCheck(type)) == 0;

    return false;
}


static bool typeEquivalentRecursive(Type *left, Type *right, bool checkTypeIdents, VisitedTypePair *firstPair)
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
    if (checkTypeIdents && left->typeIdent && right->typeIdent)
        return left->typeIdent == right->typeIdent && left->block == right->block;

    if (left->kind == right->kind)
    {
        // Pointers or weak pointers
        if (left->kind == TYPE_PTR || left->kind == TYPE_WEAKPTR)
            return typeEquivalentRecursive(left->base, right->base, checkTypeIdents, &newPair);

        // Arrays
        else if (left->kind == TYPE_ARRAY)
        {
            // Number of elements
            if (left->numItems != right->numItems)
                return false;

            return typeEquivalentRecursive(left->base, right->base, checkTypeIdents, &newPair);
        }

        // Dynamic arrays
        else if (left->kind == TYPE_DYNARRAY)
            return typeEquivalentRecursive(left->base, right->base, checkTypeIdents, &newPair);

        // Strings
        else if (left->kind == TYPE_STR)
            return true;

        // Maps
        else if (left->kind == TYPE_MAP)
        {
            // Key type
            if (!typeEquivalentRecursive(typeMapKey(left), typeMapKey(right), checkTypeIdents, &newPair))
                return false;

            return typeEquivalentRecursive(left->base, right->base, checkTypeIdents, &newPair);
        }

        // Structures or interfaces
        else if (left->kind == TYPE_STRUCT || left->kind == TYPE_INTERFACE)
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
                if (!typeEquivalentRecursive(left->field[i]->type, right->field[i]->type, checkTypeIdents, &newPair))
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
            if (left->sig.method != right->sig.method)
                return false;

            // Parameters (skip interface method receiver)
            int iStart = left->sig.offsetFromSelf == 0 ? 0 : 1;
            for (int i = iStart; i < left->sig.numParams; i++)
            {
                // Type
                if (!typeEquivalentRecursive(left->sig.param[i]->type, right->sig.param[i]->type, checkTypeIdents, &newPair))
                    return false;

                // Default value
                if (i >= left->sig.numParams - left->sig.numDefaultParams)
                    if (!typeDefaultParamEqual(&left->sig.param[i]->defaultVal, &right->sig.param[i]->defaultVal, left->sig.param[i]->type))
                        return false;
            }

            // Result type
            if (!typeEquivalentRecursive(left->sig.resultType, right->sig.resultType, checkTypeIdents, &newPair))
                return false;

            return true;
        }

        // Primitive types
        else
            return true;
    }
    return false;
}


bool typeEquivalent(Type *left, Type *right)
{
    return typeEquivalentRecursive(left, right, true, NULL);
}


bool typeEquivalentExceptIdent(Type *left, Type *right)
{
    return typeEquivalentRecursive(left, right, false, NULL);
}


void typeAssertEquivalent(Types *types, Type *left, Type *right)
{
    if (!typeEquivalent(left, right))
    {
        char leftBuf[DEFAULT_STR_LEN + 1], rightBuf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Incompatible types %s and %s", typeSpelling(left, leftBuf), typeSpelling(right, rightBuf));
    }
}


bool typeCompatible(Type *left, Type *right, bool symmetric)
{
    if (typeEquivalent(left, right))
        return true;

    // Integers
    if (typeInteger(left) && typeInteger(right))
        return true;

    // Reals
    if (typeReal(left) && typeReal(right))
        return true;

    // Pointers
    if (left->kind == TYPE_PTR && right->kind == TYPE_PTR)
    {
        // Any pointer can be assigned to an untyped pointer
        if (left->base->kind == TYPE_VOID)
            return true;

        // Any pointer can be compared to an untyped pointer
        if (right->base->kind == TYPE_VOID && symmetric)
            return true;

        // Null can be assigned to any pointer
        if (right->base->kind == TYPE_NULL)
            return true;

        // Null can be compared to any pointer
        if (left->base->kind == TYPE_NULL && symmetric)
            return true;
    }
    return false;
}


void typeAssertCompatible(Types *types, Type *left, Type *right, bool symmetric)
{
    if (!typeCompatible(left, right, symmetric))
    {
        char leftBuf[DEFAULT_STR_LEN + 1], rightBuf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Incompatible types %s and %s", typeSpelling(left, leftBuf), typeSpelling(right, rightBuf));
    }
}


void typeAssertCompatibleBuiltin(Types *types, Type *type, /*BuiltinFunc*/ int builtin, bool condition)
{
    if (!condition)
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Incompatible type %s in %s", typeSpelling(type, typeBuf), vmBuiltinSpelling(builtin));
    }
}


bool typeValidOperator(Type *type, TokenKind op)
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
        case TOK_PLUSEQ:
        case TOK_MINUSEQ:
        case TOK_MULEQ:
        case TOK_DIVEQ:     return typeInteger(type) || typeReal(type);
        case TOK_MODEQ:
        case TOK_ANDEQ:
        case TOK_OREQ:
        case TOK_XOREQ:
        case TOK_SHLEQ:
        case TOK_SHREQ:     return typeInteger(type);
        case TOK_ANDAND:
        case TOK_OROR:      return type->kind == TYPE_BOOL;
        case TOK_PLUSPLUS:
        case TOK_MINUSMINUS:return typeInteger(type);
        case TOK_EQEQ:      return typeOrdinal(type) || typeReal(type) || type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_STR;
        case TOK_LESS:
        case TOK_GREATER:   return typeOrdinal(type) || typeReal(type) || type->kind == TYPE_STR;
        case TOK_EQ:        return true;
        case TOK_NOT:       return type->kind == TYPE_BOOL;
        case TOK_NOTEQ:     return typeOrdinal(type) || typeReal(type) || type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_STR;
        case TOK_LESSEQ:
        case TOK_GREATEREQ: return typeOrdinal(type) || typeReal(type) || type->kind == TYPE_STR;
        default:            return false;
    }
}


void typeAssertValidOperator(Types *types, Type *type, TokenKind op)
{
    if (!typeValidOperator(type, op))
    {
        char buf[DEFAULT_STR_LEN + 1];
        types->error->handler(types->error->context, "Operator %s is not applicable to %s", lexSpelling(op), typeSpelling(type, buf));
    }
}


void typeAssertForwardResolved(Types *types)
{
    for (Type *type = types->first; type; type = type->next)
        if (type->kind == TYPE_FORWARD)
            types->error->handler(types->error->context, "Unresolved forward declaration of %s", (Ident *)(type->typeIdent)->name);
}


Field *typeFindField(Type *structType, const char *name)
{
    if (structType->kind == TYPE_STRUCT || structType->kind == TYPE_INTERFACE)
    {
        unsigned int nameHash = hash(name);
        for (int i = 0; i < structType->numItems; i++)
            if (structType->field[i]->hash == nameHash && strcmp(structType->field[i]->name, name) == 0)
                return structType->field[i];
    }
    return NULL;
}


Field *typeAssertFindField(Types *types, Type *structType, const char *name)
{
    Field *res = typeFindField(structType, name);
    if (!res)
        types->error->handler(types->error->context, "Unknown field %s", name);
    return res;
}


Field *typeAddField(Types *types, Type *structType, Type *fieldType, const char *fieldName)
{
    IdentName fieldNameBuf;
    const char *name;

    if (fieldName)
        name = fieldName;
    else
    {
        // Automatic field naming
        snprintf(fieldNameBuf, DEFAULT_STR_LEN + 1, "__field%d", structType->numItems);
        name = fieldNameBuf;
    }

    Field *field = typeFindField(structType, name);
    if (field)
        types->error->handler(types->error->context, "Duplicate field %s", name);

    if (fieldType->kind == TYPE_FORWARD)
        types->error->handler(types->error->context, "Unresolved forward type declaration for field %s", name);

    if (fieldType->kind == TYPE_VOID)
        types->error->handler(types->error->context, "Void field %s is not allowed", name);

    if (structType->numItems > MAX_FIELDS)
        types->error->handler(types->error->context, "Too many fields");

    int minNextFieldOffset = 0;
    if (structType->numItems > 0)
    {
        Field *lastField = structType->field[structType->numItems - 1];
        minNextFieldOffset = lastField->offset + typeSize(types, lastField->type);
    }

    if (typeSize(types, fieldType) > INT_MAX - minNextFieldOffset)
        types->error->handler(types->error->context, "Structure is too large");

    field = malloc(sizeof(Field));

    strncpy(field->name, name, MAX_IDENT_LEN);
    field->name[MAX_IDENT_LEN] = 0;

    field->hash = hash(name);
    field->type = fieldType;
    field->offset = align(minNextFieldOffset, typeAlignment(types, fieldType));

    structType->field[structType->numItems++] = field;
    return field;
}


Param *typeFindParam(Signature *sig, const char *name)
{
    unsigned int nameHash = hash(name);
    for (int i = 0; i < sig->numParams; i++)
        if (sig->param[i]->hash == nameHash && strcmp(sig->param[i]->name, name) == 0)
            return sig->param[i];

    return NULL;
}


Param *typeAddParam(Types *types, Signature *sig, Type *type, const char *name)
{
    Param *param = typeFindParam(sig, name);
    if (param)
        types->error->handler(types->error->context, "Duplicate parameter %s", name);

    if (sig->numParams > MAX_PARAMS)
        types->error->handler(types->error->context, "Too many parameters");

    param = malloc(sizeof(Param));

    strncpy(param->name, name, MAX_IDENT_LEN);
    param->name[MAX_IDENT_LEN] = 0;

    param->hash = hash(name);
    param->type = type;
    param->defaultVal.intVal = 0;

    sig->param[sig->numParams++] = param;
    return param;
}


int typeParamSizeUpTo(Types *types, Signature *sig, int index)
{
    // All parameters are slot-aligned
    int size = 0;
    for (int i = 0; i <= index; i++)
        size += align(typeSize(types, sig->param[i]->type), sizeof(Slot));
    return size;
}


int typeParamSizeTotal(Types *types, Signature *sig)
{
    return typeParamSizeUpTo(types, sig, sig->numParams - 1);
}


const char *typeKindSpelling(TypeKind kind)
{
    return spelling[kind];
}


static char *typeSpellingRecursive(Type *type, char *buf, int size, int depth)
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
        else
        {
            snprintf(buf + len, nonneg(size - len), "%s", spelling[type->kind]);
        }

        if (type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY || type->kind == TYPE_MAP)
        {
            Type *itemType = (type->kind == TYPE_MAP) ? typeMapItem(type) : type->base;

            char itemBuf[DEFAULT_STR_LEN + 1];
            if (depth > 0)
                strncat(buf, typeSpellingRecursive(itemType, itemBuf, DEFAULT_STR_LEN + 1, depth - 1), nonneg(size - len - 1));
            else
                strncat(buf, "...", nonneg(size - len - 1));
        }
    }
    return buf;
}


char *typeSpelling(Type *type, char *buf)
{
    enum {MAX_TYPE_SPELLING_DEPTH = 10};
    return typeSpellingRecursive(type, buf, DEFAULT_STR_LEN + 1, MAX_TYPE_SPELLING_DEPTH);
}

