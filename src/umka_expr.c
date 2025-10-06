#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#include "umka_expr.h"
#include "umka_decl.h"
#include "umka_stmt.h"


static void parseDynArrayLiteral(Umka *umka, const Type **type, Const *constant);


void doPushConst(Umka *umka, const Type *type, const Const *constant)
{
    if (type->kind == TYPE_UINT)
        genPushUIntConst(&umka->gen, constant->uintVal);
    else if (typeOrdinal(type) || type->kind == TYPE_FN)
        genPushIntConst(&umka->gen, constant->intVal);
    else if (typeReal(type))
        genPushRealConst(&umka->gen, constant->realVal);
    else if (type->kind == TYPE_PTR || type->kind == TYPE_STR || type->kind == TYPE_FIBER || typeStructured(type))
        genPushGlobalPtr(&umka->gen, constant->ptrVal);
    else if (type->kind == TYPE_WEAKPTR)
        genPushUIntConst(&umka->gen, constant->weakPtrVal);
    else
        umka->error.handler(umka->error.context, "Illegal type");
}


void doPushVarPtr(Umka *umka, const Ident *ident)
{
    if (ident->block == 0)
        genPushGlobalPtr(&umka->gen, ident->ptr);
    else
        genPushLocalPtr(&umka->gen, ident->offset);
}


static void doPassParam(Umka *umka, const Type *formalParamType)
{
    if (doTryRemoveCopyResultToTempVar(umka))
    {
        // Optimization: if the actual parameter is a function call, assume its reference count to be already increased before return
        // The formal parameter variable will hold this additional reference, so we can remove the temporary "reference holder" variable
    }
    else
    {
        // General case: increase parameter's reference count
        genChangeRefCnt(&umka->gen, TOK_PLUSPLUS, formalParamType);
    }

    // Non-trivial assignment to parameters
    if (typeNarrow(formalParamType) || typeStructured(formalParamType))
        genAssignParam(&umka->gen, formalParamType->kind, typeSize(&umka->types, formalParamType));
}


void doCopyResultToTempVar(Umka *umka, const Type *type)
{
    const Ident *resultCopy = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, type, true);
    genCopyResultToTempVar(&umka->gen, type, resultCopy->offset);
}


bool doTryRemoveCopyResultToTempVar(Umka *umka)
{
    if (!umka->idents.lastTempVarForResult)
        return false;

    const int resultCopyOffset = genTryRemoveCopyResultToTempVar(&umka->gen);
    if (resultCopyOffset == 0)
        return false;

    if (resultCopyOffset != umka->idents.lastTempVarForResult->offset)
        umka->error.handler(umka->error.context, "Result copy optimization failed");

    umka->idents.lastTempVarForResult->used = false;
    return true;
}


static void doTryImplicitDeref(Umka *umka, const Type **type)
{
    if ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_PTR)
    {
        genDeref(&umka->gen, TYPE_PTR);
        *type = (*type)->base;
    }
    else if ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_WEAKPTR)
    {
        genDeref(&umka->gen, TYPE_WEAKPTR);
        genStrengthenPtr(&umka->gen);
        *type = typeAddPtrTo(&umka->types, &umka->blocks, (*type)->base->base);
    }
}


static void doEscapeToHeap(Umka *umka, const Type *ptrType)
{
    // Allocate heap
    genPushIntConst(&umka->gen, typeSize(&umka->types, ptrType->base));
    genCallTypedBuiltin(&umka->gen, ptrType->base, BUILTIN_NEW);

    // Copy to heap and use heap pointer
    genDup(&umka->gen);
    genPopReg(&umka->gen, REG_HEAP_COPY);
    genSwapChangeRefCntAssign(&umka->gen, ptrType->base);
    genPushReg(&umka->gen, REG_HEAP_COPY);

    doCopyResultToTempVar(umka, ptrType);
}


static void doOrdinalToOrdinalOrRealToRealConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
    {
        if (typeConvOverflow(dest->kind, (*src)->kind, *constant))
            umka->error.handler(umka->error.context, "Overflow of %s", typeKindSpelling(dest->kind));
    }
    else
        genAssertRange(&umka->gen, dest->kind, *src);

    *src = dest;
}


static void doIntToRealConv(Umka *umka, const Type *dest, const Type **src, Const *constant, bool lhs)
{
    BuiltinFunc builtin = lhs ? BUILTIN_REAL_LHS : BUILTIN_REAL;
    if (constant)
        constCallBuiltin(&umka->consts, constant, NULL, (*src)->kind, builtin);
    else
        genCallBuiltin(&umka->gen, (*src)->kind, builtin);

    *src = dest;
}


static void doCharToStrConv(Umka *umka, const Type *dest, const Type **src, Const *constant, bool lhs)
{
    if (constant)
    {
        char *buf = NULL;
        if (constant->intVal)
        {
            buf = storageAddStr(&umka->storage, 1);
            buf[0] = constant->intVal;
            buf[1] = 0;
        }
        else
            buf = storageAddStr(&umka->storage, 0);

        constant->ptrVal = buf;
    }
    else
    {
        if (lhs)
            genSwap(&umka->gen);

        genCallTypedBuiltin(&umka->gen, *src, BUILTIN_MAKETOSTR);
        doCopyResultToTempVar(umka, dest);

        if (lhs)
            genSwap(&umka->gen);
    }

    *src = dest;
}


static void doDynArrayToStrConv(Umka *umka, const Type *dest, const Type **src, Const *constant, bool lhs)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion to string is not allowed in constant expressions");

    if (lhs)
        genSwap(&umka->gen);

    genCallTypedBuiltin(&umka->gen, *src, BUILTIN_MAKETOSTR);
    doCopyResultToTempVar(umka, dest);

    if (lhs)
        genSwap(&umka->gen);

    *src = dest;
}


static void doStrToDynArrayConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
    {
        int len = getStrDims((char *)constant->ptrVal)->len;
        DynArray *array = storageAddDynArray(&umka->storage, dest, len);
        memcpy(array->data, constant->ptrVal, len);
        constant->ptrVal = array;
    }
    else
    {
        int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, dest);
        genPushLocalPtr(&umka->gen, resultOffset);                          // Pointer to result (hidden parameter)
        genCallTypedBuiltin(&umka->gen, dest, BUILTIN_MAKEFROMSTR);
        doCopyResultToTempVar(umka, dest);
    }

    *src = dest;
}


static void doDynArrayToArrayConv(Umka *umka, const Type *dest, const Type **src, Const *constant, bool lhs)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion to array is not allowed in constant expressions");

    if (lhs)
        genSwap(&umka->gen);

    int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, dest);
    genPushLocalPtr(&umka->gen, resultOffset);                          // Pointer to result (hidden parameter)
    genCallTypedBuiltin(&umka->gen, dest, BUILTIN_MAKETOARR);
    doCopyResultToTempVar(umka, dest);

    if (lhs)
        genSwap(&umka->gen);

    *src = dest;
}


static void doArrayToDynArrayConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
    {
        DynArray *array = storageAddDynArray(&umka->storage, dest, (*src)->numItems);
        memcpy(array->data, constant->ptrVal, (*src)->numItems * array->itemSize);
        constant->ptrVal = array;
    }
    else
    {
        int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, dest);

        genPushIntConst(&umka->gen, (*src)->numItems);                      // Dynamic array length
        genPushLocalPtr(&umka->gen, resultOffset);                          // Pointer to result (hidden parameter)
        genCallTypedBuiltin(&umka->gen, dest, BUILTIN_MAKEFROMARR);
        doCopyResultToTempVar(umka, dest);
    }

    *src = dest;
}


static void doDynArrayToDynArrayConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion from dynamic array is not allowed in constant expressions");

    // Get source array length: length = len(srcArray)
    int lenOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, umka->intType);

    genDup(&umka->gen);
    genCallBuiltin(&umka->gen, (*src)->kind, BUILTIN_LEN);
    genPushLocalPtr(&umka->gen, lenOffset);
    genSwapAssign(&umka->gen, TYPE_INT, 0);

    // Allocate destination array: destArray = make(dest, length)
    const Ident *destArray = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, dest, false);
    doZeroVar(umka, destArray);

    genPushLocal(&umka->gen, TYPE_INT, lenOffset);
    doPushVarPtr(umka, destArray);
    genCallTypedBuiltin(&umka->gen, dest, BUILTIN_MAKE);
    genPop(&umka->gen);

    // Loop initialization: index = length - 1
    int indexOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, umka->intType);

    genPushLocal(&umka->gen, TYPE_INT, lenOffset);
    genPushIntConst(&umka->gen, 1);
    genBinary(&umka->gen, TOK_MINUS, umka->intType);
    genPushLocalPtr(&umka->gen, indexOffset);
    genSwapAssign(&umka->gen, TYPE_INT, 0);

    // Loop condition: index >= 0
    genWhileCondProlog(&umka->gen);

    genPushLocal(&umka->gen, TYPE_INT, indexOffset);
    genPushIntConst(&umka->gen, 0);
    genBinary(&umka->gen, TOK_GREATEREQ, umka->intType);

    genWhileCondEpilog(&umka->gen);

    // Additional scope embracing temporary variables declaration
    blocksEnter(&umka->blocks);

    // Loop body: destArray[index] = destItemType(srcArray[index]); index--
    genDup(&umka->gen);
    genPushLocal(&umka->gen, TYPE_INT, indexOffset);
    genGetDynArrayPtr(&umka->gen);
    genDeref(&umka->gen, (*src)->base->kind);

    const Type *castType = (*src)->base;
    doExplicitTypeConv(umka, dest->base, &castType, constant);

    if (!typeEquivalent(dest->base, castType))
    {
        char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
        umka->error.handler(umka->error.context, "Cannot cast %s to %s", typeSpelling((*src)->base, srcBuf), typeSpelling(dest->base, destBuf));
    }

    doPushVarPtr(umka, destArray);
    genDeref(&umka->gen, dest->kind);
    genPushLocal(&umka->gen, TYPE_INT, indexOffset);
    genGetDynArrayPtr(&umka->gen);
    genSwapChangeRefCntAssign(&umka->gen, dest->base);

    genPushLocalPtr(&umka->gen, indexOffset);
    genUnary(&umka->gen, TOK_MINUSMINUS, umka->intType);

    // Additional scope embracing temporary variables declaration
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);

    genWhileEpilog(&umka->gen);

    // Remove srcArray and push destArray
    genPop(&umka->gen);
    doPushVarPtr(umka, destArray);
    genDeref(&umka->gen, dest->kind);

    *src = dest;
}


static void doPtrToInterfaceConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
    {
        // Special case: any(null) is allowed in constant expressions
        if (typeEquivalent(dest, umka->anyType) && typeEquivalent(*src, umka->ptrNullType))
            constant->ptrVal = storageAdd(&umka->storage, typeSize(&umka->types, dest));
        else
            umka->error.handler(umka->error.context, "Conversion to interface is not allowed in constant expressions");
    }
    else
    {
        int destOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, dest);

        // Assign to #self
        genPushLocalPtr(&umka->gen, destOffset);                                // Push dest.#self pointer
        genSwapAssign(&umka->gen, TYPE_PTR, 0);                                 // Assign to dest.#self

        // Assign to #selftype (RTTI)
        const Field *selfType = typeAssertFindField(&umka->types, dest, "#selftype", NULL);

        genPushGlobalPtr(&umka->gen, (Type *)(*src));                           // Push src type
        genPushLocalPtr(&umka->gen, destOffset + selfType->offset);             // Push dest.#selftype pointer
        genSwapAssign(&umka->gen, TYPE_PTR, 0);                                 // Assign to dest.#selftype

        // Assign to methods
        for (int i = 2; i < dest->numItems; i++)
        {
            const char *name = dest->field[i]->name;

            const Type *rcvType = (*src)->base;
            if (rcvType->kind == TYPE_NULL)
                genPushIntConst(&umka->gen, 0);                                 // Allow assigning null to a non-empty interface
            else
            {
                int rcvTypeModule = rcvType->typeIdent ? rcvType->typeIdent->module : -1;

                const Ident *srcMethod = identFind(&umka->idents, &umka->modules, &umka->blocks, rcvTypeModule, name, *src, true);
                if (!srcMethod)
                {
                    char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
                    umka->error.handler(umka->error.context, "Cannot convert %s to %s: method %s is not implemented", typeSpelling(*src, srcBuf), typeSpelling(dest, destBuf), name);
                }

                if (!typeCompatible(dest->field[i]->type, srcMethod->type))
                {
                    char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
                    umka->error.handler(umka->error.context, "Cannot convert %s to %s: method %s has incompatible signature", typeSpelling(*src, srcBuf), typeSpelling(dest, destBuf), name);
                }

                genPushIntConst(&umka->gen, srcMethod->offset);                 // Push src value
            }

            genPushLocalPtr(&umka->gen, destOffset + dest->field[i]->offset);   // Push dest.method pointer
            genSwapAssign(&umka->gen, TYPE_FN, 0);                              // Assign to dest.method
        }

        genPushLocalPtr(&umka->gen, destOffset);
    }

    *src = dest;
}


static void doInterfaceToInterfaceConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion to interface is not allowed in constant expressions");

    int destOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, dest);

    // Assign to #self
    genDup(&umka->gen);                                                     // Duplicate src pointer
    genDeref(&umka->gen, TYPE_PTR);                                         // Get src.#self value
    genPushLocalPtr(&umka->gen, destOffset);                                // Push dest pointer
    genSwapAssign(&umka->gen, TYPE_PTR, 0);                                 // Assign to dest.#self (NULL means a dynamic type)

    // Assign to #selftype (RTTI)
    const Field *selfType = typeAssertFindField(&umka->types, dest, "#selftype", NULL);

    genDup(&umka->gen);                                                     // Duplicate src pointer
    genGetFieldPtr(&umka->gen, selfType->offset);                           // Get src.#selftype pointer
    genDeref(&umka->gen, TYPE_PTR);                                         // Get src.#selftype value
    genPushLocalPtr(&umka->gen, destOffset + selfType->offset);             // Push dest.#selftype pointer
    genSwapAssign(&umka->gen, TYPE_PTR, 0);                                 // Assign to dest.#selftype

    // Assign to methods
    for (int i = 2; i < dest->numItems; i++)
    {
        const char *name = dest->field[i]->name;
        const Field *srcMethod = typeFindField(*src, name, NULL);
        if (!srcMethod)
        {
            char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
            umka->error.handler(umka->error.context, "Cannot convert %s to %s: method %s is not implemented", typeSpelling(*src, srcBuf), typeSpelling(dest, destBuf), name);
        }

        if (!typeCompatible(dest->field[i]->type, srcMethod->type))
        {
            char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
            umka->error.handler(umka->error.context, "Cannot convert %s to %s: method %s has incompatible signature", typeSpelling(*src, srcBuf), typeSpelling(dest, destBuf), name);
        }

        genDup(&umka->gen);                                                 // Duplicate src pointer
        genGetFieldPtr(&umka->gen, srcMethod->offset);                      // Get src.method pointer
        genDeref(&umka->gen, TYPE_FN);                                      // Get src.method value (entry point)
        genPushLocalPtr(&umka->gen, destOffset + dest->field[i]->offset);   // Push dest.method pointer
        genSwapAssign(&umka->gen, TYPE_FN, 0);                              // Assign to dest.method
    }

    genPop(&umka->gen);                                                     // Remove src pointer
    genPushLocalPtr(&umka->gen, destOffset);
    *src = dest;
}


static void doValueToInterfaceConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion to interface is not allowed in constant expressions");

    *src = typeAddPtrTo(&umka->types, &umka->blocks, *src);
    doEscapeToHeap(umka, *src);
    doPtrToInterfaceConv(umka, dest, src, constant);
}


static void doInterfaceToPtrConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion from interface is not allowed in constant expressions");

    genAssertType(&umka->gen, dest);
    *src = dest;
}


static void doInterfaceToValueConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion from interface is not allowed in constant expressions");

    const Type *destPtrType = typeAddPtrTo(&umka->types, &umka->blocks, dest);
    genAssertType(&umka->gen, destPtrType);
    genDeref(&umka->gen, dest->kind);
    *src = dest;
}


static void doPtrToWeakPtrConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion to weak pointer is not allowed in constant expressions");

    genWeakenPtr(&umka->gen);

    *src = dest;
}


static void doWeakPtrToPtrConv(Umka *umka, const Type *dest, const Type **src, Const *constant, bool lhs)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion from weak pointer is not allowed in constant expressions");

    if (lhs)
        genSwap(&umka->gen);

    genStrengthenPtr(&umka->gen);

    if (lhs)
        genSwap(&umka->gen);

    *src = dest;
}


static void doFnToClosureConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion to closure is not allowed in constant expressions");

    int destOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, dest);

    genPushLocalPtr(&umka->gen, destOffset);
    genZero(&umka->gen, typeSize(&umka->types, dest));

    genPushLocalPtr(&umka->gen, destOffset + dest->field[0]->offset);   // Push dest.#fn pointer
    genSwapAssign(&umka->gen, TYPE_FN, 0);                              // Assign to dest.#fn

    genPushLocalPtr(&umka->gen, destOffset);
    *src = dest;
}


static void doExprListToExprListConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Conversion to expression list is not allowed in constant expressions");

    const Ident *destList = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, dest, false);
    doZeroVar(umka, destList);

    // Assign to fields
    for (int i = 0; i < dest->numItems; i++)
    {
        genDup(&umka->gen);                                                     // Duplicate src pointer
        genGetFieldPtr(&umka->gen, (*src)->field[i]->offset);                   // Get src.item pointer
        genDeref(&umka->gen, (*src)->field[i]->type->kind);                     // Get src.item value

        const Type *srcFieldType = (*src)->field[i]->type;
        doAssertImplicitTypeConv(umka, dest->field[i]->type, &srcFieldType, constant);

        genPushLocalPtr(&umka->gen, destList->offset + dest->field[i]->offset); // Push dest.item pointer
        genSwapChangeRefCntAssign(&umka->gen, dest->field[i]->type);            // Assign to dest.item
    }

    genPop(&umka->gen);                                                         // Remove src pointer
    doPushVarPtr(umka, destList);
    *src = dest;
}


static void doImplicitTypeConvEx(Umka *umka, const Type *dest, const Type **src, Const *constant, bool lhs, bool rhs)
{
    // lhs/rhs can only be set to true for operands of binary operators

    // Signed to 64-bit unsigned integer or vice versa (64-bit overflow check only, not applied to operands of binary operators)
    if (!lhs && !rhs && ((dest->kind == TYPE_UINT && typeKindSigned((*src)->kind)) || (typeKindSigned(dest->kind) && (*src)->kind == TYPE_UINT)))
    {
        doOrdinalToOrdinalOrRealToRealConv(umka, dest, src, constant);
    }

    // Integer to real
    else if (typeReal(dest) && typeInteger(*src))
    {
        doIntToRealConv(umka, dest, src, constant, lhs);
    }

    // Character to string
    else if (dest->kind == TYPE_STR && (*src)->kind == TYPE_CHAR)
    {
        doCharToStrConv(umka, dest, src, constant, lhs);
    }

    // Dynamic array to string
    else if (dest->kind == TYPE_STR && (*src)->kind == TYPE_DYNARRAY && (*src)->base->kind == TYPE_CHAR)
    {
        doDynArrayToStrConv(umka, dest, src, constant, lhs);
    }

    // String to dynamic array (not applied to operands of binary operators)
    else if (!lhs && !rhs && dest->kind == TYPE_DYNARRAY && dest->base->kind == TYPE_CHAR && (*src)->kind == TYPE_STR)
    {
        doStrToDynArrayConv(umka, dest, src, constant);
    }

    // Dynamic array to array
    else if (dest->kind == TYPE_ARRAY && (*src)->kind == TYPE_DYNARRAY && typeEquivalent(dest->base, (*src)->base))
    {
        doDynArrayToArrayConv(umka, dest, src, constant, lhs);
    }

    // Array to dynamic array (not applied to operands of binary operators)
    else if (!lhs && !rhs && dest->kind == TYPE_DYNARRAY && (*src)->kind == TYPE_ARRAY && typeEquivalent(dest->base, (*src)->base))
    {
        doArrayToDynArrayConv(umka, dest, src, constant);
    }

    // Concrete to interface or interface to interface
    else if (dest->kind == TYPE_INTERFACE)
    {
        if ((*src)->kind == TYPE_INTERFACE)
        {
            // Interface to interface
            if (!typeEquivalent(dest, *src))
                doInterfaceToInterfaceConv(umka, dest, src, constant);
        }
        else if ((*src)->kind == TYPE_PTR)
        {
            // Pointer to interface
            if ((*src)->base->kind == TYPE_PTR)
                umka->error.handler(umka->error.context, "Pointer base type cannot be a pointer");

            doPtrToInterfaceConv(umka, dest, src, constant);
        }
        else if ((*src)->kind != TYPE_VOID)
        {
            // Value to interface
            doValueToInterfaceConv(umka, dest, src, constant);
        }
    }

    // Pointer to pointer
    else if (dest->kind == TYPE_PTR && (*src)->kind == TYPE_PTR && typeImplicitlyConvertibleBaseTypes(dest->base, (*src)->base))
    {
        *src = dest;
    }

    // Pointer to weak pointer (not applied to operands of binary operators)
    else if (!lhs && !rhs && dest->kind == TYPE_WEAKPTR && (*src)->kind == TYPE_PTR && (typeEquivalent(dest->base, (*src)->base) || (*src)->base->kind == TYPE_NULL))
    {
        doPtrToWeakPtrConv(umka, dest, src, constant);
    }

    // Weak pointer to pointer
    else if (dest->kind == TYPE_PTR && (*src)->kind == TYPE_WEAKPTR && (typeEquivalent(dest->base, (*src)->base) || dest->base->kind == TYPE_NULL))
    {
        doWeakPtrToPtrConv(umka, dest, src, constant, lhs);
    }

    // Function to closure
    else if (dest->kind == TYPE_CLOSURE && (*src)->kind == TYPE_FN && typeEquivalent(dest->field[0]->type, *src))
    {
        doFnToClosureConv(umka, dest, src, constant);
    }

    // Expression list to expression list (not applied to operands of binary operators)
    else if (!lhs && !rhs && typeExprListStruct(dest) && typeExprListStruct(*src) && !typeEquivalent(dest, *src) && dest->numItems == (*src)->numItems)
    {
        doExprListToExprListConv(umka, dest, src, constant);
    }
}


void doImplicitTypeConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    doImplicitTypeConvEx(umka, dest, src, constant, false, false);
}


void doAssertImplicitTypeConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    doImplicitTypeConv(umka, dest, src, constant);
    typeAssertCompatible(&umka->types, dest, *src);
}


void doExplicitTypeConv(Umka *umka, const Type *dest, const Type **src, Const *constant)
{
    doImplicitTypeConv(umka, dest, src, constant);

    // Type to same type (up to the type identifier)
    if (typeSameExceptMaybeIdent(dest, *src))
    {
        *src = dest;
    }

    // Ordinal to ordinal or real to real
    else if ((typeOrdinal(*src) && typeOrdinal(dest)) || (typeReal(*src) && typeReal(dest)))
    {
        doOrdinalToOrdinalOrRealToRealConv(umka, dest, src, constant);
    }

    // Pointer to pointer
    else if (dest->kind == TYPE_PTR && (*src)->kind == TYPE_PTR && typeExplicitlyConvertibleBaseTypes(&umka->types, dest->base, (*src)->base))
    {
        *src = dest;
    }

    // Interface to concrete (type assertion)
    else if ((*src)->kind == TYPE_INTERFACE && dest->kind != TYPE_INTERFACE)
    {
        if (dest->kind == TYPE_PTR)
        {
            // Interface to pointer
            doInterfaceToPtrConv(umka, dest, src, constant);
        }
        else
        {
            // Interface to value
            doInterfaceToValueConv(umka, dest, src, constant);
        }
    }

    // Dynamic array to string
    else if ((*src)->kind == TYPE_DYNARRAY && (*src)->base->kind == TYPE_UINT8 && dest->kind == TYPE_STR)
    {
        doDynArrayToStrConv(umka, dest, src, constant, false);
    }

    // String to dynamic array
    else if ((*src)->kind == TYPE_STR && dest->kind == TYPE_DYNARRAY && dest->base->kind == TYPE_UINT8)
    {
        doStrToDynArrayConv(umka, dest, src, constant);
    }

    // Dynamic array to dynamic array of another base type (covariant arrays)
    else if ((*src)->kind == TYPE_DYNARRAY && dest->kind == TYPE_DYNARRAY)
    {
        doDynArrayToDynArrayConv(umka, dest, src, constant);
    }
}


static void doApplyStrCat(Umka *umka, Const *constant, const Const *rightConstant, TokenKind op)
{
    if (constant)
    {
        if (op == TOK_PLUSEQ)
            umka->error.handler(umka->error.context, "Operator is not allowed in constant expressions");

        int len = getStrDims((char *)constant->ptrVal)->len + getStrDims((char *)rightConstant->ptrVal)->len;
        char *buf = storageAddStr(&umka->storage, len);
        strcpy(buf, (char *)constant->ptrVal);

        constant->ptrVal = buf;
        constBinary(&umka->consts, constant, rightConstant, TOK_PLUS, umka->strType);   // "+" only
    }
    else
    {
        genBinary(&umka->gen, op, umka->strType);                                       // "+" or "+=" only
        doCopyResultToTempVar(umka, umka->strType);
    }
}


