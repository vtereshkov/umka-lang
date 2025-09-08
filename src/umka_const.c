#define __USE_MINGW_ANSI_STDIO 1

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <inttypes.h>

#include "umka_const.h"


void constInit(Consts *consts, Error *error)
{
    consts->error = error;
}


void constZero(void *lhs, int size)
{
    memset(lhs, 0, size);
}


bool constDeref(const Consts *consts, Const *constant, TypeKind typeKind)
{
    if (!constant->ptrVal)
    {
        if (consts)
            consts->error->handler(consts->error->context, "Pointer is null");
        return false;
    }

    switch (typeKind)
    {
        case TYPE_INT8:         constant->intVal     = *(int8_t         *)constant->ptrVal; break;
        case TYPE_INT16:        constant->intVal     = *(int16_t        *)constant->ptrVal; break;
        case TYPE_INT32:        constant->intVal     = *(int32_t        *)constant->ptrVal; break;
        case TYPE_INT:          constant->intVal     = *(int64_t        *)constant->ptrVal; break;
        case TYPE_UINT8:        constant->intVal     = *(uint8_t        *)constant->ptrVal; break;
        case TYPE_UINT16:       constant->intVal     = *(uint16_t       *)constant->ptrVal; break;
        case TYPE_UINT32:       constant->intVal     = *(uint32_t       *)constant->ptrVal; break;
        case TYPE_UINT:         constant->uintVal    = *(uint64_t       *)constant->ptrVal; break;
        case TYPE_BOOL:         constant->intVal     = *(bool           *)constant->ptrVal; break;
        case TYPE_CHAR:         constant->intVal     = *(unsigned char  *)constant->ptrVal; break;
        case TYPE_REAL32:       constant->realVal    = *(float          *)constant->ptrVal; break;
        case TYPE_REAL:         constant->realVal    = *(double         *)constant->ptrVal; break;
        case TYPE_PTR:          constant->ptrVal     = *(void *         *)constant->ptrVal; break;
        case TYPE_WEAKPTR:      constant->weakPtrVal = *(uint64_t       *)constant->ptrVal; break;
        case TYPE_STR:          constant->ptrVal     = *(void *         *)constant->ptrVal; break;
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:      break;  // Always represented by pointer, not dereferenced
        case TYPE_FIBER:        constant->ptrVal     = *(void *         *)constant->ptrVal; break;
        case TYPE_FN:           constant->intVal     = *(int64_t        *)constant->ptrVal; break;

        default:
        {
            if (consts)
                consts->error->handler(consts->error->context, "Illegal type");
            return false;
        }
    }

    return true;
}


bool constAssign(const Consts *consts, void *lhs, const Const *rhs, TypeKind typeKind, int size)
{
    if (typeOverflow(typeKind, *rhs))
    {
        if (consts)
            consts->error->handler(consts->error->context, "Overflow in assignment to %s", typeKindSpelling(typeKind));
        return false;
    }

    switch (typeKind)
    {
        case TYPE_INT8:         *(int8_t        *)lhs = rhs->intVal;         break;
        case TYPE_INT16:        *(int16_t       *)lhs = rhs->intVal;         break;
        case TYPE_INT32:        *(int32_t       *)lhs = rhs->intVal;         break;
        case TYPE_INT:          *(int64_t       *)lhs = rhs->intVal;         break;
        case TYPE_UINT8:        *(uint8_t       *)lhs = rhs->intVal;         break;
        case TYPE_UINT16:       *(uint16_t      *)lhs = rhs->intVal;         break;
        case TYPE_UINT32:       *(uint32_t      *)lhs = rhs->intVal;         break;
        case TYPE_UINT:         *(uint64_t      *)lhs = rhs->uintVal;        break;
        case TYPE_BOOL:         *(bool          *)lhs = rhs->intVal;         break;
        case TYPE_CHAR:         *(unsigned char *)lhs = rhs->intVal;         break;
        case TYPE_REAL32:       *(float         *)lhs = rhs->realVal;        break;
        case TYPE_REAL:         *(double        *)lhs = rhs->realVal;        break;
        case TYPE_PTR:          *(void *        *)lhs = rhs->ptrVal;         break;
        case TYPE_WEAKPTR:      *(uint64_t      *)lhs = rhs->weakPtrVal;     break;
        case TYPE_STR:          *(void *        *)lhs = rhs->ptrVal;         break;
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:      memcpy(lhs, rhs->ptrVal, size);              break;
        case TYPE_FIBER:        *(void *        *)lhs = rhs->ptrVal;         break;
        case TYPE_FN:           *(int64_t       *)lhs = rhs->intVal;         break;

        default:
        {
            if (consts)
                consts->error->handler(consts->error->context, "Illegal type"); 
            return false;
        }
    }

    return true;
}


int64_t constCompare(const Consts *consts, const Const *lhs, const Const *rhs, const Type *type)
{
    switch (type->kind)
    {
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:   return lhs->intVal - rhs->intVal;
        case TYPE_UINT:     return (lhs->uintVal == rhs->uintVal) ? 0 : (lhs->uintVal > rhs->uintVal) ? 1 : -1;
        case TYPE_BOOL:
        case TYPE_CHAR:     return lhs->intVal - rhs->intVal;
        case TYPE_REAL32:
        case TYPE_REAL:
        {
            const double diff = lhs->realVal - rhs->realVal;
            return (diff == 0.0) ? 0 : (diff > 0.0) ? 1 : -1;
        }
        case TYPE_PTR:      return (char *)lhs->ptrVal - (char *)rhs->ptrVal;
        case TYPE_WEAKPTR:  return lhs->weakPtrVal - rhs->weakPtrVal;
        case TYPE_STR:
        {
            const char *lhsStr = lhs->ptrVal;
            if (!lhsStr)
                lhsStr = "";

            const char *rhsStr = rhs->ptrVal;
            if (!rhsStr)
                rhsStr = "";

            return strcmp(lhsStr, rhsStr);
        }
        case TYPE_ARRAY:
        case TYPE_STRUCT:
        {
            if (!lhs->ptrVal || !rhs->ptrVal)
                return (char *)lhs->ptrVal - (char *)rhs->ptrVal;
            
            for (int i = 0; i < type->numItems; i++)
            {
                const Type *itemType = (type->kind == TYPE_ARRAY) ? type->base : type->field[i]->type;
                const int itemOffset = (type->kind == TYPE_ARRAY) ? (i * itemType->size) : type->field[i]->offset;                
                
                Const lhsItem = {.ptrVal = (char *)lhs->ptrVal + itemOffset};
                Const rhsItem = {.ptrVal = (char *)rhs->ptrVal + itemOffset};
            
                if (!constDeref(consts, &lhsItem, itemType->kind) || !constDeref(consts, &rhsItem, itemType->kind))
                    return (char *)lhs->ptrVal - (char *)rhs->ptrVal;

                const int64_t itemDiff = constCompare(consts, &lhsItem, &rhsItem, itemType);
                if (itemDiff != 0)
                    return itemDiff;
            }
            return 0;
        }
        case TYPE_DYNARRAY:
        {
            if (!lhs->ptrVal || !rhs->ptrVal)
                return (char *)lhs->ptrVal - (char *)rhs->ptrVal;
            
            const DynArray *lhsArray = lhs->ptrVal;
            const int64_t lhsLen = lhsArray->data ? getDims(lhsArray)->len : 0;

            const DynArray *rhsArray = rhs->ptrVal;
            const int64_t rhsLen = rhsArray->data ? getDims(rhsArray)->len : 0;

            for (int i = 0; ; i++)
            {
                if (i == lhsLen && i == rhsLen)
                    return 0;
                if (i == lhsLen)
                    return -1;
                if (i == rhsLen)
                    return 1;
                
                const int itemOffset = i * type->base->size;                
                
                Const lhsItem = {.ptrVal = (char *)lhsArray->data + itemOffset};
                Const rhsItem = {.ptrVal = (char *)rhsArray->data + itemOffset};
            
                if (!constDeref(consts, &lhsItem, type->base->kind) || !constDeref(consts, &rhsItem, type->base->kind))
                    return (char *)lhs->ptrVal - (char *)rhs->ptrVal;

                const int64_t itemDiff = constCompare(consts, &lhsItem, &rhsItem, type->base);
                if (itemDiff != 0)
                    return itemDiff;
            }
            return 0;
        }
        default:
        {
            if (consts)
                consts->error->handler(consts->error->context, "Illegal type"); 
            return 0;
        }
    }
}