void doApplyOperator(Umka *umka, const Type **type, const Type **rightType, Const *constant, Const *rightConstant, TokenKind op, bool apply, bool convertLhs)
{
    // First, the right-hand side type is converted to the left-hand side type
    doImplicitTypeConvEx(umka, *type, rightType, rightConstant, false, true);

    // Second, the left-hand side type is converted to the right-hand side type for symmetric operators
    if (convertLhs)
        doImplicitTypeConvEx(umka, *rightType, type, constant, true, false);

    typeAssertCompatible(&umka->types, *type, *rightType);
    typeAssertValidOperator(&umka->types, *type, op);

    if (apply)
    {
        if ((*type)->kind == TYPE_STR && (op == TOK_PLUS || op == TOK_PLUSEQ))
            doApplyStrCat(umka, constant, rightConstant, op);
        else
        {
            if (constant)
                constBinary(&umka->consts, constant, rightConstant, op, *type);
            else
                genBinary(&umka->gen, op, *type);
        }
    }
}


// qualIdent = [ident "::"] ident.
const Ident *parseQualIdent(Umka *umka)
{
    lexCheck(&umka->lex, TOK_IDENT);

    int moduleToSeekIn = umka->blocks.module;

    Lexer lookaheadLex = umka->lex;
    lexNext(&lookaheadLex);
    if (lookaheadLex.tok.kind == TOK_COLONCOLON)
    {
        const Ident *moduleIdent = identAssertFindModule(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, umka->lex.tok.name);

        lexNext(&umka->lex);
        lexNext(&umka->lex);
        lexCheck(&umka->lex, TOK_IDENT);

        moduleToSeekIn = moduleIdent->moduleVal;
    }

    const Ident *ident = identAssertFind(&umka->idents, &umka->modules, &umka->blocks, moduleToSeekIn, umka->lex.tok.name, NULL);

    if (identIsOuterLocalVar(&umka->blocks, ident))
        umka->error.handler(umka->error.context, "%s is not specified as a captured variable", ident->name);

    return ident;
}


static void parseBuiltinIOCall(Umka *umka, const Type **type, Const *constant, BuiltinFunc builtin)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Parameters: count, stream, format, value

    // Count (number of characters for printf(), number of items for scanf())
    genPushIntConst(&umka->gen, 0);

    // Stream (file/string pointer)
    if (builtin == BUILTIN_FPRINTF || builtin == BUILTIN_FSCANF  || builtin == BUILTIN_SSCANF)
    {
        const Type *expectedType = (builtin == BUILTIN_FPRINTF || builtin == BUILTIN_FSCANF) ? umka->fileType : umka->strType;
        *type = expectedType;
        parseExpr(umka, type, constant);
        doAssertImplicitTypeConv(umka, expectedType, type, constant);
        lexEat(&umka->lex, TOK_COMMA);
    }
    else
        genPushGlobalPtr(&umka->gen, NULL);

    // Format string - statically checked if given as a string literal
    const char *formatLiteral = NULL;
    if (umka->lex.tok.kind == TOK_STRLITERAL)
    {
        Lexer lookaheadLex = umka->lex;
        lexNext(&lookaheadLex);
        if (lookaheadLex.tok.kind == TOK_COMMA || lookaheadLex.tok.kind == TOK_RPAR)
            formatLiteral = umka->lex.tok.strVal;
    }

    *type = umka->strType;
    parseExpr(umka, type, constant);
    typeAssertCompatible(&umka->types, umka->strType, *type);

    // Values, if any
    int formatLen = -1, typeLetterPos = -1;
    TypeKind expectedTypeKind = TYPE_NONE;
    FormatStringTypeSize formatStringTypeSize = FORMAT_SIZE_NORMAL;

    while (umka->lex.tok.kind == TOK_COMMA)
    {     
        lexNext(&umka->lex);
        *type = NULL;
        parseExpr(umka, type, constant);

        if (formatLiteral)
        {
            if (!typeFormatStringValid(formatLiteral, &formatLen, &typeLetterPos, &expectedTypeKind, &formatStringTypeSize))
                umka->error.handler(umka->error.context, "Invalid format string");
            formatLiteral += formatLen;
        }

        typeAssertCompatibleIOBuiltin(&umka->types, expectedTypeKind, *type, builtin, false);

        if (builtin == BUILTIN_SCANF || builtin == BUILTIN_FSCANF || builtin == BUILTIN_SSCANF)
            *type = (*type)->base;

        genCallTypedBuiltin(&umka->gen, *type, builtin); 
    } // while

    // The rest of format string
    genPushIntConst(&umka->gen, 0);

    if (formatLiteral)
    {
        if (!typeFormatStringValid(formatLiteral, &formatLen, &typeLetterPos, &expectedTypeKind, &formatStringTypeSize))
            umka->error.handler(umka->error.context, "Invalid format string");
        formatLiteral += formatLen;
    }

    if (builtin == BUILTIN_SCANF || builtin == BUILTIN_FSCANF || builtin == BUILTIN_SSCANF)
        *type = umka->ptrVoidType;
    else
        *type = umka->voidType;

    typeAssertCompatibleIOBuiltin(&umka->types, expectedTypeKind, *type, builtin, true);
    genCallTypedBuiltin(&umka->gen, umka->voidType, builtin);

    genPop(&umka->gen);                         // Remove format string

    // Result
    if (builtin == BUILTIN_SPRINTF)
    {
        genSwap(&umka->gen);                    // Swap stream and count
        genPop(&umka->gen);                     // Remove count, keep stream
        *type = umka->strType;
    }
    else
    {
        genPop(&umka->gen);                     // Remove stream, keep count
        *type = umka->intType;
    }
}


static void parseBuiltinMathCall(Umka *umka, const Type **type, Const *constant, BuiltinFunc builtin)
{
    const Type *argType = (builtin == BUILTIN_ABS) ? umka->intType : umka->realType;

    *type = argType;
    parseExpr(umka, type, constant);
    doAssertImplicitTypeConv(umka, argType, type, constant);

    Const constant2Val = {.realVal = 0};
    Const *constant2 = NULL;

    // fn atan2(y, x: real): real
    if (builtin == BUILTIN_ATAN2)
    {
        lexEat(&umka->lex, TOK_COMMA);

        const Type *type2 = umka->realType;
        if (constant)
            constant2 = &constant2Val;

        parseExpr(umka, &type2, constant2);
        doAssertImplicitTypeConv(umka, umka->realType, &type2, constant2);
    }

    if (constant)
        constCallBuiltin(&umka->consts, constant, constant2, argType->kind, builtin);
    else
        genCallBuiltin(&umka->gen, argType->kind, builtin);

    if (builtin == BUILTIN_ROUND || builtin == BUILTIN_TRUNC || builtin == BUILTIN_CEIL || builtin == BUILTIN_FLOOR || builtin == BUILTIN_ABS)
        *type = umka->intType;
    else
        *type = umka->realType;
}


// fn new(type: Type, size: int [, expr: type]): ^type
static void parseBuiltinNewCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Type
    *type = parseType(umka, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_NEW, (*type)->kind != TYPE_VOID && (*type)->kind != TYPE_NULL);

    genPushIntConst(&umka->gen, typeSize(&umka->types, *type));
    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_NEW);

    // Initializer expression
    if (umka->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&umka->lex);
        genDup(&umka->gen);

        const Type *exprType = *type;
        parseExpr(umka, &exprType, NULL);
        doAssertImplicitTypeConv(umka, *type, &exprType, NULL);

        genChangeRefCntAssign(&umka->gen, *type);
    }

    *type = typeAddPtrTo(&umka->types, &umka->blocks, *type);
}


// fn make(type: Type, len: int): type
// fn make(type: Type): type
// fn make(type: Type, childFunc: fn()): type
static void parseBuiltinMakeCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    *type = parseType(umka, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_MAKE, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP || (*type)->kind == TYPE_FIBER);

    if ((*type)->kind == TYPE_DYNARRAY)
    {
        lexEat(&umka->lex, TOK_COMMA);

        // Dynamic array length
        const Type *lenType = umka->intType;
        parseExpr(umka, &lenType, NULL);
        typeAssertCompatible(&umka->types, umka->intType, lenType);

        // Pointer to result (hidden parameter)
        int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, *type);
        genPushLocalPtr(&umka->gen, resultOffset);
    }
    else if ((*type)->kind == TYPE_MAP)
    {
        // Pointer to result (hidden parameter)
        int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, *type);
        genPushLocalPtr(&umka->gen, resultOffset);
    }
    else if ((*type)->kind == TYPE_FIBER)
    {
        lexEat(&umka->lex, TOK_COMMA);

        // Child fiber closure
        const Type *fiberClosureType = umka->fiberType->base;
        parseExpr(umka, &fiberClosureType, constant);
        doAssertImplicitTypeConv(umka, umka->fiberType->base, &fiberClosureType, NULL);
    }
    else
        umka->error.handler(umka->error.context, "Illegal type");

    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_MAKE);
}


// fn copy(array: [] type): [] type
// fn copy(m: map [keyType] type): map [keyType] type
static void parseBuiltinCopyCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    *type = NULL;
    parseExpr(umka, type, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_COPY, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, *type);
    genPushLocalPtr(&umka->gen, resultOffset);

    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_COPY);
}


// fn append(array: [] type, item: (^type | [] type), single: bool, type: Type): [] type
static void parseBuiltinAppendCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    *type = NULL;
    parseExpr(umka, type, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_APPEND, (*type)->kind == TYPE_DYNARRAY);

    lexEat(&umka->lex, TOK_COMMA);

    // New item (must always be a pointer, even for value types) or right-hand side dynamic array
    const Type *itemType = (*type)->base;
    parseExpr(umka, &itemType, NULL);

    bool singleItem = true;
    if (typeEquivalent(*type, itemType))
        singleItem = false;
    else if (itemType->kind == TYPE_ARRAY && typeEquivalent((*type)->base, itemType->base))
    {
        doImplicitTypeConv(umka, *type, &itemType, NULL);
        singleItem = false;
    }

    if (singleItem)
    {
        doAssertImplicitTypeConv(umka, (*type)->base, &itemType, NULL);

        if (!typeStructured(itemType))
        {
            // Assignment to an anonymous stack area does not require updating reference counts
            int itemOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, itemType);
            genPushLocalPtr(&umka->gen, itemOffset);
            genSwapAssign(&umka->gen, itemType->kind, 0);

            genPushLocalPtr(&umka->gen, itemOffset);
        }
    }

    // 'Append single item' flag (hidden parameter)
    genPushIntConst(&umka->gen, singleItem);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, *type);
    genPushLocalPtr(&umka->gen, resultOffset);

    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_APPEND);
}


// fn insert(array: [] type, index: int, item: type, type: Type): [] type
static void parseBuiltinInsertCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    *type = NULL;
    parseExpr(umka, type, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_INSERT, (*type)->kind == TYPE_DYNARRAY);

    // New item index
    lexEat(&umka->lex, TOK_COMMA);

    const Type *indexType = umka->intType;
    parseExpr(umka, &indexType, NULL);
    doAssertImplicitTypeConv(umka, umka->intType, &indexType, NULL);

    // New item (must always be a pointer, even for value types)
    lexEat(&umka->lex, TOK_COMMA);

    const Type *itemType = (*type)->base;
    parseExpr(umka, &itemType, NULL);
    doAssertImplicitTypeConv(umka, (*type)->base, &itemType, NULL);

    if (!typeStructured(itemType))
    {
        // Assignment to an anonymous stack area does not require updating reference counts
        int itemOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, itemType);
        genPushLocalPtr(&umka->gen, itemOffset);
        genSwapAssign(&umka->gen, itemType->kind, 0);

        genPushLocalPtr(&umka->gen, itemOffset);
    }

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, *type);
    genPushLocalPtr(&umka->gen, resultOffset);

    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_INSERT);
}


// fn delete(array: [] type, index: int): [] type
// fn delete(m: map [keyType] type, key: keyType): map [keyType] type
static void parseBuiltinDeleteCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Dynamic array or map
    *type = NULL;
    parseExpr(umka, type, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_DELETE, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP);

    // Item index or map key
    lexEat(&umka->lex, TOK_COMMA);

    const Type *expectedIndexType = ((*type)->kind == TYPE_DYNARRAY) ? umka->intType : typeMapKey(*type);
    const Type *indexType = expectedIndexType;

    parseExpr(umka, &indexType, NULL);
    doAssertImplicitTypeConv(umka, expectedIndexType, &indexType, NULL);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, *type);
    genPushLocalPtr(&umka->gen, resultOffset);

    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_DELETE);
}