void constUnary(const Consts *consts, Const *arg, TokenKind op, const Type *type)
{
    if (typeReal(type))
    {
        switch (op)
        {
            case TOK_PLUS:  break;
            case TOK_MINUS: arg->realVal = -arg->realVal; break;

            default:        consts->error->handler(consts->error->context, "Illegal operator");
        }
    }
    else
    {
        switch (op)
        {
            case TOK_PLUS:  break;
            case TOK_MINUS: arg->intVal = -arg->intVal; break;
            case TOK_NOT:   arg->intVal = !arg->intVal; break;
            case TOK_XOR:   arg->intVal = ~arg->intVal; break;

            default:        consts->error->handler(consts->error->context, "Illegal operator");
        }
    }
}


void constBinary(const Consts *consts, Const *lhs, const Const *rhs, TokenKind op, const Type *type)
{
    if (type->kind == TYPE_PTR)
    {
        switch (op)
        {
            case TOK_EQEQ:      lhs->intVal = lhs->ptrVal == rhs->ptrVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->ptrVal != rhs->ptrVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->ptrVal >  rhs->ptrVal; break;
            case TOK_LESS:      lhs->intVal = lhs->ptrVal <  rhs->ptrVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->ptrVal >= rhs->ptrVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->ptrVal <= rhs->ptrVal; break;             

            default:            consts->error->handler(consts->error->context, "Illegal operator"); return;
        }
    }
    else if (type->kind == TYPE_WEAKPTR)
    {
        switch (op)
        {
            case TOK_EQEQ:      lhs->intVal = lhs->weakPtrVal == rhs->weakPtrVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->weakPtrVal != rhs->weakPtrVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->weakPtrVal >  rhs->weakPtrVal; break;
            case TOK_LESS:      lhs->intVal = lhs->weakPtrVal <  rhs->weakPtrVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->weakPtrVal >= rhs->weakPtrVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->weakPtrVal <= rhs->weakPtrVal; break;             
            
            default:            consts->error->handler(consts->error->context, "Illegal operator"); return;
        }        
    }    
    else if (type->kind == TYPE_STR)
    {
        switch (op)
        {
            case TOK_PLUS:      strcat((char *)lhs->ptrVal, (char *)rhs->ptrVal); break;

            case TOK_EQEQ:      lhs->intVal = strcmp((char *)lhs->ptrVal, (char *)rhs->ptrVal) == 0; break;
            case TOK_NOTEQ:     lhs->intVal = strcmp((char *)lhs->ptrVal, (char *)rhs->ptrVal) != 0; break;
            case TOK_GREATER:   lhs->intVal = strcmp((char *)lhs->ptrVal, (char *)rhs->ptrVal) >  0; break;
            case TOK_LESS:      lhs->intVal = strcmp((char *)lhs->ptrVal, (char *)rhs->ptrVal) <  0; break;
            case TOK_GREATEREQ: lhs->intVal = strcmp((char *)lhs->ptrVal, (char *)rhs->ptrVal) >= 0; break;
            case TOK_LESSEQ:    lhs->intVal = strcmp((char *)lhs->ptrVal, (char *)rhs->ptrVal) <= 0; break;

            default:            consts->error->handler(consts->error->context, "Illegal operator"); return;
        }        
    }
    else if (type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY || type->kind == TYPE_STRUCT)
    {
        switch (op)
        {
            case TOK_EQEQ:      lhs->intVal = constCompare(consts, lhs, rhs, type) == 0; break;
            case TOK_NOTEQ:     lhs->intVal = constCompare(consts, lhs, rhs, type) != 0; break;
            case TOK_GREATER:   lhs->intVal = constCompare(consts, lhs, rhs, type)  > 0; break;
            case TOK_LESS:      lhs->intVal = constCompare(consts, lhs, rhs, type)  < 0; break;
            case TOK_GREATEREQ: lhs->intVal = constCompare(consts, lhs, rhs, type) >= 0; break;
            case TOK_LESSEQ:    lhs->intVal = constCompare(consts, lhs, rhs, type) <= 0; break;            
            
            default:            consts->error->handler(consts->error->context, "Illegal operator"); return;
        }
    }    
    else if (typeReal(type))
    {
        switch (op)
        {
            case TOK_PLUS:  lhs->realVal += rhs->realVal; break;
            case TOK_MINUS: lhs->realVal -= rhs->realVal; break;
            case TOK_MUL:   lhs->realVal *= rhs->realVal; break;
            case TOK_DIV:
            {
                if (rhs->realVal == 0)
                    consts->error->handler(consts->error->context, "Division by zero");
                lhs->realVal /= rhs->realVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs->realVal == 0)
                    consts->error->handler(consts->error->context, "Division by zero");
                lhs->realVal = fmod(lhs->realVal, rhs->realVal);
                break;
            }

            case TOK_EQEQ:      lhs->intVal = lhs->realVal == rhs->realVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->realVal != rhs->realVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->realVal >  rhs->realVal; break;
            case TOK_LESS:      lhs->intVal = lhs->realVal <  rhs->realVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->realVal >= rhs->realVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->realVal <= rhs->realVal; break;

            default:            consts->error->handler(consts->error->context, "Illegal operator"); return;
        }       
    }
    else if (type->kind == TYPE_UINT)
    {
        switch (op)
        {
            case TOK_PLUS:  lhs->uintVal += rhs->uintVal; break;
            case TOK_MINUS: lhs->uintVal -= rhs->uintVal; break;
            case TOK_MUL:   lhs->uintVal *= rhs->uintVal; break;
            case TOK_DIV:
            {
                if (rhs->uintVal == 0)
                    consts->error->handler(consts->error->context, "Division by zero");
                lhs->uintVal /= rhs->uintVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs->uintVal == 0)
                    consts->error->handler(consts->error->context, "Division by zero");
                lhs->uintVal %= rhs->uintVal;
                break;
            }

            case TOK_SHL:   lhs->uintVal <<= rhs->uintVal; break;
            case TOK_SHR:   lhs->uintVal >>= rhs->uintVal; break;
            case TOK_AND:   lhs->uintVal &= rhs->uintVal; break;
            case TOK_OR:    lhs->uintVal |= rhs->uintVal; break;
            case TOK_XOR:   lhs->uintVal ^= rhs->uintVal; break;

            case TOK_EQEQ:      lhs->intVal = lhs->uintVal == rhs->uintVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->uintVal != rhs->uintVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->uintVal >  rhs->uintVal; break;
            case TOK_LESS:      lhs->intVal = lhs->uintVal <  rhs->uintVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->uintVal >= rhs->uintVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->uintVal <= rhs->uintVal; break;

            default:            consts->error->handler(consts->error->context, "Illegal operator"); return;
        }
    }
    else  // All ordinal types except TYPE_UINT
    {
        switch (op)
        {
            case TOK_PLUS:  lhs->intVal += rhs->intVal; break;
            case TOK_MINUS: lhs->intVal -= rhs->intVal; break;
            case TOK_MUL:   lhs->intVal *= rhs->intVal; break;
            case TOK_DIV:
            {
                if (rhs->intVal == 0)
                    consts->error->handler(consts->error->context, "Division by zero");
                if (lhs->intVal == LLONG_MIN && rhs->intVal == -1)
                    consts->error->handler(consts->error->context, "Overflow of int");
                lhs->intVal /= rhs->intVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs->intVal == 0)
                    consts->error->handler(consts->error->context, "Division by zero");
                if (lhs->intVal == LLONG_MIN && rhs->intVal == -1)
                    consts->error->handler(consts->error->context, "Overflow of int");
                lhs->intVal %= rhs->intVal;
                break;
            }

            case TOK_SHL:   lhs->intVal <<= rhs->intVal; break;
            case TOK_SHR:   lhs->intVal >>= rhs->intVal; break;
            case TOK_AND:   lhs->intVal &= rhs->intVal; break;
            case TOK_OR:    lhs->intVal |= rhs->intVal; break;
            case TOK_XOR:   lhs->intVal ^= rhs->intVal; break;

            case TOK_EQEQ:      lhs->intVal = lhs->intVal == rhs->intVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->intVal != rhs->intVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->intVal >  rhs->intVal; break;
            case TOK_LESS:      lhs->intVal = lhs->intVal <  rhs->intVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->intVal >= rhs->intVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->intVal <= rhs->intVal; break;

            default:            consts->error->handler(consts->error->context, "Illegal operator"); return;
        }
    }
}