// fn slice(array: [] type | str, startIndex [, endIndex]: int, type: Type): [] type | str
static void parseBuiltinSliceCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Dynamic array or string
    *type = NULL;
    parseExpr(umka, type, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SLICE, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_STR);

    lexEat(&umka->lex, TOK_COMMA);

    const Type *indexType = umka->intType;

    // Start index
    parseExpr(umka, &indexType, NULL);
    doAssertImplicitTypeConv(umka, umka->intType, &indexType, NULL);

    if (umka->lex.tok.kind == TOK_COMMA)
    {
        // Optional end index
        lexNext(&umka->lex);
        parseExpr(umka, &indexType, NULL);
        doAssertImplicitTypeConv(umka, umka->intType, &indexType, NULL);
    }
    else
        genPushIntConst(&umka->gen, INT_MIN);

    if ((*type)->kind == TYPE_DYNARRAY)
    {
        // Pointer to result (hidden parameter)
        int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, *type);
        genPushLocalPtr(&umka->gen, resultOffset);
    }
    else
        genPushGlobalPtr(&umka->gen, NULL);

    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_SLICE);
}


// fn sort(array: [] type, compare: fn (a, b: ^type): int)
// fn sort(array: [] type, ascending: bool [, ident])
static void parseBuiltinSortCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    *type = NULL;
    parseExpr(umka, type, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SORT, (*type)->kind == TYPE_DYNARRAY);

    lexEat(&umka->lex, TOK_COMMA);

    // Compare closure or ascending/descending order flag
    Type *fnType = typeAdd(&umka->types, &umka->blocks, TYPE_FN);
    const Type *paramType = typeAddPtrTo(&umka->types, &umka->blocks, (*type)->base);

    typeAddParam(&umka->types, &fnType->sig, umka->anyType, "#upvalues", (Const){0});
    typeAddParam(&umka->types, &fnType->sig, paramType, "a", (Const){0});
    typeAddParam(&umka->types, &fnType->sig, paramType, "b", (Const){0});

    fnType->sig.resultType = umka->intType;

    Type *expectedCompareType = typeAdd(&umka->types, &umka->blocks, TYPE_CLOSURE);
    typeAddField(&umka->types, expectedCompareType, fnType, "#fn");
    typeAddField(&umka->types, expectedCompareType, umka->anyType, "#upvalues");

    const Type *compareOrFlagType = expectedCompareType;
    parseExpr(umka, &compareOrFlagType, NULL);

    if (typeEquivalent(compareOrFlagType, umka->boolType))
    {
        // "Fast" form

        if (umka->lex.tok.kind == TOK_COMMA)
        {
            // Item type is a structure with a comparable field
            typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SORT, (*type)->base->kind == TYPE_STRUCT);

            // Field name
            lexEat(&umka->lex, TOK_COMMA);
            lexCheck(&umka->lex, TOK_IDENT);

            const Field *field = typeAssertFindField(&umka->types, (*type)->base, umka->lex.tok.name, NULL);
            typeAssertValidOperator(&umka->types, field->type, TOK_LESS);

            lexNext(&umka->lex);

            genPushIntConst(&umka->gen, field->offset);
            genCallTypedBuiltin(&umka->gen, field->type, BUILTIN_SORTFAST);
        }
        else
        {
            // Item type is comparable
            typeAssertValidOperator(&umka->types, (*type)->base, TOK_LESS);

            genPushIntConst(&umka->gen, 0);
            genCallTypedBuiltin(&umka->gen, (*type)->base, BUILTIN_SORTFAST);
        }
    }
    else
    {
        // "General" form

        // Compare closure type (hidden parameter)
        doAssertImplicitTypeConv(umka, expectedCompareType, &compareOrFlagType, NULL);
        genPushGlobalPtr(&umka->gen, (Type *)compareOrFlagType);

        genCallTypedBuiltin(&umka->gen, (*type)->base, BUILTIN_SORT);
    }

    *type = umka->voidType;
}


static void parseBuiltinLenCall(Umka *umka, const Type **type, Const *constant)
{
    *type = NULL;
    parseExpr(umka, type, constant);

    switch ((*type)->kind)
    {
        case TYPE_ARRAY:
        {
            if (constant)
                constant->intVal = (*type)->numItems;
            else
            {
                genPop(&umka->gen);
                genPushIntConst(&umka->gen, (*type)->numItems);
            }
            break;
        }
        case TYPE_DYNARRAY:
        {
            if (constant)
                umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

            genCallBuiltin(&umka->gen, TYPE_DYNARRAY, BUILTIN_LEN);
            break;
        }
        case TYPE_STR:
        {
            if (constant)
                constCallBuiltin(&umka->consts, constant, NULL, TYPE_STR, BUILTIN_LEN);
            else
                genCallBuiltin(&umka->gen, TYPE_STR, BUILTIN_LEN);
            break;
        }
        case TYPE_MAP:
        {
            if (constant)
                umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

            genCallBuiltin(&umka->gen, TYPE_MAP, BUILTIN_LEN);
            break;
        }
        default:
        {
            typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_LEN, false);
            return;
        }
    }

    *type = umka->intType;
}


static void parseBuiltinCapCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    *type = NULL;
    parseExpr(umka, type, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_CAP, (*type)->kind == TYPE_DYNARRAY);

    genCallBuiltin(&umka->gen, TYPE_DYNARRAY, BUILTIN_CAP);
    *type = umka->intType;
}


// fn sizeof(T | a: T): int
static void parseBuiltinSizeofCall(Umka *umka, const Type **type, Const *constant)
{
    *type = NULL;

    // sizeof(T)
    if (umka->lex.tok.kind == TOK_IDENT)
    {
        const Ident *ident = identFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, umka->lex.tok.name, NULL, false);
        if (ident && ident->kind == IDENT_TYPE)
        {
            Lexer lookaheadLex = umka->lex;
            lexNext(&lookaheadLex);
            if (lookaheadLex.tok.kind == TOK_RPAR)
            {
                lexNext(&umka->lex);
                *type = ident->type;
                identSetUsed(ident);
            }
        }
    }

    // sizeof(a: T)
    if (!(*type))
    {
        *type = NULL;
        parseExpr(umka, type, constant);
        if ((*type)->kind != TYPE_VOID)
            genPop(&umka->gen);
    }

    int size = typeSize(&umka->types, *type);

    if (constant)
        constant->intVal = size;
    else
        genPushIntConst(&umka->gen, size);

    *type = umka->intType;
}


static void parseBuiltinSizeofselfCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    *type = NULL;
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SIZEOFSELF, (*type)->kind == TYPE_INTERFACE);

    genCallBuiltin(&umka->gen, TYPE_INTERFACE, BUILTIN_SIZEOFSELF);
    *type = umka->intType;
}


static void parseBuiltinSelfptrCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    *type = NULL;
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SELFPTR, (*type)->kind == TYPE_INTERFACE);

    genCallBuiltin(&umka->gen, TYPE_INTERFACE, BUILTIN_SELFPTR);
    *type = umka->ptrVoidType;
}


static void parseBuiltinSelfhasptrCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    *type = NULL;
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SELFHASPTR, (*type)->kind == TYPE_INTERFACE);

    genCallBuiltin(&umka->gen, TYPE_INTERFACE, BUILTIN_SELFHASPTR);
    *type = umka->boolType;
}


static void parseBuiltinSelftypeeqCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Left interface
    *type = NULL;
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SELFTYPEEQ, (*type)->kind == TYPE_INTERFACE);

    lexEat(&umka->lex, TOK_COMMA);

    // Right interface
    *type = NULL;
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_SELFTYPEEQ, (*type)->kind == TYPE_INTERFACE);

    genCallBuiltin(&umka->gen, TYPE_INTERFACE, BUILTIN_SELFTYPEEQ);
    *type = umka->boolType;
}


// fn typeptr(T): ^void
static void parseBuiltinTypeptrCall(Umka *umka, const Type **type, Const *constant)
{
    *type = parseType(umka, NULL);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_TYPEPTR, (*type)->kind != TYPE_VOID && (*type)->kind != TYPE_NULL);

    if (constant)
        constant->ptrVal = (Type *)(*type);
    else
        genPushGlobalPtr(&umka->gen, (Type *)(*type));

    *type = umka->ptrVoidType;
}


static void parseBuiltinValidCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    *type = NULL;
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_VALID, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP || (*type)->kind == TYPE_INTERFACE || (*type)->kind == TYPE_FN || (*type)->kind == TYPE_CLOSURE || (*type)->kind == TYPE_FIBER);

    genCallBuiltin(&umka->gen, (*type)->kind, BUILTIN_VALID);
    *type = umka->boolType;
}


// fn validkey(m: map [keyType] type, key: keyType): bool
static void parseBuiltinValidkeyCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Map
    *type = NULL;
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_VALIDKEY, (*type)->kind == TYPE_MAP);

    lexEat(&umka->lex, TOK_COMMA);

    // Map key
    const Type *keyType = typeMapKey(*type);
    parseExpr(umka, &keyType, constant);
    doAssertImplicitTypeConv(umka, typeMapKey(*type), &keyType, NULL);

    genCallBuiltin(&umka->gen, (*type)->kind, BUILTIN_VALIDKEY);
    *type = umka->boolType;
}


// fn keys(m: map [keyType] type): []keyType
static void parseBuiltinKeysCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    // Map
    parseExpr(umka, type, constant);
    typeAssertCompatibleBuiltin(&umka->types, *type, BUILTIN_KEYS, (*type)->kind == TYPE_MAP);

    // Result type (hidden parameter)
    Type *keysType = typeAdd(&umka->types, &umka->blocks, TYPE_DYNARRAY);
    keysType->base = typeMapKey(*type);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, keysType);
    genPushLocalPtr(&umka->gen, resultOffset);

    genCallTypedBuiltin(&umka->gen, keysType, BUILTIN_KEYS);
    *type = keysType;
}


// fn resume([child: fiber])
static void parseBuiltinResumeCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    if (umka->lex.tok.kind != TOK_RPAR)
    {
        // Child fiber
        parseExpr(umka, type, constant);
        doAssertImplicitTypeConv(umka, umka->fiberType, type, constant);
    }
    else
    {
        // Parent fiber (implied)
        genPushGlobalPtr(&umka->gen, NULL);
    }

    genCallBuiltin(&umka->gen, TYPE_NONE, BUILTIN_RESUME);
    *type = umka->voidType;
}


// fn memusage(): int
static void parseBuiltinMemusageCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    genCallBuiltin(&umka->gen, TYPE_INT, BUILTIN_MEMUSAGE);
    *type = umka->intType;
}


// fn exit(code: int, msg: str = "")
static void parseBuiltinExitCall(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
        umka->error.handler(umka->error.context, "Function is not allowed in constant expressions");

    *type = umka->intType;
    parseExpr(umka, type, constant);
    doAssertImplicitTypeConv(umka, umka->intType, type, constant);

    if (umka->lex.tok.kind == TOK_RPAR)
    {
        genPushGlobalPtr(&umka->gen, storageAddStr(&umka->storage, 0));
    }
    else
    {
        lexEat(&umka->lex, TOK_COMMA);

        parseExpr(umka, type, constant);
        doAssertImplicitTypeConv(umka, umka->strType, type, constant);
    }

    genCallBuiltin(&umka->gen, TYPE_VOID, BUILTIN_EXIT);
    *type = umka->voidType;
}


// builtinCall = qualIdent "(" [expr {"," expr}] ")".
static void parseBuiltinCall(Umka *umka, const Type **type, Const *constant, BuiltinFunc builtin)
{
    lexEat(&umka->lex, TOK_LPAR);

    switch (builtin)
    {
        // I/O
        case BUILTIN_PRINTF:
        case BUILTIN_FPRINTF:
        case BUILTIN_SPRINTF:
        case BUILTIN_SCANF:
        case BUILTIN_FSCANF:
        case BUILTIN_SSCANF:        parseBuiltinIOCall(umka, type, constant, builtin);      break;

        // Math
        case BUILTIN_ROUND:
        case BUILTIN_TRUNC:
        case BUILTIN_CEIL:
        case BUILTIN_FLOOR:
        case BUILTIN_ABS:
        case BUILTIN_FABS:
        case BUILTIN_SQRT:
        case BUILTIN_SIN:
        case BUILTIN_COS:
        case BUILTIN_ATAN:
        case BUILTIN_ATAN2:
        case BUILTIN_EXP:
        case BUILTIN_LOG:           parseBuiltinMathCall(umka, type, constant, builtin);    break;

        // Memory
        case BUILTIN_NEW:           parseBuiltinNewCall(umka, type, constant);              break;
        case BUILTIN_MAKE:          parseBuiltinMakeCall(umka, type, constant);             break;
        case BUILTIN_COPY:          parseBuiltinCopyCall(umka, type, constant);             break;
        case BUILTIN_APPEND:        parseBuiltinAppendCall(umka, type, constant);           break;
        case BUILTIN_INSERT:        parseBuiltinInsertCall(umka, type, constant);           break;
        case BUILTIN_DELETE:        parseBuiltinDeleteCall(umka, type, constant);           break;
        case BUILTIN_SLICE:         parseBuiltinSliceCall(umka, type, constant);            break;
        case BUILTIN_SORT:          parseBuiltinSortCall(umka, type, constant);             break;
        case BUILTIN_LEN:           parseBuiltinLenCall(umka, type, constant);              break;
        case BUILTIN_CAP:           parseBuiltinCapCall(umka, type, constant);              break;
        case BUILTIN_SIZEOF:        parseBuiltinSizeofCall(umka, type, constant);           break;
        case BUILTIN_SIZEOFSELF:    parseBuiltinSizeofselfCall(umka, type, constant);       break;
        case BUILTIN_SELFPTR:       parseBuiltinSelfptrCall(umka, type, constant);          break;
        case BUILTIN_SELFHASPTR:    parseBuiltinSelfhasptrCall(umka, type, constant);       break;
        case BUILTIN_SELFTYPEEQ:    parseBuiltinSelftypeeqCall(umka, type, constant);       break;
        case BUILTIN_TYPEPTR:       parseBuiltinTypeptrCall(umka, type, constant);          break;
        case BUILTIN_VALID:         parseBuiltinValidCall(umka, type, constant);            break;

        // Maps
        case BUILTIN_VALIDKEY:      parseBuiltinValidkeyCall(umka, type, constant);         break;
        case BUILTIN_KEYS:          parseBuiltinKeysCall(umka, type, constant);             break;

        // Fibers
        case BUILTIN_RESUME:        parseBuiltinResumeCall(umka, type, constant);           break;

        // Misc
        case BUILTIN_MEMUSAGE:      parseBuiltinMemusageCall(umka, type, constant);         break;
        case BUILTIN_EXIT:          parseBuiltinExitCall(umka, type, constant);             break;

        default: umka->error.handler(umka->error.context, "Illegal built-in function");
    }

    // Allow closing parenthesis on a new line
    if (umka->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&umka->lex);

    lexEat(&umka->lex, TOK_RPAR);
}


// actualParams = "(" [expr {"," expr}] ")".
static void parseCall(Umka *umka, const Type **type)
{
    lexEat(&umka->lex, TOK_LPAR);

    // Decide whether a (default) indirect call can be replaced with a direct call
    int immediateEntryPoint = (*type)->kind == TYPE_FN ? genTryRemoveImmediateEntryPoint(&umka->gen) : -1;

    // Actual parameters: [#self,] param1, param2 ...[#result]
    int numExplicitParams = 0, numPreHiddenParams = 0, numPostHiddenParams = 0;
    int i = 0;

    if ((*type)->kind == TYPE_CLOSURE)
    {
        // Closure upvalue
        const Field *fn = typeAssertFindField(&umka->types, *type, "#fn", NULL);
        *type = fn->type;

        genPushUpvalue(&umka->gen);
        doPassParam(umka, (*type)->sig.param[0]->type);

        numPreHiddenParams++;
        i++;
    }
    else if ((*type)->sig.isMethod)
    {
        // Method receiver
        genPushReg(&umka->gen, REG_SELF);

        // Increase receiver's reference count
        genChangeRefCnt(&umka->gen, TOK_PLUSPLUS, (*type)->sig.param[0]->type);

        numPreHiddenParams++;
        i++;
    }
    else
    {
        // Dummy upvalue
        genPushZero(&umka->gen, sizeof(Interface) / sizeof(Slot));

        numPreHiddenParams++;
        i++;
    }

    // #result
    if (typeStructured((*type)->sig.resultType))
        numPostHiddenParams++;

    if (umka->lex.tok.kind != TOK_RPAR)
    {
        while (1)
        {
            if (numPreHiddenParams + numExplicitParams + numPostHiddenParams > (*type)->sig.numParams - 1)
            {
                char fnTypeBuf[DEFAULT_STR_LEN + 1];
                umka->error.handler(umka->error.context, "Too many actual parameters to %s", typeSpelling(*type, fnTypeBuf));
            }

            const Type *formalParamType = (*type)->sig.param[i]->type;
            const Type *actualParamType = formalParamType;

            if (formalParamType->isVariadicParamList)
            {
                // Variadic parameter list
                parseDynArrayLiteral(umka, &formalParamType, NULL);
                actualParamType = formalParamType;
            }
            else
            {
                // Regular parameter
                parseExpr(umka, &actualParamType, NULL);

                doImplicitTypeConv(umka, formalParamType, &actualParamType, NULL);
                typeAssertCompatibleParam(&umka->types, formalParamType, actualParamType, *type, numExplicitParams + 1);
            }

            doPassParam(umka, formalParamType);
            numExplicitParams++;
            i++;

            if (umka->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&umka->lex);
        }
    }

    // Allow closing parenthesis on a new line
    if (umka->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&umka->lex);

    int numDefaultOrVariadicFormalParams = 0;

    if ((*type)->sig.numDefaultParams > 0)
        numDefaultOrVariadicFormalParams = (*type)->sig.numDefaultParams;
    else if ((*type)->sig.numParams > 0 && (*type)->sig.param[(*type)->sig.numParams - 1]->type->isVariadicParamList)
        numDefaultOrVariadicFormalParams = 1;

    if (numPreHiddenParams + numExplicitParams + numPostHiddenParams < (*type)->sig.numParams - numDefaultOrVariadicFormalParams)
    {
        char fnTypeBuf[DEFAULT_STR_LEN + 1];
        umka->error.handler(umka->error.context, "Too few actual parameters to %s", typeSpelling(*type, fnTypeBuf));
    }

    // Push default or variadic parameters, if not specified explicitly
    while (i + numPostHiddenParams < (*type)->sig.numParams)
    {
        const Type *formalParamType = (*type)->sig.param[i]->type;

        if ((*type)->sig.numDefaultParams > 0)
            doPushConst(umka, formalParamType, &((*type)->sig.param[i]->defaultVal));   // Default parameter
        else
            parseDynArrayLiteral(umka, &formalParamType, NULL);                         // Variadic parameter (empty dynamic array)

        doPassParam(umka, formalParamType);
        i++;
    }

    // Push #result pointer
    if (typeStructured((*type)->sig.resultType))
    {
        int offset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, (*type)->sig.resultType);
        genPushLocalPtr(&umka->gen, offset);
        i++;
    }

    if (immediateEntryPoint > 0)
        genCall(&umka->gen, immediateEntryPoint);                                           // Direct call
    else if (immediateEntryPoint < 0)
    {
        int paramSlots = typeParamSizeTotal(&umka->types, &(*type)->sig) / sizeof(Slot);
        genCallIndirect(&umka->gen, paramSlots);                                            // Indirect call
        genPop(&umka->gen);                                                                 // Pop entry point
    }
    else
        umka->error.handler(umka->error.context, "Called function is not defined");

    *type = (*type)->sig.resultType;

    lexEat(&umka->lex, TOK_RPAR);
}


// primary = qualIdent | builtinCall.
static void parsePrimary(Umka *umka, const Ident *ident, const Type **type, Const *constant, bool *isVar, bool *isCall)
{
    switch (ident->kind)
    {
        case IDENT_CONST:
        {
            if (constant)
                *constant = ident->constant;
            else
                doPushConst(umka, ident->type, &ident->constant);

            *type = ident->type;
            *isVar = false;
            *isCall = false;
            lexNext(&umka->lex);
            break;
        }

        case IDENT_VAR:
        {
            if (constant)
                umka->error.handler(umka->error.context, "Constant expected but variable %s found", ident->name);

            doPushVarPtr(umka, ident);

            if (typeStructured(ident->type))
                *type = ident->type;
            else
                *type = typeAddPtrTo(&umka->types, &umka->blocks, ident->type);
            *isVar = true;
            *isCall = false;
            lexNext(&umka->lex);
            break;
        }

        // Built-in function call
        case IDENT_BUILTIN_FN:
        {
            lexNext(&umka->lex);
            parseBuiltinCall(umka, type, constant, ident->builtin);

            // Copy result to a temporary local variable to collect it as garbage when leaving the block
            if (typeGarbageCollected(*type) && ident->builtin != BUILTIN_SELFPTR && ident->builtin != BUILTIN_TYPEPTR)
                doCopyResultToTempVar(umka, *type);

            *isVar = false;
            *isCall = true;
            break;
        }

        default: umka->error.handler(umka->error.context, "Unexpected identifier %s", ident->name);
    }
}


// typeCast = type "(" expr ")".
static void parseTypeCast(Umka *umka, const Type **type, Const *constant)
{
    lexEat(&umka->lex, TOK_LPAR);

    const Type *originalType = NULL;
    parseExpr(umka, &originalType, constant);

    const Type *castType = originalType;
    doExplicitTypeConv(umka, *type, &castType, constant);

    if (!typeEquivalent(*type, castType))
    {
        char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
        umka->error.handler(umka->error.context, "Cannot cast %s to %s", typeSpelling(originalType, srcBuf), typeSpelling(*type, destBuf));
    }

    lexEat(&umka->lex, TOK_RPAR);
}


// arrayLiteral  = "{" [expr {"," expr} [","]] "}".
// structLiteral = "{" [[ident ":"] expr {"," [ident ":"] expr} [","]] "}".
static void parseArrayOrStructLiteral(Umka *umka, const Type **type, Const *constant)
{
    lexEat(&umka->lex, TOK_LBRACE);

    bool namedFields = false;
    bool *fieldInitialized = NULL;

    if ((*type)->kind == TYPE_STRUCT)
    {
        if (umka->lex.tok.kind == TOK_RBRACE)
            namedFields = true;
        else if (umka->lex.tok.kind == TOK_IDENT)
        {
            Lexer lookaheadLex = umka->lex;
            lexNext(&lookaheadLex);
            namedFields = lookaheadLex.tok.kind == TOK_COLON;
        }
    }

    if (namedFields)
        fieldInitialized = storageAdd(&umka->storage, (*type)->numItems + 1);

    const int size = typeSize(&umka->types, *type);
    const Ident *arrayOrStruct = NULL;

    if (constant)
    {
        constant->ptrVal = storageAdd(&umka->storage, size);
        if (namedFields)
            constZero(constant->ptrVal, size);
    }
    else
    {
        arrayOrStruct = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, *type, false);
        doZeroVar(umka, arrayOrStruct);
    }

    int numItems = 0, itemOffset = 0;
    while (umka->lex.tok.kind != TOK_RBRACE)
    {
        if (!namedFields && numItems > (*type)->numItems - 1)
            umka->error.handler(umka->error.context, "Too many elements in literal");

        // [ident ":"]
        const Field *field = NULL;
        if (namedFields)
        {
            lexCheck(&umka->lex, TOK_IDENT);

            int fieldIndex = 0;
            field = typeAssertFindField(&umka->types, *type, umka->lex.tok.name, &fieldIndex);

            if (field && fieldInitialized[fieldIndex])
                umka->error.handler(umka->error.context, "Duplicate field %s", field->name);

            fieldInitialized[fieldIndex] = true;
            itemOffset = field->offset;

            lexNext(&umka->lex);
            lexEat(&umka->lex, TOK_COLON);
        }
        else if ((*type)->kind == TYPE_STRUCT)
        {
            field = (*type)->field[numItems];
            if (field && identIsHidden(field->name))
                umka->error.handler(umka->error.context, "Cannot initialize hidden field");

            itemOffset = field->offset;
        }

        if (!constant)
            genPushLocalPtr(&umka->gen, arrayOrStruct->offset + itemOffset);

        const Type *expectedItemType = (*type)->kind == TYPE_ARRAY ? (*type)->base : field->type;
        const Type *itemType = expectedItemType;
        Const itemConstantBuf, *itemConstant = constant ? &itemConstantBuf : NULL;
        const int itemSize = typeSize(&umka->types, expectedItemType);

        // expr
        parseExpr(umka, &itemType, itemConstant);
        doAssertImplicitTypeConv(umka, expectedItemType, &itemType, itemConstant);

        if (constant)
            constAssign(&umka->consts, (char *)constant->ptrVal + itemOffset, itemConstant, expectedItemType->kind, itemSize);
        else
        {
            if (doTryRemoveCopyResultToTempVar(umka))
            {
                // Optimization: if the right-hand side is a function call, assume its reference count to be already increased before return
                // The left-hand side will hold this additional reference, so we can remove the temporary "reference holder" variable
                genAssign(&umka->gen, expectedItemType->kind, itemSize);
            }
            else
            {
                // General case: update reference counts for both sides
                genChangeRefCntAssign(&umka->gen, expectedItemType);
            }
        }

        numItems++;
        if ((*type)->kind == TYPE_ARRAY)
            itemOffset += itemSize;

        if (umka->lex.tok.kind != TOK_COMMA)
            break;
        lexNext(&umka->lex);
    }

    if (!namedFields && numItems < (*type)->numItems)
        umka->error.handler(umka->error.context, "Too few elements in literal");

    if (!constant)
        doPushVarPtr(umka, arrayOrStruct);

    // Allow closing brace on a new line
    if (umka->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&umka->lex);

    lexEat(&umka->lex, TOK_RBRACE);
}