void constCallBuiltin(const Consts *consts, Const *arg, const Const *arg2, TypeKind argTypeKind, BuiltinFunc builtinVal)
{
    switch (builtinVal)
    {
        case BUILTIN_REAL:
        case BUILTIN_REAL_LHS:
        {
            if (argTypeKind == TYPE_UINT)
                arg->realVal = arg->uintVal;
            else
                arg->realVal = arg->intVal;
            break;
        }
        case BUILTIN_ROUND:     arg->intVal  = (int64_t)round(arg->realVal); break;
        case BUILTIN_TRUNC:     arg->intVal  = (int64_t)trunc(arg->realVal); break;
        case BUILTIN_CEIL:      arg->intVal  = (int64_t)ceil (arg->realVal); break;
        case BUILTIN_FLOOR:     arg->intVal  = (int64_t)floor(arg->realVal); break;
        case BUILTIN_ABS:
        {
            if (arg->intVal == LLONG_MIN)
                consts->error->handler(consts->error->context, "abs() domain error");
            arg->intVal = llabs(arg->intVal);
            break;
        }
        case BUILTIN_FABS:      arg->realVal = fabs(arg->realVal); break;
        case BUILTIN_SQRT:
        {
            if (arg->realVal < 0)
                consts->error->handler(consts->error->context, "sqrt() domain error");
            arg->realVal = sqrt(arg->realVal);
            break;
        }
        case BUILTIN_SIN:       arg->realVal = sin (arg->realVal); break;
        case BUILTIN_COS:       arg->realVal = cos (arg->realVal); break;
        case BUILTIN_ATAN:      arg->realVal = atan(arg->realVal); break;
        case BUILTIN_ATAN2:
        {
            if (arg->realVal == 0 || arg2->realVal == 0)
                consts->error->handler(consts->error->context, "atan2() domain error");
            arg->realVal = atan2(arg->realVal, arg2->realVal);
            break;
        }
        case BUILTIN_EXP:       arg->realVal = exp (arg->realVal); break;
        case BUILTIN_LOG:
        {
            if (arg->realVal <= 0)
                consts->error->handler(consts->error->context, "log() domain error");
            arg->realVal = log(arg->realVal);
            break;
        }
        case BUILTIN_LEN:       arg->intVal  = strlen((char *)arg->ptrVal); break;

        default: consts->error->handler(consts->error->context, "Illegal function");
    }

}


void constArrayAlloc(ConstArray *array, Storage *storage, const Type *type)
{
    array->storage = storage;
    array->type = type;
    array->len = 0;
    array->capacity = 4;
    array->data = storageAdd(array->storage, array->capacity * sizeof(Const));
}


void constArrayAppend(ConstArray *array, Const val)
{
    if (array->len == array->capacity)
    {
        array->capacity *= 2;
        array->data = storageRealloc(array->storage, array->data, array->capacity * sizeof(Const));
    }
    array->data[array->len++] = val;
}


int constArrayFind(const Consts *consts, const ConstArray *array, Const val)
{
    for (int i = 0; i < array->len; i++)
    {
        Const result = array->data[i];
        constBinary(consts, &result, &val, TOK_EQEQ, array->type);
        if (result.intVal)
            return i;
    }
    return -1;
}


int constArrayFindEquivalentType(const Consts *consts, const ConstArray *array, Const val)
{
    for (int i = 0; i < array->len; i++)
    {
        if (typeEquivalent((Type *)array->data[i].ptrVal, (Type *)val.ptrVal))
            return i;
    }
    return -1;
}


void constArrayFree(ConstArray *array)
{
    storageRemove(array->storage, array->data);
}