// dynArrayLiteral = arrayLiteral.
static void parseDynArrayLiteral(Umka *umka, const Type **type, Const *constant)
{
    if (!(*type)->isVariadicParamList)
        lexEat(&umka->lex, TOK_LBRACE);

    ConstArray constItems;
    if (constant)
        constArrayAlloc(&constItems, &umka->storage, (*type)->base);

    // Dynamic array is first parsed as a static array of unknown length, then converted to a dynamic array
    Type *staticArrayType = typeAdd(&umka->types, &umka->blocks, TYPE_ARRAY);
    staticArrayType->base = (*type)->base;
    const int itemSize = typeSize(&umka->types, staticArrayType->base);

    // Parse array
    const TokenKind rightEndTok = (*type)->isVariadicParamList ? TOK_RPAR : TOK_RBRACE;
    if (umka->lex.tok.kind != rightEndTok)
    {
        while ((*type)->isVariadicParamList || umka->lex.tok.kind != TOK_RBRACE)
        {
            const Type *itemType = staticArrayType->base;

            Const *constItem = NULL;
            if (constant)
            {
                constArrayAppend(&constItems, (Const){0});
                constItem = &constItems.data[staticArrayType->numItems];
            }

            parseExpr(umka, &itemType, constItem);

            // Special case: variadic parameter list's first item is already a dynamic array compatible with the variadic parameter list
            if ((*type)->isVariadicParamList && typeCompatible(*type, itemType) && staticArrayType->numItems == 0)
                return;

            doAssertImplicitTypeConv(umka, staticArrayType->base, &itemType, constItem);

            typeResizeArray(staticArrayType, staticArrayType->numItems + 1);

            if (umka->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&umka->lex);
        }
    }

    if (!(*type)->isVariadicParamList)
    {
        // Allow closing brace on a new line
        if (umka->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
            lexNext(&umka->lex);

        lexEat(&umka->lex, TOK_RBRACE);
    }

    if (constant)
    {
        // Allocate array
        Const constStaticArray = {.ptrVal = storageAdd(&umka->storage, staticArrayType->numItems * itemSize)};

        // Assign items
        for (int i = staticArrayType->numItems - 1; i >= 0; i--)
            constAssign(&umka->consts, (char *)constStaticArray.ptrVal + i * itemSize, &constItems.data[i], staticArrayType->base->kind, itemSize);

        constArrayFree(&constItems);

        *constant = constStaticArray;
    }
    else
    {
        // Allocate array
        const int staticArrayOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, staticArrayType);

        // Assign items
        for (int i = staticArrayType->numItems - 1; i >= 0; i--)
        {
            genPushLocalPtr(&umka->gen, staticArrayOffset + i * itemSize);
            genSwapAssign(&umka->gen, staticArrayType->base->kind, staticArrayType->base->size);
        }

        genPushLocalPtr(&umka->gen, staticArrayOffset);
    }

    // Convert to dynamic array
    doAssertImplicitTypeConv(umka, *type, (const Type **)&staticArrayType, constant);
}


// mapLiteral = "{" [expr ":" expr {"," expr ":" expr} [","]] "}".
static void parseMapLiteral(Umka *umka, const Type **type, Const *constant)
{
    lexEat(&umka->lex, TOK_LBRACE);

    if (constant)
        umka->error.handler(umka->error.context, "Map literals are not allowed for constants");

    // Allocate map
    const Ident *mapIdent = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, *type, false);
    doZeroVar(umka, mapIdent);

    doPushVarPtr(umka, mapIdent);
    genCallTypedBuiltin(&umka->gen, *type, BUILTIN_MAKE);

    // Parse map
    while (umka->lex.tok.kind != TOK_RBRACE)
    {
        genDup(&umka->gen);

        // Key
        const Type *keyType = typeMapKey(*type);
        parseExpr(umka, &keyType, NULL);
        doAssertImplicitTypeConv(umka, typeMapKey(*type), &keyType, NULL);

        lexEat(&umka->lex, TOK_COLON);

        // Get map item by key
        genGetMapPtr(&umka->gen, *type);

        // Item
        const Type *itemType = typeMapItem(*type);
        parseExpr(umka, &itemType, NULL);
        doAssertImplicitTypeConv(umka, typeMapItem(*type), &itemType, NULL);

        // Assign to map item
        if (doTryRemoveCopyResultToTempVar(umka))
        {
            // Optimization: if the right-hand side is a function call, assume its reference count to be already increased before return
            // The left-hand side will hold this additional reference, so we can remove the temporary "reference holder" variable
            genChangeLeftRefCntAssign(&umka->gen, typeMapItem(*type));
        }
        else
        {
            // General case: update reference counts for both sides
            genChangeRefCntAssign(&umka->gen, typeMapItem(*type));
        }

        if (umka->lex.tok.kind != TOK_COMMA)
            break;
        lexNext(&umka->lex);
    }

    // Allow closing brace on a new line
    if (umka->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&umka->lex);

    lexEat(&umka->lex, TOK_RBRACE);
}


// closureLiteral = ["|" ident {"," ident} "|"] fnBlock.
static void parseClosureLiteral(Umka *umka, const Type **type, Const *constant)
{
    if (constant)
    {
        // Allocate closure
        Closure *closure = (Closure *)storageAdd(&umka->storage, typeSize(&umka->types, *type));

        // ["|" ident {"," ident} "|"]
        if (umka->lex.tok.kind == TOK_OR)
            umka->error.handler(umka->error.context, "Cannot capture variables in a constant closure literal");

        // fnBlock
        int beforeEntry = umka->gen.ip;

        if (umka->blocks.top != 0)
            genNop(&umka->gen);                                     // Jump over the nested function block (stub)

        const Field *fn = typeAssertFindField(&umka->types, *type, "#fn", NULL);

        const Const fnConstant = {.intVal = umka->gen.ip};
        Ident *fnConstantIdent = identAddTempConst(&umka->idents, &umka->modules, &umka->blocks, fn->type, fnConstant);
        parseFnBlock(umka, fnConstantIdent, NULL);

        if (umka->blocks.top != 0)
            genGoFromTo(&umka->gen, beforeEntry, umka->gen.ip);     // Jump over the nested function block (fixup)

        // Assign closure function
        closure->entryOffset = fnConstant.intVal;
        constant->ptrVal = closure;
    }
    else
    {
        // Allocate closure
        const Ident *closureIdent = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, *type, false);
        doZeroVar(umka, closureIdent);

        Type *upvaluesStructType = NULL;

        // ["|" ident {"," ident} "|"]
        if (umka->lex.tok.kind == TOK_OR)
        {
            lexNext(&umka->lex);

            // Determine upvalues structure type
            upvaluesStructType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
            while (1)
            {
                lexCheck(&umka->lex, TOK_IDENT);

                const Ident *capturedIdent = identAssertFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, umka->lex.tok.name, NULL);

                if (capturedIdent->kind != IDENT_VAR)
                    umka->error.handler(umka->error.context, "%s is not a variable", capturedIdent->name);

                if (identIsOuterLocalVar(&umka->blocks, capturedIdent))
                    umka->error.handler(umka->error.context, "%s is not specified as a captured variable", capturedIdent->name);

                typeAddField(&umka->types, upvaluesStructType, capturedIdent->type, capturedIdent->name);

                lexNext(&umka->lex);

                if (umka->lex.tok.kind != TOK_COMMA)
                    break;
                lexNext(&umka->lex);
            }

            lexEat(&umka->lex, TOK_OR);

            // Allocate upvalues structure
            const Ident *upvaluesStructIdent = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, upvaluesStructType, false);
            doZeroVar(umka, upvaluesStructIdent);

            // Assign upvalues structure fields
            for (int i = 0; i < upvaluesStructType->numItems; i++)
            {
                const Field *upvalue = upvaluesStructType->field[i];
                const Ident *capturedIdent = identAssertFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, upvalue->name, NULL);

                doPushVarPtr(umka, upvaluesStructIdent);
                genGetFieldPtr(&umka->gen, upvalue->offset);

                doPushVarPtr(umka, capturedIdent);
                genDeref(&umka->gen, capturedIdent->type->kind);

                genChangeRefCntAssign(&umka->gen, upvalue->type);
            }

            // Assign closure upvalues
            const Field *upvalues = typeAssertFindField(&umka->types, closureIdent->type, "#upvalues", NULL);
            const Type *upvaluesType = upvaluesStructIdent->type;

            doPushVarPtr(umka, closureIdent);
            genGetFieldPtr(&umka->gen, upvalues->offset);

            doPushVarPtr(umka, upvaluesStructIdent);
            genDeref(&umka->gen, upvaluesStructIdent->type->kind);
            doAssertImplicitTypeConv(umka, upvalues->type, &upvaluesType, NULL);

            genChangeRefCntAssign(&umka->gen, upvalues->type);
        }

        // fnBlock
        const int beforeEntry = umka->gen.ip;

        genNop(&umka->gen);                                     // Jump over the nested function block (stub)

        const Field *fn = typeAssertFindField(&umka->types, closureIdent->type, "#fn", NULL);

        const Const fnConstant = {.intVal = umka->gen.ip};
        Ident *fnConstantIdent = identAddTempConst(&umka->idents, &umka->modules, &umka->blocks, fn->type, fnConstant);
        parseFnBlock(umka, fnConstantIdent, upvaluesStructType);

        genGoFromTo(&umka->gen, beforeEntry, umka->gen.ip);     // Jump over the nested function block (fixup)

        // Assign closure function
        doPushVarPtr(umka, closureIdent);
        genGetFieldPtr(&umka->gen, fn->offset);

        doPushConst(umka, fn->type, &fnConstant);

        genChangeRefCntAssign(&umka->gen, fn->type);

        doPushVarPtr(umka, closureIdent);
    }
}


// compositeLiteral = [type] (arrayLiteral | dynArrayLiteral | mapLiteral | structLiteral | closureLiteral).
static void parseCompositeLiteral(Umka *umka, const Type **type, Const *constant)
{
    if ((*type)->kind == TYPE_ARRAY || (*type)->kind == TYPE_STRUCT)
        parseArrayOrStructLiteral(umka, type, constant);
    else if ((*type)->kind == TYPE_DYNARRAY)
        parseDynArrayLiteral(umka, type, constant);
    else if ((*type)->kind == TYPE_MAP)
        parseMapLiteral(umka, type, constant);
    else if ((*type)->kind == TYPE_CLOSURE)
        parseClosureLiteral(umka, type, constant);
    else
        umka->error.handler(umka->error.context, "Composite literals are only allowed for arrays, maps, structures and closures");
}


// enumConst = [type] "." ident.
static void parseEnumConst(Umka *umka, const Type **type, Const *constant)
{
    if (!typeEnum(*type))
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        umka->error.handler(umka->error.context, "Type %s is not an enumeration", typeSpelling(*type, typeBuf));
    }

    lexEat(&umka->lex, TOK_PERIOD);
    lexCheck(&umka->lex, TOK_IDENT);

    const EnumConst *enumConst = typeAssertFindEnumConst(&umka->types, *type, umka->lex.tok.name);

    if (constant)
        *constant = enumConst->val;
    else
        doPushConst(umka, *type, &enumConst->val);

    lexNext(&umka->lex);
}


static void parseTypeCastOrCompositeLiteralOrEnumConst(Umka *umka, const Ident *ident, const Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit)
{
    if (*type && (umka->lex.tok.kind == TOK_LBRACE || umka->lex.tok.kind == TOK_OR || umka->lex.tok.kind == TOK_PERIOD))
    {
        // No type to parse - use the inferred type instead, i.e., the type specified as an initial value to the type parameter of parseExpr() or parseExprList()
    }
    else
    {
        *type = parseType(umka, ident);
    }

    if (umka->lex.tok.kind == TOK_LPAR)
    {
        parseTypeCast(umka, type, constant);
        *isCompLit = false;
    }
    else if (umka->lex.tok.kind == TOK_LBRACE || umka->lex.tok.kind == TOK_OR)
    {
        parseCompositeLiteral(umka, type, constant);
        *isCompLit = true;
    }
    else if (umka->lex.tok.kind == TOK_PERIOD)
    {
        parseEnumConst(umka, type, constant);
        *isCompLit = false;
    }
    else
        umka->error.handler(umka->error.context, "Type cast or composite literal or enumeration constant expected");

    *isVar = typeStructured(*type);
    *isCall = false;
}


// derefSelector = "^".
static void parseDerefSelector(Umka *umka, const Type **type, bool *isVar, bool *isCall)
{
    if (*isVar)   // This is always the case, except for type-cast lvalues like ^T(x)^ which are not variables
    {
        if ((*type)->kind != TYPE_PTR)
            umka->error.handler(umka->error.context, "Typed pointer expected");

        genDeref(&umka->gen, (*type)->base->kind);
        *type = (*type)->base;
    }

    if (((*type)->kind != TYPE_PTR && (*type)->kind != TYPE_WEAKPTR) ||
        ((*type)->base->kind == TYPE_VOID || (*type)->base->kind == TYPE_NULL))
    {
        umka->error.handler(umka->error.context, "Typed pointer expected");
    }

    if ((*type)->kind == TYPE_WEAKPTR)
    {
        genStrengthenPtr(&umka->gen);
        *type = typeAddPtrTo(&umka->types, &umka->blocks, (*type)->base);
    }

    lexNext(&umka->lex);
    *isVar = true;
    *isCall = false;
}


// indexSelector = "[" expr "]".
static void parseIndexSelector(Umka *umka, const Type **type, bool *isVar, bool *isCall)
{
    // Implicit dereferencing: a^[i] == a[i]
    doTryImplicitDeref(umka, type);

    // Explicit dereferencing for a string, since it is just a pointer, not a structured type
    if ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_STR)
        genDeref(&umka->gen, TYPE_STR);

    if ((*type)->kind == TYPE_PTR &&
       ((*type)->base->kind == TYPE_ARRAY || (*type)->base->kind == TYPE_DYNARRAY || (*type)->base->kind == TYPE_STR || (*type)->base->kind == TYPE_MAP))
        *type = (*type)->base;

    if ((*type)->kind != TYPE_ARRAY && (*type)->kind != TYPE_DYNARRAY && (*type)->kind != TYPE_STR && (*type)->kind != TYPE_MAP)
        umka->error.handler(umka->error.context, "Array, string or map expected");

    // Index or key
    lexNext(&umka->lex);

    if ((*type)->kind == TYPE_MAP)
    {
        const Type *keyType = typeMapKey(*type);
        parseExpr(umka, &keyType, NULL);
        doAssertImplicitTypeConv(umka, typeMapKey(*type), &keyType, NULL);
    }
    else
    {
        const Type *indexType = umka->intType;
        parseExpr(umka, &indexType, NULL);
        typeAssertCompatible(&umka->types, umka->intType, indexType);
    }

    lexEat(&umka->lex, TOK_RBRACKET);

    const Type *itemType = NULL;
    switch ((*type)->kind)
    {
        case TYPE_ARRAY:
        {
            genGetArrayPtr(&umka->gen, typeSize(&umka->types, (*type)->base), (*type)->numItems);   // Use nominal length for range checking
            itemType = (*type)->base;
            break;
        }
        case TYPE_DYNARRAY:
        {
            genGetDynArrayPtr(&umka->gen);
            itemType = (*type)->base;
            break;
        }
        case TYPE_STR:
        {
            genGetArrayPtr(&umka->gen, typeSize(&umka->types, umka->charType), -1);                 // Use actual length for range checking
            genDeref(&umka->gen, TYPE_CHAR);
            itemType = umka->charType;
            break;
        }
        case TYPE_MAP:
        {
            genGetMapPtr(&umka->gen, *type);
            itemType = typeMapItem(*type);
            break;
        }
        default:
            break;
    }

    if ((*type)->kind == TYPE_STR)
    {
        *type = itemType;
        *isVar = false;
    }
    else
    {
        if (typeStructured(itemType))
            *type = itemType;
        else
            *type = typeAddPtrTo(&umka->types, &umka->blocks, itemType);
        *isVar = true;
    }

    *isCall = false;
}


// fieldSelector = "." ident.
static void parseFieldSelector(Umka *umka, const Type **type, bool *isVar, bool *isCall)
{
    // Implicit dereferencing: a^.x == a.x
    doTryImplicitDeref(umka, type);

    // Search for a method
    if ((*type)->kind == TYPE_PTR)
        *type = (*type)->base;
    else if (!typeStructured(*type))
        umka->error.handler(umka->error.context, "Addressable value expected");

    lexNext(&umka->lex);
    lexCheck(&umka->lex, TOK_IDENT);

    const Type *rcvType = *type;
    int rcvTypeModule = rcvType->typeIdent ? rcvType->typeIdent->module : -1;

    rcvType = typeAddPtrTo(&umka->types, &umka->blocks, rcvType);

    const Ident *method = identFind(&umka->idents, &umka->modules, &umka->blocks, rcvTypeModule, umka->lex.tok.name, rcvType, true);
    if (method)
    {
        // Method
        lexNext(&umka->lex);

        // Save concrete method's receiver to dedicated register and push method's entry point
        genPopReg(&umka->gen, REG_SELF);
        doPushConst(umka, method->type, &method->constant);

        *type = method->type;
        *isVar = false;
        *isCall = false;
    }
    else
    {
        // Field
        if ((*type)->kind != TYPE_STRUCT && (*type)->kind != TYPE_INTERFACE)
        {
            char typeBuf[DEFAULT_STR_LEN + 1];
            umka->error.handler(umka->error.context, "Method %s is not defined for %s", umka->lex.tok.name, typeSpelling(*type, typeBuf));
        }

        const Field *field = typeAssertFindField(&umka->types, *type, umka->lex.tok.name, NULL);
        lexNext(&umka->lex);

        genGetFieldPtr(&umka->gen, field->offset);

        // Save interface method's receiver to dedicated register and push method's entry point
        if (field->type->kind == TYPE_FN && field->type->sig.isMethod && field->type->sig.offsetFromSelf != 0)
        {
            genDup(&umka->gen);
            genGetFieldPtr(&umka->gen, -field->type->sig.offsetFromSelf);
            genDeref(&umka->gen, TYPE_PTR);
            genPopReg(&umka->gen, REG_SELF);
        }

        if (typeStructured(field->type))
            *type = field->type;
        else
            *type = typeAddPtrTo(&umka->types, &umka->blocks, field->type);

        *isVar = true;
        *isCall = false;
    }
}


// callSelector = actualParams.
static void parseCallSelector(Umka *umka, const Type **type, bool *isVar, bool *isCall)
{
    // Implicit dereferencing: f^(x) == f(x)
    doTryImplicitDeref(umka, type);

    if ((*type)->kind == TYPE_PTR && ((*type)->base->kind == TYPE_FN || (*type)->base->kind == TYPE_CLOSURE))
    {
        genDeref(&umka->gen, (*type)->base->kind);
        *type = (*type)->base;
    }

    if ((*type)->kind != TYPE_FN && (*type)->kind != TYPE_CLOSURE)
        umka->error.handler(umka->error.context, "Function or closure expected");

    parseCall(umka, type);

    // Push result
    if ((*type)->kind != TYPE_VOID)
        genPushReg(&umka->gen, REG_RESULT);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    if (typeGarbageCollected(*type))
        doCopyResultToTempVar(umka, *type);

    *isVar = typeStructured(*type);
    *isCall = true;
}


// selectors = {derefSelector | indexSelector | fieldSelector | callSelector}.
static void parseSelectors(Umka *umka, const Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit)
{
    while (umka->lex.tok.kind == TOK_CARET  || umka->lex.tok.kind == TOK_LBRACKET ||
           umka->lex.tok.kind == TOK_PERIOD || umka->lex.tok.kind == TOK_LPAR)
    {
        if (constant)
            umka->error.handler(umka->error.context, "Selector %s is not allowed for constants", lexSpelling(umka->lex.tok.kind));

        *isCompLit = false;

        switch (umka->lex.tok.kind)
        {
            case TOK_CARET:     parseDerefSelector(umka, type, isVar, isCall); break;
            case TOK_LBRACKET:  parseIndexSelector(umka, type, isVar, isCall); break;
            case TOK_PERIOD:    parseFieldSelector(umka, type, isVar, isCall); break;
            case TOK_LPAR:      parseCallSelector (umka, type, isVar, isCall); break;
            default:            break;
        } // switch
    } // while
}


// designator = (primary | typeCast | compositeLiteral | enumConst) selectors.
static void parseDesignator(Umka *umka, const Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit)
{
    const Ident *ident = NULL;
    if (umka->lex.tok.kind == TOK_IDENT && (ident = parseQualIdent(umka)) && ident->kind != IDENT_TYPE)
    {
        parsePrimary(umka, ident, type, constant, isVar, isCall);
        *isCompLit = false;
    }
    else
        parseTypeCastOrCompositeLiteralOrEnumConst(umka, ident, type, constant, isVar, isCall, isCompLit);

    parseSelectors(umka, type, constant, isVar, isCall, isCompLit);

    if (((*type)->kind == TYPE_FN && (*type)->sig.isMethod) ||
        ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_FN && (*type)->base->sig.isMethod))
    {
        umka->error.handler(umka->error.context, "Method must be called");
    }
}


// designatorList = designator {"," designator}.
void parseDesignatorList(Umka *umka, const Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit)
{
    parseDesignator(umka, type, constant, isVar, isCall, isCompLit);

    if (umka->lex.tok.kind == TOK_COMMA && (*isVar) && !(*isCall) && !(*isCompLit))
    {
        // Designator list (types formally encoded as structure field types - not a real structure)
        if (constant)
            umka->error.handler(umka->error.context, "Designator lists are not allowed for constants");

        const Type *fieldType = *type;

        Type *designatorListType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
        designatorListType->isExprList = true;

        while (1)
        {
            typeAddField(&umka->types, designatorListType, fieldType, NULL);

            if (umka->lex.tok.kind != TOK_COMMA)
                break;

            lexNext(&umka->lex);

            bool fieldIsVar, fieldIsCall, fieldIsCompLit;
            parseDesignator(umka, &fieldType, NULL, &fieldIsVar, &fieldIsCall, &fieldIsCompLit);

            if (!fieldIsVar || fieldIsCall || fieldIsCompLit)
                umka->error.handler(umka->error.context, "Inconsistent designator list");
        }

        *type = designatorListType;
    }
}


// factor = designator | intNumber | realNumber | charLiteral | stringLiteral |
//          ("+" | "-" | "!" | "~" ) factor | "&" designator | "(" expr ")".
static void parseFactor(Umka *umka, const Type **type, Const *constant)
{
    switch (umka->lex.tok.kind)
    {
        case TOK_IDENT:
        case TOK_CARET:
        case TOK_WEAK:
        case TOK_LBRACKET:
        case TOK_STR:
        case TOK_ENUM:
        case TOK_MAP:
        case TOK_STRUCT:
        case TOK_INTERFACE:
        case TOK_FN:
        case TOK_LBRACE:
        case TOK_OR:
        case TOK_PERIOD:
        {
            // A designator that isVar is always an addressable quantity (a structured type or a pointer to a value type)
            bool isVar, isCall, isCompLit;
            parseDesignator(umka, type, constant, &isVar, &isCall, &isCompLit);
            if (isVar)
            {
                if (!typeStructured(*type))
                {
                    genDeref(&umka->gen, (*type)->base->kind);
                    *type = (*type)->base;
                }
            }
            break;
        }

        case TOK_INTNUMBER:
        {
            if (umka->lex.tok.uintVal > (uint64_t)INT64_MAX)
            {
                if (constant)
                    constant->uintVal = umka->lex.tok.uintVal;
                else
                    genPushUIntConst(&umka->gen, umka->lex.tok.uintVal);
                *type = umka->uintType;
            }
            else
            {
                if (constant)
                    constant->intVal = umka->lex.tok.intVal;
                else
                    genPushIntConst(&umka->gen, umka->lex.tok.intVal);
                *type = umka->intType;
            }
            lexNext(&umka->lex);
            break;
        }

        case TOK_REALNUMBER:
        {
            if (constant)
                constant->realVal = umka->lex.tok.realVal;
            else
                genPushRealConst(&umka->gen, umka->lex.tok.realVal);
            lexNext(&umka->lex);
            *type = umka->realType;
            break;
        }

        case TOK_CHARLITERAL:
        {
            if (constant)
                constant->uintVal = umka->lex.tok.uintVal;
            else
                genPushIntConst(&umka->gen, umka->lex.tok.intVal);
            lexNext(&umka->lex);
            *type = umka->charType;
            break;
        }

        case TOK_STRLITERAL:
        {
            if (constant)
                constant->ptrVal = umka->lex.tok.strVal;
            else
                genPushGlobalPtr(&umka->gen, umka->lex.tok.strVal);
            lexNext(&umka->lex);

            *type = typeAdd(&umka->types, &umka->blocks, TYPE_STR);
            break;
        }

        case TOK_PLUS:
        case TOK_MINUS:
        case TOK_NOT:
        case TOK_XOR:
        {
            TokenKind op = umka->lex.tok.kind;
            lexNext(&umka->lex);

            parseFactor(umka, type, constant);
            typeAssertValidOperator(&umka->types, *type, op);

            if (constant)
                constUnary(&umka->consts, constant, op, *type);
            else
                genUnary(&umka->gen, op, *type);
            break;
        }

        case TOK_AND:
        {
            if (constant)
                umka->error.handler(umka->error.context, "Address operator is not allowed in constant expressions");

            lexNext(&umka->lex);

            bool isVar, isCall, isCompLit;
            parseDesignator(umka, type, constant, &isVar, &isCall, &isCompLit);

            if (!isVar)
                umka->error.handler(umka->error.context, "Cannot take address");

            if (isCompLit)
                doEscapeToHeap(umka, typeAddPtrTo(&umka->types, &umka->blocks, *type));

            // A value type is already a pointer, a structured type needs to have it added
            if (typeStructured(*type))
                *type = typeAddPtrTo(&umka->types, &umka->blocks, *type);

            break;
        }

        case TOK_LPAR:
        {
            lexEat(&umka->lex, TOK_LPAR);

            *type = NULL;
            parseExpr(umka, type, constant);

            lexEat(&umka->lex, TOK_RPAR);
            break;
        }

        default: umka->error.handler(umka->error.context, "Illegal expression");
    }
}


// term = factor {("*" | "/" | "%" | "<<" | ">>" | "&") factor}.
static void parseTerm(Umka *umka, const Type **type, Const *constant)
{
    parseFactor(umka, type, constant);

    while (umka->lex.tok.kind == TOK_MUL || umka->lex.tok.kind == TOK_DIV || umka->lex.tok.kind == TOK_MOD ||
           umka->lex.tok.kind == TOK_SHL || umka->lex.tok.kind == TOK_SHR || umka->lex.tok.kind == TOK_AND)
    {
        TokenKind op = umka->lex.tok.kind;
        lexNext(&umka->lex);

        Const rightConstantBuf, *rightConstant;
        if (constant)
            rightConstant = &rightConstantBuf;
        else
            rightConstant = NULL;

        const Type *rightType = *type;
        parseFactor(umka, &rightType, rightConstant);
        doApplyOperator(umka, type, &rightType, constant, rightConstant, op, true, true);
    }
}


// relationTerm = term {("+" | "-" | "|" | "^") term}.
static void parseRelationTerm(Umka *umka, const Type **type, Const *constant)
{
    parseTerm(umka, type, constant);

    while (umka->lex.tok.kind == TOK_PLUS || umka->lex.tok.kind == TOK_MINUS ||
           umka->lex.tok.kind == TOK_OR   || umka->lex.tok.kind == TOK_XOR)
    {
        TokenKind op = umka->lex.tok.kind;
        lexNext(&umka->lex);

        Const rightConstantBuf, *rightConstant;
        if (constant)
            rightConstant = &rightConstantBuf;
        else
            rightConstant = NULL;

        const Type *rightType = *type;
        parseTerm(umka, &rightType, rightConstant);
        doApplyOperator(umka, type, &rightType, constant, rightConstant, op, true, true);
    }
}


// relation = relationTerm [("==" | "!=" | "<" | "<=" | ">" | ">=") relationTerm].
static void parseRelation(Umka *umka, const Type **type, Const *constant)
{
    parseRelationTerm(umka, type, constant);

    if (umka->lex.tok.kind == TOK_EQEQ   || umka->lex.tok.kind == TOK_NOTEQ   || umka->lex.tok.kind == TOK_LESS ||
        umka->lex.tok.kind == TOK_LESSEQ || umka->lex.tok.kind == TOK_GREATER || umka->lex.tok.kind == TOK_GREATEREQ)
    {
        TokenKind op = umka->lex.tok.kind;
        lexNext(&umka->lex);

        Const rightConstantBuf, *rightConstant;
        if (constant)
            rightConstant = &rightConstantBuf;
        else
            rightConstant = NULL;

        const Type *rightType = *type;
        parseRelationTerm(umka, &rightType, rightConstant);
        doApplyOperator(umka, type, &rightType, constant, rightConstant, op, true, true);

        *type = umka->boolType;
    }
}


// logicalTerm = relation {"&&" relation}.
static void parseLogicalTerm(Umka *umka, const Type **type, Const *constant)
{
    parseRelation(umka, type, constant);

    while (umka->lex.tok.kind == TOK_ANDAND)
    {
        TokenKind op = umka->lex.tok.kind;
        lexNext(&umka->lex);

        if (constant)
        {
            if (constant->intVal)
            {
                Const rightConstantBuf, *rightConstant = &rightConstantBuf;

                const Type *rightType = *type;
                parseRelation(umka, &rightType, rightConstant);
                doApplyOperator(umka, type, &rightType, constant, rightConstant, op, false, true);
                constant->intVal = rightConstant->intVal;
            }
            else
                constant->intVal = false;
        }
        else
        {
            genShortCircuitProlog(&umka->gen);

            blocksEnter(&umka->blocks);

            const Type *rightType = *type;
            parseRelation(umka, &rightType, NULL);
            doApplyOperator(umka, type, &rightType, NULL, NULL, op, false, true);

            doGarbageCollection(umka);
            identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
            blocksLeave(&umka->blocks);

            genShortCircuitEpilog(&umka->gen, op);
        }
    }
}


// logicalExpr = logicalTerm {"||" logicalTerm}.
static void parseLogicalExpr(Umka *umka, const Type **type, Const *constant)
{
    parseLogicalTerm(umka, type, constant);

    while (umka->lex.tok.kind == TOK_OROR)
    {
        TokenKind op = umka->lex.tok.kind;
        lexNext(&umka->lex);

        if (constant)
        {
            if (!constant->intVal)
            {
                Const rightConstantBuf, *rightConstant = &rightConstantBuf;

                const Type *rightType = *type;
                parseLogicalTerm(umka, &rightType, rightConstant);
                doApplyOperator(umka, type, &rightType, constant, rightConstant, op, false, true);
                constant->intVal = rightConstant->intVal;
            }
            else
                constant->intVal = true;
        }
        else
        {
            genShortCircuitProlog(&umka->gen);

            blocksEnter(&umka->blocks);

            const Type *rightType = *type;
            parseLogicalTerm(umka, &rightType, NULL);
            doApplyOperator(umka, type, &rightType, NULL, NULL, op, false, true);

            doGarbageCollection(umka);
            identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
            blocksLeave(&umka->blocks);

            genShortCircuitEpilog(&umka->gen, op);
        }
    }
}


// expr = logicalExpr ["?" expr ":" expr].
void parseExpr(Umka *umka, const Type **type, Const *constant)
{
    parseLogicalExpr(umka, type, constant);

    // "?"
    if (umka->lex.tok.kind == TOK_QUESTION)
    {
        typeAssertCompatible(&umka->types, umka->boolType, *type);
        lexNext(&umka->lex);

        const Type *leftType = *type, *rightType = *type;

        if (constant)
        {
            Const leftConstantBuf, *leftConstant = &leftConstantBuf;
            parseExpr(umka, &leftType, leftConstant);

            // ":"
            lexEat(&umka->lex, TOK_COLON);

            Const rightConstantBuf, *rightConstant = &rightConstantBuf;
            rightType = leftType;
            parseExpr(umka, &rightType, rightConstant);
            doAssertImplicitTypeConv(umka, leftType, &rightType, rightConstant);

            *constant = constant->intVal ? (*leftConstant) : (*rightConstant);
        }
        else
        {
            genIfCondEpilog(&umka->gen);

            // Left-hand side expression
            blocksEnter(&umka->blocks);

            parseExpr(umka, &leftType, NULL);

            const Ident *result = NULL;
            if (typeGarbageCollected(leftType))
            {
                // Create a temporary result variable in the outer block, so that it could outlive both left- and right-hand side expression blocks
                blocksLeave(&umka->blocks);
                result = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, leftType, false);
                blocksReenter(&umka->blocks);

                // Copy result to temporary variable
                genDup(&umka->gen);
                genChangeRefCnt(&umka->gen, TOK_PLUSPLUS, leftType);
                doPushVarPtr(umka, result);
                genSwapAssign(&umka->gen, result->type->kind, typeSize(&umka->types, result->type));
            }

            doGarbageCollection(umka);
            identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
            blocksLeave(&umka->blocks);

            // ":"
            lexEat(&umka->lex, TOK_COLON);
            genElseProlog(&umka->gen);

            // Right-hand side expression
            blocksEnter(&umka->blocks);

            rightType = leftType;
            parseExpr(umka, &rightType, NULL);
            doAssertImplicitTypeConv(umka, leftType, &rightType, NULL);

            if (typeGarbageCollected(leftType))
            {
                // Copy result to temporary variable
                genDup(&umka->gen);
                genChangeRefCnt(&umka->gen, TOK_PLUSPLUS, leftType);
                doPushVarPtr(umka, result);
                genSwapAssign(&umka->gen, result->type->kind, typeSize(&umka->types, result->type));
            }

            doGarbageCollection(umka);
            identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
            blocksLeave(&umka->blocks);

            genIfElseEpilog(&umka->gen);
        }

        *type = leftType;
    }

    if ((*type)->kind == TYPE_VOID)
        umka->error.handler(umka->error.context, "Void expression is not allowed");
}


// exprList = expr {"," expr}.
void parseExprList(Umka *umka, const Type **type, Const *constant)
{
    const Type *inferredType = *type;
    if (inferredType && typeExprListStruct(inferredType) && inferredType->numItems > 0)
        *type = inferredType->field[0]->type;

    parseExpr(umka, type, constant);

    if (umka->lex.tok.kind == TOK_COMMA)
    {
        // Expression list (syntactic sugar - actually a structure literal)
        Const fieldConstantBuf[MAX_IDENTS_IN_LIST], *fieldConstant = NULL;
        if (constant)
        {
            fieldConstantBuf[0] = *constant;
            fieldConstant = &fieldConstantBuf[0];
        }

        const Type *fieldType = *type;

        Type *exprListType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
        exprListType->isExprList = true;

        // Evaluate expressions and get the total structure size
        while (1)
        {
            // Convert field to the desired type if necessary and possible (no error is thrown anyway)
            if (inferredType && typeExprListStruct(inferredType) && inferredType->numItems > exprListType->numItems)
            {
                const Type *inferredFieldType = inferredType->field[exprListType->numItems]->type;
                doImplicitTypeConv(umka, inferredFieldType, &fieldType, fieldConstant);
                if (typeCompatible(inferredFieldType, fieldType))
                    fieldType = inferredFieldType;
            }

            if (typeExprListStruct(fieldType))
                umka->error.handler(umka->error.context, "Nested expression lists are not allowed");

            if (exprListType->numItems >= MAX_IDENTS_IN_LIST)
                umka->error.handler(umka->error.context, "Too many expressions in list");

            typeAddField(&umka->types, exprListType, fieldType, NULL);

            if (umka->lex.tok.kind != TOK_COMMA)
                break;

            fieldConstant = constant ? &fieldConstantBuf[exprListType->numItems] : NULL;

            lexNext(&umka->lex);

            fieldType = NULL;
            if (inferredType && typeExprListStruct(inferredType) && inferredType->numItems > exprListType->numItems)
                fieldType = inferredType->field[exprListType->numItems]->type;

            parseExpr(umka, &fieldType, fieldConstant);
        }

        *type = exprListType;

        // Allocate structure
        const Ident *exprList = NULL;
        if (constant)
            constant->ptrVal = storageAdd(&umka->storage, typeSize(&umka->types, *type));
        else
        {
            exprList = identAllocTempVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, *type, false);
            doZeroVar(umka, exprList);
        }

        // Assign expressions
        for (int i = (*type)->numItems - 1; i >= 0; i--)
        {
            const Field *field = (*type)->field[i];
            int fieldSize = typeSize(&umka->types, field->type);

            if (constant)
                constAssign(&umka->consts, (char *)constant->ptrVal + field->offset, &fieldConstantBuf[i], field->type->kind, fieldSize);
            else
            {
                genPushLocalPtr(&umka->gen, exprList->offset + field->offset);
                genSwapChangeRefCntAssign(&umka->gen, field->type);
            }
        }

        if (!constant)
            doPushVarPtr(umka, exprList);
    }
}

