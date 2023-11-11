#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "umka_expr.h"
#include "umka_decl.h"
#include "umka_stmt.h"


static void parseDynArrayLiteral(Compiler *comp, Type **type, Const *constant);


void doPushConst(Compiler *comp, Type *type, Const *constant)
{
    if (typeReal(type))
        genPushRealConst(&comp->gen, constant->realVal);
    else
        genPushIntConst(&comp->gen, constant->intVal);
}


void doPushVarPtr(Compiler *comp, Ident *ident)
{
    if (ident->block == 0)
        genPushGlobalPtr(&comp->gen, ident->ptr);
    else
        genPushLocalPtr(&comp->gen, ident->offset);
}


static void doPassParam(Compiler *comp, Type *formalParamType)
{
    // Process non-64-bit ordinal and real types
    if ((typeOrdinal(formalParamType) || typeReal(formalParamType)) && typeSizeNoCheck(formalParamType) < typeSizeNoCheck(comp->intType))
    {
        genAssertRange(&comp->gen, formalParamType->kind);                      // Check overflow
        genCallBuiltin(&comp->gen, formalParamType->kind, BUILTIN_NARROW);      // Convert 64-bit slot to narrower representation
    }

    if (doTryRemoveCopyResultToTempVar(comp))
    {
        // Optimization: if the actual parameter is a function call, assume its reference count to be already increased before return
        // The formal parameter variable will hold this additional reference, so we can remove the temporary "reference holder" variable
    }
    else
    {
        // General case: increase parameter's reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, formalParamType);
    }

    // Copy structured parameter if passed by value
    if (typeStructured(formalParamType))
        genPushStruct(&comp->gen, typeSize(&comp->types, formalParamType));
}


void doCopyResultToTempVar(Compiler *comp, Type *type)
{
    Ident *resultCopy = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, type, true);
    genCopyResultToTempVar(&comp->gen, type, resultCopy->offset);
}


bool doTryRemoveCopyResultToTempVar(Compiler *comp)
{
    if (!comp->idents.lastTempVarForResult)
        return false;

    const int resultCopyOffset = genTryRemoveCopyResultToTempVar(&comp->gen);
    if (resultCopyOffset == 0)
        return false;

    if (resultCopyOffset != comp->idents.lastTempVarForResult->offset)
        comp->error.handler(comp->error.context, "Result copy optimization failed");

    comp->idents.lastTempVarForResult->used = false;
    return true;
}


static void doEscapeToHeap(Compiler *comp, Type *ptrType, bool useRefCnt)
{
    // Allocate heap
    genPushGlobalPtr(&comp->gen, ptrType->base);
    genPushIntConst(&comp->gen, typeSize(&comp->types, ptrType->base));
    genCallBuiltin(&comp->gen, TYPE_PTR, BUILTIN_NEW);
    doCopyResultToTempVar(comp, ptrType);

    // Save heap pointer
    genDup(&comp->gen);
    genPopReg(&comp->gen, VM_REG_COMMON_0);

    // Copy to heap and use heap pointer
    if (useRefCnt)
        genSwapChangeRefCntAssign(&comp->gen, ptrType->base);
    else
        genSwapAssign(&comp->gen, ptrType->base->kind, typeSize(&comp->types, ptrType->base));

    genPushReg(&comp->gen, VM_REG_COMMON_0);
}


static void doOrdinalToOrdinalOrRealToRealConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
    {
        if (typeOverflow(dest->kind, *constant))
            comp->error.handler(comp->error.context, "Overflow of %s", typeKindSpelling(dest->kind));
    }
    else
        genAssertRange(&comp->gen, dest->kind);

    *src = dest;
}


static void doIntToRealConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    BuiltinFunc builtin = lhs ? BUILTIN_REAL_LHS : BUILTIN_REAL;
    if (constant)
        constCallBuiltin(&comp->consts, constant, NULL, (*src)->kind, builtin);
    else
        genCallBuiltin(&comp->gen, (*src)->kind, builtin);

    *src = dest;
}


static void doCharToStrConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    if (constant)
    {
        char *buf = NULL;
        if (constant->intVal)
        {
            buf = storageAddStr(&comp->storage, 1);
            buf[0] = constant->intVal;
            buf[1] = 0;
        }
        else
            buf = storageAddStr(&comp->storage, 0);

        constant->ptrVal = buf;
    }
    else
    {
        if (lhs)
            genSwap(&comp->gen);

        genCallBuiltin(&comp->gen, TYPE_CHAR, BUILTIN_MAKETOSTR);
        doCopyResultToTempVar(comp, dest);

        if (lhs)
            genSwap(&comp->gen);
    }

    *src = dest;
}


static void doDynArrayToStrConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to string is not allowed in constant expressions");

    // fn maketostr(src: char | []char): str

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_MAKETOSTR);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    doCopyResultToTempVar(comp, dest);

    *src = dest;
}


static void doStrToDynArrayConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to dynamic array is not allowed in constant expressions");

    // fn makefromstr(src: str, type: Type): []char

    genPushGlobalPtr(&comp->gen, dest);                                 // Dynamic array type

    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, dest);
    genPushLocalPtr(&comp->gen, resultOffset);                          // Pointer to result (hidden parameter)

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_MAKEFROMSTR);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    doCopyResultToTempVar(comp, dest);

    *src = dest;
}


static void doDynArrayToArrayConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to array is not allowed in constant expressions");

    // fn maketoarr(src: []ItemType, type: Type): [...]ItemType

    genPushGlobalPtr(&comp->gen, dest);                                 // Array type

    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, dest);
    genPushLocalPtr(&comp->gen, resultOffset);                          // Pointer to result (hidden parameter)

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_MAKETOARR);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    doCopyResultToTempVar(comp, dest);

    *src = dest;
}


static void doArrayToDynArrayConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to dynamic array is not allowed in constant expressions");

    // fn makefromarr(src: [...]ItemType, type: Type, len: int): type

    genPushGlobalPtr(&comp->gen, dest);                                 // Dynamic array type
    genPushIntConst(&comp->gen, (*src)->numItems);                      // Dynamic array length

    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, dest);
    genPushLocalPtr(&comp->gen, resultOffset);                          // Pointer to result (hidden parameter)

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_MAKEFROMARR);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    doCopyResultToTempVar(comp, dest);

    *src = dest;
}


static void doDynArrayToDynArrayConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion from dynamic array is not allowed in constant expressions");

    // Get source array length: length = len(srcArray)
    int lenOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, comp->intType);

    genDup(&comp->gen);
    genCallBuiltin(&comp->gen, (*src)->kind, BUILTIN_LEN);
    genPushLocalPtr(&comp->gen, lenOffset);
    genSwapAssign(&comp->gen, TYPE_INT, 0);

    // Allocate destination array: destArray = make(dest, length)
    Ident *destArray = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, dest, false);
    doZeroVar(comp, destArray);

    genPushGlobalPtr(&comp->gen, dest);
    genPushLocal(&comp->gen, TYPE_INT, lenOffset);
    doPushVarPtr(comp, destArray);
    genCallBuiltin(&comp->gen, dest->kind, BUILTIN_MAKE);
    genPop(&comp->gen);

    // Loop initialization: index = length - 1
    int indexOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, comp->intType);

    genPushLocal(&comp->gen, TYPE_INT, lenOffset);
    genPushIntConst(&comp->gen, 1);
    genBinary(&comp->gen, TOK_MINUS, TYPE_INT, 0);
    genPushLocalPtr(&comp->gen, indexOffset);
    genSwapAssign(&comp->gen, TYPE_INT, 0);

    // Loop condition: index >= 0
    genWhileCondProlog(&comp->gen);

    genPushLocal(&comp->gen, TYPE_INT, indexOffset);
    genPushIntConst(&comp->gen, 0);
    genBinary(&comp->gen, TOK_GREATEREQ, TYPE_INT, 0);

    genWhileCondEpilog(&comp->gen);

    // Additional scope embracing temporary variables declaration
    blocksEnter(&comp->blocks, NULL);

    // Loop body: destArray[index] = destItemType(srcArray[index]); index--
    genDup(&comp->gen);
    genPushLocal(&comp->gen, TYPE_INT, indexOffset);
    genGetDynArrayPtr(&comp->gen);
    genDeref(&comp->gen, (*src)->base->kind);

    Type *castType = (*src)->base;
    doExplicitTypeConv(comp, dest->base, &castType, constant, false);

    if (!typeEquivalent(dest->base, castType))
    {
        char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
        comp->error.handler(comp->error.context, "Cannot cast %s to %s", typeSpelling((*src)->base, srcBuf), typeSpelling(dest->base, destBuf));
    }

    doPushVarPtr(comp, destArray);
    genDeref(&comp->gen, dest->kind);
    genPushLocal(&comp->gen, TYPE_INT, indexOffset);
    genGetDynArrayPtr(&comp->gen);
    genSwapChangeRefCntAssign(&comp->gen, dest->base);

    genPushLocalPtr(&comp->gen, indexOffset);
    genUnary(&comp->gen, TOK_MINUSMINUS, TYPE_INT);

    // Additional scope embracing temporary variables declaration
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);

    genWhileEpilog(&comp->gen);

    // Remove srcArray and push destArray
    genPop(&comp->gen);
    doPushVarPtr(comp, destArray);
    genDeref(&comp->gen, dest->kind);

    *src = dest;
}


static void doPtrToInterfaceConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to interface is not allowed in constant expressions");

    int destOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, dest);

    // Assign to __self
    genPushLocalPtr(&comp->gen, destOffset);                                // Push dest.__self pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__self

    // Assign to __selftype (RTTI)
    Field *selfType = typeAssertFindField(&comp->types, dest, "__selftype");

    genPushGlobalPtr(&comp->gen, *src);                                     // Push src type
    genPushLocalPtr(&comp->gen, destOffset + selfType->offset);             // Push dest.__selftype pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__selftype

    // Assign to methods
    for (int i = 2; i < dest->numItems; i++)
    {
        const char *name = dest->field[i]->name;

        Type *rcvType = (*src)->base;
        if (rcvType->kind == TYPE_NULL)
            genPushIntConst(&comp->gen, 0);                                 // Allow assigning null to a non-empty interface
        else
        {
            int rcvTypeModule = rcvType->typeIdent ? rcvType->typeIdent->module : -1;

            Ident *srcMethod = identFind(&comp->idents, &comp->modules, &comp->blocks, rcvTypeModule, name, *src, true);
            if (!srcMethod)
                comp->error.handler(comp->error.context, "Method %s is not implemented", name);

            if (!typeCompatible(dest->field[i]->type, srcMethod->type, false))
                comp->error.handler(comp->error.context, "Method %s has incompatible signature", name);

            genPushIntConst(&comp->gen, srcMethod->offset);                 // Push src value
        }

        genPushLocalPtr(&comp->gen, destOffset + dest->field[i]->offset);   // Push dest.method pointer
        genSwapAssign(&comp->gen, TYPE_FN, 0);                              // Assign to dest.method
    }

    genPushLocalPtr(&comp->gen, destOffset);
    *src = dest;
}


static void doInterfaceToInterfaceConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to interface is not allowed in constant expressions");

    int destOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, dest);

    // Assign to __self
    genDup(&comp->gen);                                                     // Duplicate src pointer
    genDeref(&comp->gen, TYPE_PTR);                                         // Get src.__self value
    genPushLocalPtr(&comp->gen, destOffset);                                // Push dest pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__self (NULL means a dynamic type)

    // Assign to __selftype (RTTI)
    Field *selfType = typeAssertFindField(&comp->types, dest, "__selftype");

    genDup(&comp->gen);                                                     // Duplicate src pointer
    genGetFieldPtr(&comp->gen, selfType->offset);                           // Get src.__selftype pointer
    genDeref(&comp->gen, TYPE_PTR);                                         // Get src.__selftype value
    genPushLocalPtr(&comp->gen, destOffset + selfType->offset);             // Push dest.__selftype pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__selftype

    // Assign to methods
    for (int i = 2; i < dest->numItems; i++)
    {
        const char *name = dest->field[i]->name;
        Field *srcMethod = typeFindField(*src, name);
        if (!srcMethod)
            comp->error.handler(comp->error.context, "Method %s is not implemented", name);

        if (!typeCompatible(dest->field[i]->type, srcMethod->type, false))
            comp->error.handler(comp->error.context, "Method %s has incompatible signature", name);

        genDup(&comp->gen);                                                 // Duplicate src pointer
        genGetFieldPtr(&comp->gen, srcMethod->offset);                      // Get src.method pointer
        genDeref(&comp->gen, TYPE_FN);                                      // Get src.method value (entry point)
        genPushLocalPtr(&comp->gen, destOffset + dest->field[i]->offset);   // Push dest.method pointer
        genSwapAssign(&comp->gen, TYPE_FN, 0);                              // Assign to dest.method
    }

    genPop(&comp->gen);                                                     // Remove src pointer
    genPushLocalPtr(&comp->gen, destOffset);
    *src = dest;
}


static void doValueToInterfaceConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to interface is not allowed in constant expressions");

    *src = typeAddPtrTo(&comp->types, &comp->blocks, *src);
    doEscapeToHeap(comp, *src, true);
    doPtrToInterfaceConv(comp, dest, src, constant);
}


static void doInterfaceToPtrConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion from interface is not allowed in constant expressions");

    genAssertType(&comp->gen, dest);
    *src = dest;
}


static void doInterfaceToValueConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion from interface is not allowed in constant expressions");

    Type *destPtrType = typeAddPtrTo(&comp->types, &comp->blocks, dest);
    genAssertType(&comp->gen, destPtrType);
    genDeref(&comp->gen, dest->kind);
    *src = dest;
}


static void doPtrToWeakPtrConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to weak pointer is not allowed in constant expressions");

    genWeakenPtr(&comp->gen);
    *src = dest;
}


static void doWeakPtrToPtrConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion from weak pointer is not allowed in constant expressions");

    genStrengthenPtr(&comp->gen);
    *src = dest;
}


static void doFnToClosureConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to closure is not allowed in constant expressions");

    int destOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, dest);

    genPushLocalPtr(&comp->gen, destOffset);
    genZero(&comp->gen, typeSize(&comp->types, dest));

    genPushLocalPtr(&comp->gen, destOffset + dest->field[0]->offset);   // Push dest.__fn pointer
    genSwapAssign(&comp->gen, TYPE_FN, 0);                              // Assign to dest.__fn

    genPushLocalPtr(&comp->gen, destOffset);
    *src = dest;
}


void doImplicitTypeConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    // Integer to real
    if (typeReal(dest) && typeInteger(*src))
    {
        doIntToRealConv(comp, dest, src, constant, lhs);
    }

    // Character to string
    else if (dest->kind == TYPE_STR && (*src)->kind == TYPE_CHAR)
    {
        doCharToStrConv(comp, dest, src, constant, lhs);
    }

    // Dynamic array to string
    else if (dest->kind == TYPE_STR && (*src)->kind == TYPE_DYNARRAY && (*src)->base->kind == TYPE_CHAR)
    {
        doDynArrayToStrConv(comp, dest, src, constant, lhs);
    }

    // String to dynamic array
    else if (dest->kind == TYPE_DYNARRAY && dest->base->kind == TYPE_CHAR && (*src)->kind == TYPE_STR)
    {
        doStrToDynArrayConv(comp, dest, src, constant, lhs);
    }

    // Array to dynamic array
    else if (dest->kind == TYPE_DYNARRAY && (*src)->kind == TYPE_ARRAY && typeEquivalent(dest->base, (*src)->base))
    {
        doArrayToDynArrayConv(comp, dest, src, constant);
    }

    // Dynamic array to array
    else if (dest->kind == TYPE_ARRAY && (*src)->kind == TYPE_DYNARRAY && typeEquivalent(dest->base, (*src)->base))
    {
        doDynArrayToArrayConv(comp, dest, src, constant);
    }

    // Concrete to interface or interface to interface
    else if (dest->kind == TYPE_INTERFACE)
    {
        if ((*src)->kind == TYPE_INTERFACE)
        {
            // Interface to interface
            if (!typeEquivalent(dest, *src))
                doInterfaceToInterfaceConv(comp, dest, src, constant);
        }
        else if ((*src)->kind == TYPE_PTR)
        {
            // Pointer to interface
            if ((*src)->base->kind == TYPE_PTR)
                comp->error.handler(comp->error.context, "Pointer base type cannot be a pointer");

            doPtrToInterfaceConv(comp, dest, src, constant);
        }
        else
        {
            // Value to interface
            doValueToInterfaceConv(comp, dest, src, constant);
        }
    }

    // Pointer to weak pointer
    else if (dest->kind == TYPE_WEAKPTR && (*src)->kind == TYPE_PTR && typeEquivalent(dest->base, (*src)->base))
    {
        doPtrToWeakPtrConv(comp, dest, src, constant);
    }

    // Weak pointer to pointer
    else if (dest->kind == TYPE_PTR && (*src)->kind == TYPE_WEAKPTR && typeEquivalent(dest->base, (*src)->base))
    {
        doWeakPtrToPtrConv(comp, dest, src, constant);
    }

    // Function to closure
    else if (dest->kind == TYPE_CLOSURE && (*src)->kind == TYPE_FN && typeEquivalent(dest->field[0]->type, *src))
    {
        doFnToClosureConv(comp, dest, src, constant);
    }
}


void doExplicitTypeConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    doImplicitTypeConv(comp, dest, src, constant, lhs);

    // Type to equivalent type (up to the type identifier)
    if (typeEquivalentExceptIdent(dest, *src))
    {
        *src = dest;
    }

    // Ordinal to ordinal or real to real
    else if ((typeOrdinal(*src) && typeOrdinal(dest)) || (typeReal(*src) && typeReal(dest)))
    {
        doOrdinalToOrdinalOrRealToRealConv(comp, dest, src, constant);
    }

    // Pointer to pointer
    else if (typeCastablePtrs(&comp->types, dest, *src))
    {
        *src = dest;
    }

    // Interface to concrete (type assertion)
    else if ((*src)->kind == TYPE_INTERFACE && dest->kind != TYPE_INTERFACE)
    {
        if (dest->kind == TYPE_PTR)
        {
            // Interface to pointer
            doInterfaceToPtrConv(comp, dest, src, constant);
        }
        else
        {
            // Interface to value
            doInterfaceToValueConv(comp, dest, src, constant);
        }
    }

    // Dynamic array to dynamic array of another base type (covariant arrays)
    else if ((*src)->kind == TYPE_DYNARRAY && dest->kind == TYPE_DYNARRAY)
    {
        doDynArrayToDynArrayConv(comp, dest, src, constant);
    }
}


static void doApplyStrCat(Compiler *comp, Const *constant, Const *rightConstant, TokenKind op)
{
    if (constant)
    {
        if (op == TOK_PLUSEQ)
            comp->error.handler(comp->error.context, "Operator is not allowed in constant expressions");

        int len = getStrDims((char *)constant->ptrVal)->len + getStrDims((char *)rightConstant->ptrVal)->len;
        char *buf = storageAddStr(&comp->storage, len);
        strcpy(buf, (char *)constant->ptrVal);

        constant->ptrVal = buf;
        constBinary(&comp->consts, constant, rightConstant, TOK_PLUS, TYPE_STR);    // "+" only
    }
    else
    {
        genBinary(&comp->gen, op, TYPE_STR, 0);                                     // "+" or "+=" only
        doCopyResultToTempVar(comp, comp->strType);
    }
}


void doApplyOperator(Compiler *comp, Type **type, Type **rightType, Const *constant, Const *rightConstant, TokenKind op, bool apply, bool convertLhs)
{
    // First, the right-hand side type is converted to the left-hand side type
    doImplicitTypeConv(comp, *type, rightType, rightConstant, false);

    // Second, the left-hand side type is converted to the right-hand side type for symmetric operators
    if (convertLhs)
        doImplicitTypeConv(comp, *rightType, type, constant, true);

    typeAssertCompatible(&comp->types, *type, *rightType, true);
    typeAssertValidOperator(&comp->types, *type, op);

    if (apply)
    {
        if ((*type)->kind == TYPE_STR && (op == TOK_PLUS || op == TOK_PLUSEQ))
            doApplyStrCat(comp, constant, rightConstant, op);
        else
        {
            if (constant)
                constBinary(&comp->consts, constant, rightConstant, op, (*type)->kind);
            else
                genBinary(&comp->gen, op, (*type)->kind, typeSize(&comp->types, *type));
        }
    }
}


// qualIdent = [ident "."] ident.
Ident *parseQualIdent(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    Ident *ident = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, comp->lex.tok.name, NULL);

    if (ident->kind == IDENT_MODULE)
    {
        lexNext(&comp->lex);
        lexEat(&comp->lex, TOK_PERIOD);
        lexCheck(&comp->lex, TOK_IDENT);

        ident = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, ident->moduleVal, comp->lex.tok.name, NULL);
    }

    if (identIsOuterLocalVar(&comp->blocks, ident))
        comp->error.handler(comp->error.context, "%s is not specified as a captured variable", ident->name);

    return ident;
}


static void parseBuiltinIOCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // printf() parameters: count, stream, format, value, type
    // scanf() parameters:  count, stream, format, value

    // Count (number of characters for printf(), number of items for scanf())
    genPushIntConst(&comp->gen, 0);

    // Stream (file/string pointer)
    if (builtin == BUILTIN_FPRINTF || builtin == BUILTIN_FSCANF  || builtin == BUILTIN_SSCANF)
    {
        Type *expectedType = (builtin == BUILTIN_FPRINTF || builtin == BUILTIN_FSCANF) ? comp->ptrVoidType : comp->strType;
        parseExpr(comp, type, constant);
        typeAssertCompatible(&comp->types, expectedType, *type, false);
        lexEat(&comp->lex, TOK_COMMA);
    }
    else
        genPushGlobalPtr(&comp->gen, NULL);

    // Format string
    parseExpr(comp, type, constant);
    typeAssertCompatible(&comp->types, comp->strType, *type, false);

    // Values, if any
    while (comp->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&comp->lex);
        parseExpr(comp, type, constant);

        if (builtin == BUILTIN_PRINTF || builtin == BUILTIN_FPRINTF || builtin == BUILTIN_SPRINTF)
        {
            typeAssertCompatibleBuiltin(&comp->types, *type, builtin, (*type)->kind != TYPE_VOID);
            genPushGlobalPtr(&comp->gen, *type);                    // Push type
            genCallBuiltin(&comp->gen, (*type)->kind, builtin);
            genPop(&comp->gen);                                     // Remove type
        }
        else  // BUILTIN_SCANF, BUILTIN_FSCANF, BUILTIN_SSCANF
        {
            typeAssertCompatibleBuiltin(&comp->types, *type, builtin, (*type)->kind == TYPE_PTR && (typeOrdinal((*type)->base) || typeReal((*type)->base) || (*type)->base->kind == TYPE_STR));
            genCallBuiltin(&comp->gen, (*type)->base->kind, builtin);
        }
        genPop(&comp->gen); // Remove parameter

    } // while

    // The rest of format string
    genPushIntConst(&comp->gen, 0);
    if (builtin == BUILTIN_PRINTF || builtin == BUILTIN_FPRINTF || builtin == BUILTIN_SPRINTF)
    {
        genPushGlobalPtr(&comp->gen, comp->voidType);           // Push type
        genCallBuiltin(&comp->gen, TYPE_VOID, builtin);
        genPop(&comp->gen);                                     // Remove type
    }
    else
        genCallBuiltin(&comp->gen, TYPE_VOID, builtin);

    genPop(&comp->gen);  // Remove parameter

    genPop(&comp->gen);  // Remove format string

    // Result
    if (builtin == BUILTIN_SPRINTF)
    {
        genSwap(&comp->gen);                    // Swap stream and count
        genPop(&comp->gen);                     // Remove count, keep stream
        *type = comp->strType;
    }
    else
    {
        genPop(&comp->gen);                     // Remove stream, keep count
        *type = comp->intType;
    }
}


static void parseBuiltinMathCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    parseExpr(comp, type, constant);
    doImplicitTypeConv(comp, comp->realType, type, constant, false);
    typeAssertCompatible(&comp->types, comp->realType, *type, false);

    Const constant2Val = {.realVal = 0};
    Const *constant2 = NULL;

    // fn atan2(y, x: real): real
    if (builtin == BUILTIN_ATAN2)
    {
        lexEat(&comp->lex, TOK_COMMA);

        Type *type2;
        if (constant)
            constant2 = &constant2Val;

        parseExpr(comp, &type2, constant2);
        doImplicitTypeConv(comp, comp->realType, &type2, constant2, false);
        typeAssertCompatible(&comp->types, comp->realType, type2, false);
    }

    if (constant)
        constCallBuiltin(&comp->consts, constant, constant2, TYPE_REAL, builtin);
    else
        genCallBuiltin(&comp->gen, TYPE_REAL, builtin);

    if (builtin == BUILTIN_ROUND || builtin == BUILTIN_TRUNC || builtin == BUILTIN_CEIL || builtin == BUILTIN_FLOOR)
        *type = comp->intType;
    else
        *type = comp->realType;
}


// fn new(type: Type, size: int [, expr: type]): ^type
static void parseBuiltinNewCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Type
    *type = parseType(comp, NULL);
    int size = typeSize(&comp->types, *type);

    genPushGlobalPtr(&comp->gen, *type);
    genPushIntConst(&comp->gen, size);
    genCallBuiltin(&comp->gen, TYPE_PTR, BUILTIN_NEW);

    // Initializer expression
    if (comp->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&comp->lex);
        genDup(&comp->gen);

        Type *exprType = NULL;
        parseExpr(comp, &exprType, NULL);
        doImplicitTypeConv(comp, *type, &exprType, NULL, false);
        typeAssertCompatible(&comp->types, *type, exprType, false);

        genChangeRefCntAssign(&comp->gen, *type);
    }

    *type = typeAddPtrTo(&comp->types, &comp->blocks, *type);
}


// fn make(type: Type [, len: int]): type
static void parseBuiltinMakeCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array type
    *type = parseType(comp, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_MAKE, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP);

    genPushGlobalPtr(&comp->gen, *type);

    if ((*type)->kind == TYPE_DYNARRAY)
    {
        // Dynamic array length
        lexEat(&comp->lex, TOK_COMMA);

        Type *lenType;
        parseExpr(comp, &lenType, NULL);
        typeAssertCompatible(&comp->types, comp->intType, lenType, false);
    }
    else // TYPE_MAP
        genPushIntConst(&comp->gen, 0);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_MAKE);
}


// fn copy(array: [] type): [] type
// fn copy(m: map [keyType] type): map [keyType] type
static void parseBuiltinCopyCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_COPY, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_COPY);
}


// fn append(array: [] type, item: (^type | [] type), single: bool, type: Type): [] type
static void parseBuiltinAppendCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_APPEND, (*type)->kind == TYPE_DYNARRAY);

    lexEat(&comp->lex, TOK_COMMA);

    // New item (must always be a pointer, even for value types) or right-hand side dynamic array
    Type *itemType;
    parseExpr(comp, &itemType, NULL);

    bool singleItem = true;
    if (typeEquivalent(*type, itemType))
        singleItem = false;
    else if (itemType->kind == TYPE_ARRAY && typeEquivalent((*type)->base, itemType->base))
    {
        doImplicitTypeConv(comp, *type, &itemType, NULL, false);
        singleItem = false;
    }

    if (singleItem)
    {
        doImplicitTypeConv(comp, (*type)->base, &itemType, NULL, false);
        typeAssertCompatible(&comp->types, (*type)->base, itemType, false);

        if (!typeStructured(itemType))
        {
            // Assignment to an anonymous stack area does not require updating reference counts
            int itemOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, itemType);
            genPushLocalPtr(&comp->gen, itemOffset);
            genSwapAssign(&comp->gen, itemType->kind, 0);

            genPushLocalPtr(&comp->gen, itemOffset);
        }
    }

    // 'Append single item' flag (hidden parameter)
    genPushIntConst(&comp->gen, singleItem);

    // Dynamic array type (hidden parameter)
    genPushGlobalPtr(&comp->gen, *type);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_APPEND);
}


// fn insert(array: [] type, index: int, item: type, type: Type): [] type
static void parseBuiltinInsertCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_INSERT, (*type)->kind == TYPE_DYNARRAY);

    // New item index
    lexEat(&comp->lex, TOK_COMMA);

    Type *indexType;
    parseExpr(comp, &indexType, NULL);
    doImplicitTypeConv(comp, comp->intType, &indexType, NULL, false);
    typeAssertCompatible(&comp->types, comp->intType, indexType, false);

    // New item (must always be a pointer, even for value types)
    lexEat(&comp->lex, TOK_COMMA);

    Type *itemType;
    parseExpr(comp, &itemType, NULL);
    doImplicitTypeConv(comp, (*type)->base, &itemType, NULL, false);
    typeAssertCompatible(&comp->types, (*type)->base, itemType, false);

    if (!typeStructured(itemType))
    {
        // Assignment to an anonymous stack area does not require updating reference counts
        int itemOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, itemType);
        genPushLocalPtr(&comp->gen, itemOffset);
        genSwapAssign(&comp->gen, itemType->kind, 0);

        genPushLocalPtr(&comp->gen, itemOffset);
    }

    // Dynamic array type (hidden parameter)
    genPushGlobalPtr(&comp->gen, *type);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_INSERT);
}


// fn delete(array: [] type, index: int): [] type
// fn delete(m: map [keyType] type, key: keyType): map [keyType] type
static void parseBuiltinDeleteCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array or map
    parseExpr(comp, type, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_DELETE, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP);

    // Item index or map key
    lexEat(&comp->lex, TOK_COMMA);
    Type *indexType;
    Type *expectedIndexType = ((*type)->kind == TYPE_DYNARRAY) ? comp->intType : typeMapKey(*type);
    parseExpr(comp, &indexType, NULL);

    doImplicitTypeConv(comp, expectedIndexType, &indexType, NULL, false);
    typeAssertCompatible(&comp->types, expectedIndexType, indexType, false);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_DELETE);
}


// fn slice(array: [] type | str, startIndex [, endIndex]: int, type: Type): [] type | str
static void parseBuiltinSliceCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array or string
    parseExpr(comp, type, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_SLICE, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_STR);

    lexEat(&comp->lex, TOK_COMMA);

    Type *indexType;

    // Start index
    parseExpr(comp, &indexType, NULL);
    doImplicitTypeConv(comp, comp->intType, &indexType, NULL, false);
    typeAssertCompatible(&comp->types, comp->intType, indexType, false);

    if (comp->lex.tok.kind == TOK_COMMA)
    {
        // Optional end index
        lexNext(&comp->lex);
        parseExpr(comp, &indexType, NULL);
        doImplicitTypeConv(comp, comp->intType, &indexType, NULL, false);
        typeAssertCompatible(&comp->types, comp->intType, indexType, false);
    }
    else
        genPushIntConst(&comp->gen, INT_MIN);

    // Dynamic array or string type (hidden parameter)
    genPushGlobalPtr(&comp->gen, *type);

    if ((*type)->kind == TYPE_DYNARRAY)
    {
        // Pointer to result (hidden parameter)
        int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
        genPushLocalPtr(&comp->gen, resultOffset);
    }
    else
        genPushGlobalPtr(&comp->gen, NULL);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_SLICE);
}


static void parseBuiltinLenCall(Compiler *comp, Type **type, Const *constant)
{
    parseExpr(comp, type, constant);

    switch ((*type)->kind)
    {
        case TYPE_ARRAY:
        {
            if (constant)
                constant->intVal = (*type)->numItems;
            else
            {
                genPop(&comp->gen);
                genPushIntConst(&comp->gen, (*type)->numItems);
            }
            break;
        }
        case TYPE_DYNARRAY:
        {
            if (constant)
                comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

            genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_LEN);
            break;
        }
        case TYPE_STR:
        {
            if (constant)
                constCallBuiltin(&comp->consts, constant, NULL, TYPE_STR, BUILTIN_LEN);
            else
                genCallBuiltin(&comp->gen, TYPE_STR, BUILTIN_LEN);
            break;
        }
        case TYPE_MAP:
        {
            if (constant)
                comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

            genCallBuiltin(&comp->gen, TYPE_MAP, BUILTIN_LEN);
            break;
        }
        default:
        {
            typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_LEN, false);
            return;
        }
    }

    *type = comp->intType;
}


static void parseBuiltinCapCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_CAP, (*type)->kind == TYPE_DYNARRAY);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_CAP);
    *type = comp->intType;
}


// fn sizeof(T | a: T): int
static void parseBuiltinSizeofCall(Compiler *comp, Type **type, Const *constant)
{
    *type = NULL;

    // sizeof(T)
    if (comp->lex.tok.kind == TOK_IDENT)
    {
        Ident *ident = identFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, comp->lex.tok.name, NULL, false);
        if (ident && ident->kind == IDENT_TYPE)
        {
            Lexer lookaheadLex = comp->lex;
            lexNext(&lookaheadLex);
            if (lookaheadLex.tok.kind == TOK_RPAR)
            {
                lexNext(&comp->lex);
                *type = ident->type;
                ident->used = true;
            }
        }
    }

    // sizeof(a: T)
    if (!(*type))
    {
        parseExpr(comp, type, constant);
        if ((*type)->kind != TYPE_VOID)
            genPop(&comp->gen);
    }

    int size = typeSize(&comp->types, *type);

    if (constant)
        constant->intVal = size;
    else
        genPushIntConst(&comp->gen, size);

    *type = comp->intType;
}


static void parseBuiltinSizeofselfCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_SIZEOFSELF, (*type)->kind == TYPE_INTERFACE);

    genCallBuiltin(&comp->gen, TYPE_INTERFACE, BUILTIN_SIZEOFSELF);
    *type = comp->intType;
}


static void parseBuiltinSelfhasptrCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_SELFHASPTR, (*type)->kind == TYPE_INTERFACE);

    genCallBuiltin(&comp->gen, TYPE_INTERFACE, BUILTIN_SELFHASPTR);
    *type = comp->boolType;
}


static void parseBuiltinSelftypeeqCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Left interface
    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_SELFTYPEEQ, (*type)->kind == TYPE_INTERFACE);

    lexEat(&comp->lex, TOK_COMMA);

    // Right interface
    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_SELFTYPEEQ, (*type)->kind == TYPE_INTERFACE);

    genCallBuiltin(&comp->gen, TYPE_INTERFACE, BUILTIN_SELFTYPEEQ);
    *type = comp->boolType;
}


// fn typeptr(T): ^void
static void parseBuiltinTypeptrCall(Compiler *comp, Type **type, Const *constant)
{
    *type = parseType(comp, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_TYPEPTR, (*type)->kind != TYPE_VOID && (*type)->kind != TYPE_NULL);

    if (constant)
        constant->ptrVal = *type;
    else
        genPushGlobalPtr(&comp->gen, *type);

    *type = comp->ptrVoidType;
}


static void parseBuiltinValidCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_VALID, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP || (*type)->kind == TYPE_INTERFACE || (*type)->kind == TYPE_FN || (*type)->kind == TYPE_CLOSURE || (*type)->kind == TYPE_FIBER);

    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_VALID);
    *type = comp->boolType;
}


// fn validkey(m: map [keyType] type, key: keyType): bool
static void parseBuiltinValidkeyCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Map
    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_VALIDKEY, (*type)->kind == TYPE_MAP);

    lexEat(&comp->lex, TOK_COMMA);

    // Map key
    Type *keyType = NULL;
    parseExprOrUntypedLiteral(comp, &keyType, typeMapKey(*type), constant);
    doImplicitTypeConv(comp, typeMapKey(*type), &keyType, NULL, false);
    typeAssertCompatible(&comp->types, typeMapKey(*type), keyType, false);

    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_VALIDKEY);
    *type = comp->boolType;
}


// fn keys(m: map [keyType] type): []keyType
static void parseBuiltinKeysCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Map
    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_KEYS, (*type)->kind == TYPE_MAP);

    // Result type (hidden parameter)
    Type *keysType = typeAdd(&comp->types, &comp->blocks, TYPE_DYNARRAY);
    keysType->base = typeMapKey(*type);
    genPushGlobalPtr(&comp->gen, keysType);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, keysType);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_KEYS);
    *type = keysType;
}


// type FiberFunc = fn(parent: fiber, anyParam: ^type)
// fn fiberspawn(childFunc: FiberFunc, anyParam: ^type): fiber
// fn fibercall(child: fiber)
// fn fiberalive(child: fiber)
static void parseBuiltinFiberCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    if (builtin == BUILTIN_FIBERSPAWN)
    {
        // Parent fiber pointer
        Type *fiberFuncType = NULL;

        parseExpr(comp, &fiberFuncType, constant);
        typeAssertCompatibleBuiltin(&comp->types, fiberFuncType, builtin, typeFiberFunc(fiberFuncType));

        lexEat(&comp->lex, TOK_COMMA);

        // Arbitrary pointer parameter
        Type *anyParamType = NULL;
        Type *expectedAnyParamType = fiberFuncType->sig.param[2]->type;

        parseExpr(comp, &anyParamType, constant);
        doImplicitTypeConv(comp, expectedAnyParamType, &anyParamType, constant, false);
        typeAssertCompatible(&comp->types, expectedAnyParamType, anyParamType, false);

        // Increase parameter's reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, expectedAnyParamType);

        *type = comp->fiberType;
    }
    else    // BUILTIN_FIBERCALL, BUILTIN_FIBERALIVE
    {
        parseExpr(comp, type, constant);
        doImplicitTypeConv(comp, comp->fiberType, type, constant, false);
        typeAssertCompatible(&comp->types, comp->fiberType, *type, false);

        if (builtin == BUILTIN_FIBERALIVE)
            *type = comp->boolType;
        else
            *type = comp->voidType;
    }

    genCallBuiltin(&comp->gen, TYPE_NONE, builtin);
}


static void parseBuiltinExitCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    genCallBuiltin(&comp->gen, TYPE_VOID, BUILTIN_EXIT);
    *type = comp->voidType;
}


static void parseBuiltinErrorCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    doImplicitTypeConv(comp, comp->strType, type, constant, false);
    typeAssertCompatible(&comp->types, comp->strType, *type, false);

    genCallBuiltin(&comp->gen, TYPE_VOID, BUILTIN_ERROR);
    *type = comp->voidType;
}


// builtinCall = qualIdent "(" [expr {"," expr}] ")".
static void parseBuiltinCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    lexEat(&comp->lex, TOK_LPAR);

    switch (builtin)
    {
        // I/O
        case BUILTIN_PRINTF:
        case BUILTIN_FPRINTF:
        case BUILTIN_SPRINTF:
        case BUILTIN_SCANF:
        case BUILTIN_FSCANF:
        case BUILTIN_SSCANF:        parseBuiltinIOCall(comp, type, constant, builtin);      break;

        // Math
        case BUILTIN_ROUND:
        case BUILTIN_TRUNC:
        case BUILTIN_CEIL:
        case BUILTIN_FLOOR:
        case BUILTIN_FABS:
        case BUILTIN_SQRT:
        case BUILTIN_SIN:
        case BUILTIN_COS:
        case BUILTIN_ATAN:
        case BUILTIN_ATAN2:
        case BUILTIN_EXP:
        case BUILTIN_LOG:           parseBuiltinMathCall(comp, type, constant, builtin);    break;

        // Memory
        case BUILTIN_NEW:           parseBuiltinNewCall(comp, type, constant);              break;
        case BUILTIN_MAKE:          parseBuiltinMakeCall(comp, type, constant);             break;
        case BUILTIN_COPY:          parseBuiltinCopyCall(comp, type, constant);             break;
        case BUILTIN_APPEND:        parseBuiltinAppendCall(comp, type, constant);           break;
        case BUILTIN_INSERT:        parseBuiltinInsertCall(comp, type, constant);           break;
        case BUILTIN_DELETE:        parseBuiltinDeleteCall(comp, type, constant);           break;
        case BUILTIN_SLICE:         parseBuiltinSliceCall(comp, type, constant);            break;
        case BUILTIN_LEN:           parseBuiltinLenCall(comp, type, constant);              break;
        case BUILTIN_CAP:           parseBuiltinCapCall(comp, type, constant);              break;
        case BUILTIN_SIZEOF:        parseBuiltinSizeofCall(comp, type, constant);           break;
        case BUILTIN_SIZEOFSELF:    parseBuiltinSizeofselfCall(comp, type, constant);       break;
        case BUILTIN_SELFHASPTR:    parseBuiltinSelfhasptrCall(comp, type, constant);       break;
        case BUILTIN_SELFTYPEEQ:    parseBuiltinSelftypeeqCall(comp, type, constant);       break;
        case BUILTIN_TYPEPTR:       parseBuiltinTypeptrCall(comp, type, constant);          break;
        case BUILTIN_VALID:         parseBuiltinValidCall(comp, type, constant);            break;

        // Maps
        case BUILTIN_VALIDKEY:      parseBuiltinValidkeyCall(comp, type, constant);         break;
        case BUILTIN_KEYS:          parseBuiltinKeysCall(comp, type, constant);             break;

        // Fibers
        case BUILTIN_FIBERSPAWN:
        case BUILTIN_FIBERCALL:
        case BUILTIN_FIBERALIVE:    parseBuiltinFiberCall(comp, type, constant, builtin);   break;

        // Misc
        case BUILTIN_EXIT:          parseBuiltinExitCall(comp, type, constant);             break;
        case BUILTIN_ERROR:         parseBuiltinErrorCall(comp, type, constant);            break;

        default: comp->error.handler(comp->error.context, "Illegal built-in function");
    }

    // Allow closing parenthesis on a new line
    if (comp->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&comp->lex);

    lexEat(&comp->lex, TOK_RPAR);
}


// actualParams = "(" [exprOrLit {"," exprOrLit}] ")".
static void parseCall(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LPAR);

    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Decide whether a (default) indirect call can be replaced with a direct call
    int immediateEntryPoint = (*type)->kind == TYPE_FN ? genTryRemoveImmediateEntryPoint(&comp->gen) : -1;

    // Actual parameters: [__self,] param1, param2 ...[__result]
    int numExplicitParams = 0, numPreHiddenParams = 0, numPostHiddenParams = 0;
    int i = 0;

    if ((*type)->kind == TYPE_CLOSURE)
    {
        // Closure upvalue
        Field *fn = typeAssertFindField(&comp->types, *type, "__fn");
        *type = fn->type;

        genPushUpvalue(&comp->gen);
        doPassParam(comp, (*type)->sig.param[0]->type);

        numPreHiddenParams++;
        i++;
    }
    else if ((*type)->sig.isMethod)
    {
        // Method receiver
        genPushReg(&comp->gen, VM_REG_SELF);

        // Increase receiver's reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, (*type)->sig.param[0]->type);

        numPreHiddenParams++;
        i++;
    }
    else
    {
        // Dummy upvalue
        genPushZero(&comp->gen, sizeof(Interface) / sizeof(Slot));

        numPreHiddenParams++;
        i++;
    }

    // __result
    if (typeStructured((*type)->sig.resultType))
        numPostHiddenParams++;

    if (comp->lex.tok.kind != TOK_RPAR)
    {
        while (1)
        {
            if (numPreHiddenParams + numExplicitParams + numPostHiddenParams > (*type)->sig.numParams - 1)
            {
                char fnTypeBuf[DEFAULT_STR_LEN + 1];
                comp->error.handler(comp->error.context, "Too many actual parameters to %s", typeSpelling(*type, fnTypeBuf));
            }

            Type *formalParamType = (*type)->sig.param[i]->type;
            Type *actualParamType;

            if (formalParamType->isVariadicParamList)
            {
                // Variadic parameter list
                parseDynArrayLiteral(comp, &formalParamType, constant);
                actualParamType = formalParamType;
            }
            else
            {
                // Regular parameter
                parseExprOrUntypedLiteral(comp, &actualParamType, formalParamType, constant);

                doImplicitTypeConv(comp, formalParamType, &actualParamType, constant, false);
                typeAssertCompatibleParam(&comp->types, formalParamType, actualParamType, *type, numExplicitParams + 1);
            }

            doPassParam(comp, formalParamType);
            numExplicitParams++;
            i++;

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }

    int numDefaultOrVariadicFormalParams = 0;

    if ((*type)->sig.numDefaultParams > 0)
        numDefaultOrVariadicFormalParams = (*type)->sig.numDefaultParams;
    else if ((*type)->sig.numParams > 0 && (*type)->sig.param[(*type)->sig.numParams - 1]->type->isVariadicParamList)
        numDefaultOrVariadicFormalParams = 1;

    if (numPreHiddenParams + numExplicitParams + numPostHiddenParams < (*type)->sig.numParams - numDefaultOrVariadicFormalParams)
    {
        char fnTypeBuf[DEFAULT_STR_LEN + 1];
        comp->error.handler(comp->error.context, "Too few actual parameters to %s", typeSpelling(*type, fnTypeBuf));
    }

    // Push default or variadic parameters, if not specified explicitly
    while (i + numPostHiddenParams < (*type)->sig.numParams)
    {
        Type *formalParamType = (*type)->sig.param[i]->type;

        if ((*type)->sig.numDefaultParams > 0)
            doPushConst(comp, formalParamType, &((*type)->sig.param[i]->defaultVal));   // Default parameter
        else
            parseDynArrayLiteral(comp, &formalParamType, constant);                     // Variadic parameter (empty dynamic array)

        doPassParam(comp, formalParamType);
        i++;
    }

    // Push __result pointer
    if (typeStructured((*type)->sig.resultType))
    {
        int offset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, (*type)->sig.resultType);
        genPushLocalPtr(&comp->gen, offset);
        i++;
    }

    if (immediateEntryPoint > 0)
        genCall(&comp->gen, immediateEntryPoint);                                           // Direct call
    else if (immediateEntryPoint < 0)
    {
        int paramSlots = typeParamSizeTotal(&comp->types, &(*type)->sig) / sizeof(Slot);
        genCallIndirect(&comp->gen, paramSlots);                                            // Indirect call
        genPop(&comp->gen);                                                                 // Pop entry point
    }
    else
        comp->error.handler(comp->error.context, "Called function is not defined");

    *type = (*type)->sig.resultType;

    // Allow closing parenthesis on a new line
    if (comp->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&comp->lex);

    lexEat(&comp->lex, TOK_RPAR);
}


// primary = qualIdent | builtinCall.
static void parsePrimary(Compiler *comp, Ident *ident, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    switch (ident->kind)
    {
        case IDENT_CONST:
        {
            if (constant)
                *constant = ident->constant;
            else
                doPushConst(comp, ident->type, &ident->constant);

            *type = ident->type;
            *isVar = false;
            *isCall = false;
            lexNext(&comp->lex);
            break;
        }

        case IDENT_VAR:
        {
            if (constant)
                comp->error.handler(comp->error.context, "Constant expected but variable %s found", ident->name);

            doPushVarPtr(comp, ident);

            if (typeStructured(ident->type))
                *type = ident->type;
            else
                *type = typeAddPtrTo(&comp->types, &comp->blocks, ident->type);
            *isVar = true;
            *isCall = false;
            lexNext(&comp->lex);
            break;
        }

        // Built-in function call
        case IDENT_BUILTIN_FN:
        {
            lexNext(&comp->lex);
            parseBuiltinCall(comp, type, constant, ident->builtin);

            // Copy result to a temporary local variable to collect it as garbage when leaving the block
            if (typeGarbageCollected(*type) && ident->builtin != BUILTIN_TYPEPTR)
                doCopyResultToTempVar(comp, *type);

            *isVar = false;
            *isCall = true;
            break;
        }

        default: comp->error.handler(comp->error.context, "Unexpected identifier %s", ident->name);
    }
}


// typeCast = type "(" expr ")".
static void parseTypeCast(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LPAR);

    Type *originalType;
    parseExpr(comp, &originalType, constant);

    Type *castType = originalType;
    doExplicitTypeConv(comp, *type, &castType, constant, false);

    if (!typeEquivalent(*type, castType))
    {
        char srcBuf[DEFAULT_STR_LEN + 1], destBuf[DEFAULT_STR_LEN + 1];
        comp->error.handler(comp->error.context, "Cannot cast %s to %s", typeSpelling(originalType, srcBuf), typeSpelling(*type, destBuf));
    }

    lexEat(&comp->lex, TOK_RPAR);
}


// arrayLiteral     = "{" [exprOrLit {"," exprOrLit}] "}".
// structLiteral    = "{" [[ident ":"] exprOrLit {"," [ident ":"] exprOrLit}] "}".
static void parseArrayOrStructLiteral(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LBRACE);

    bool namedFields = false;
    if ((*type)->kind == TYPE_STRUCT)
    {
        if (comp->lex.tok.kind == TOK_RBRACE)
            namedFields = true;
        else if (comp->lex.tok.kind == TOK_IDENT)
        {
            Lexer lookaheadLex = comp->lex;
            lexNext(&lookaheadLex);
            namedFields = lookaheadLex.tok.kind == TOK_COLON;
        }
    }

    const int size = typeSize(&comp->types, *type);
    Ident *arrayOrStruct = NULL;

    if (constant)
    {
        constant->ptrVal = storageAdd(&comp->storage, size);
        if (namedFields)
            constZero(constant->ptrVal, size);
    }
    else
    {
        arrayOrStruct = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, *type, false);
        doZeroVar(comp, arrayOrStruct);
    }

    int numItems = 0, itemOffset = 0;
    if (comp->lex.tok.kind != TOK_RBRACE)
    {
        while (1)
        {
            if (!namedFields && numItems > (*type)->numItems - 1)
                comp->error.handler(comp->error.context, "Too many elements in literal");

            // [ident ":"]
            Field *field = NULL;
            if (namedFields)
            {
                lexCheck(&comp->lex, TOK_IDENT);
                field = typeAssertFindField(&comp->types, *type, comp->lex.tok.name);
                itemOffset = field->offset;

                lexNext(&comp->lex);
                lexEat(&comp->lex, TOK_COLON);
            }
            else if ((*type)->kind == TYPE_STRUCT)
            {
                field = (*type)->field[numItems];
                itemOffset = field->offset;
            }

            if (!constant)
                genPushLocalPtr(&comp->gen, arrayOrStruct->offset + itemOffset);

            Type *expectedItemType = (*type)->kind == TYPE_ARRAY ? (*type)->base : field->type;
            Type *itemType;
            Const itemConstantBuf, *itemConstant = constant ? &itemConstantBuf : NULL;
            int itemSize = typeSize(&comp->types, expectedItemType);

            // exprOrLit
            parseExprOrUntypedLiteral(comp, &itemType, expectedItemType, itemConstant);

            doImplicitTypeConv(comp, expectedItemType, &itemType, itemConstant, false);
            typeAssertCompatible(&comp->types, expectedItemType, itemType, false);

            if (constant)
                constAssign(&comp->consts, (char *)constant->ptrVal + itemOffset, itemConstant, expectedItemType->kind, itemSize);
            else
                genChangeRefCntAssign(&comp->gen, expectedItemType);

            numItems++;
            if ((*type)->kind == TYPE_ARRAY)
                itemOffset += itemSize;

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }
    if (!namedFields && numItems < (*type)->numItems)
        comp->error.handler(comp->error.context, "Too few elements in literal");

    if (!constant)
        doPushVarPtr(comp, arrayOrStruct);

    // Allow closing brace on a new line
    if (comp->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&comp->lex);

    lexEat(&comp->lex, TOK_RBRACE);
}


// dynArrayLiteral = arrayLiteral.
static void parseDynArrayLiteral(Compiler *comp, Type **type, Const *constant)
{
    if (!(*type)->isVariadicParamList)
        lexEat(&comp->lex, TOK_LBRACE);

    if (constant)
        comp->error.handler(comp->error.context, "Dynamic array literals are not allowed for constants");

    // Dynamic array is first parsed as a static array of unknown length, then converted to a dynamic array
    Type *staticArrayType = typeAdd(&comp->types, &comp->blocks, TYPE_ARRAY);
    staticArrayType->base = (*type)->base;
    int itemSize = typeSize(&comp->types, staticArrayType->base);

    // Parse array
    const TokenKind rightEndTok = (*type)->isVariadicParamList ? TOK_RPAR : TOK_RBRACE;
    if (comp->lex.tok.kind != rightEndTok)
    {
        while (1)
        {
            Type *itemType;
            parseExprOrUntypedLiteral(comp, &itemType, staticArrayType->base, NULL);

            // Special case: variadic parameter list's first item is already a dynamic array compatible with the variadic parameter list
            if ((*type)->isVariadicParamList && typeCompatible(*type, itemType, false) && staticArrayType->numItems == 0)
                return;

            doImplicitTypeConv(comp, staticArrayType->base, &itemType, NULL, false);
            typeAssertCompatible(&comp->types, staticArrayType->base, itemType, false);

            staticArrayType->numItems++;

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }

    if (!(*type)->isVariadicParamList)
    {
        // Allow closing brace on a new line
        if (comp->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
            lexNext(&comp->lex);

        lexEat(&comp->lex, TOK_RBRACE);
    }

    // Allocate array
    Ident *staticArray = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, staticArrayType, false);
    doZeroVar(comp, staticArray);

    // Assign items
    for (int i = staticArrayType->numItems - 1; i >= 0; i--)
    {
        genPushLocalPtr(&comp->gen, staticArray->offset + i * itemSize);
        genSwapChangeRefCntAssign(&comp->gen, staticArrayType->base);
    }

    // Convert to dynamic array
    doPushVarPtr(comp, staticArray);
    doImplicitTypeConv(comp, *type, &staticArrayType, NULL, false);
    typeAssertCompatible(&comp->types, *type, staticArrayType, false);
}


// mapLiteral = "{" exprOrLit ":" exprOrLit {"," exprOrLit ":" exprOrLit} "}".
static void parseMapLiteral(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LBRACE);

    if (constant)
        comp->error.handler(comp->error.context, "Map literals are not allowed for constants");

    // Allocate map
    Ident *mapIdent = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, *type, false);
    doZeroVar(comp, mapIdent);

    genPushGlobalPtr(&comp->gen, *type);
    genPushIntConst(&comp->gen, 0);
    doPushVarPtr(comp, mapIdent);
    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_MAKE);

    // Parse map
    if (comp->lex.tok.kind != TOK_RBRACE)
    {
        while (1)
        {
            genDup(&comp->gen);

            // Key
            Type *keyType;
            parseExprOrUntypedLiteral(comp, &keyType, typeMapKey(*type), NULL);
            doImplicitTypeConv(comp, typeMapKey(*type), &keyType, NULL, false);
            typeAssertCompatible(&comp->types, typeMapKey(*type), keyType, false);

            lexEat(&comp->lex, TOK_COLON);

            // Get map item by key
            genGetMapPtr(&comp->gen, *type);

            // Item
            Type *itemType;
            parseExprOrUntypedLiteral(comp, &itemType, typeMapItem(*type), NULL);
            doImplicitTypeConv(comp, typeMapItem(*type), &itemType, NULL, false);
            typeAssertCompatible(&comp->types, typeMapItem(*type), itemType, false);

            // Assign to map item
            genChangeRefCntAssign(&comp->gen, typeMapItem(*type));

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }

    // Allow closing brace on a new line
    if (comp->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
        lexNext(&comp->lex);

    lexEat(&comp->lex, TOK_RBRACE);
}


// closureLiteral = ["|" ident {"," ident} "|"] fnBlock.
static void parseClosureLiteral(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
    {
        // Allocate closure
        Closure *closure = (Closure *)storageAdd(&comp->storage, typeSize(&comp->types, *type));

        // ["|" ident {"," ident} "|"]
        if (comp->lex.tok.kind == TOK_OR)
            comp->error.handler(comp->error.context, "Cannot capture variables in a constant closure literal");

        // fnBlock
        int beforeEntry = comp->gen.ip;

        if (comp->blocks.top != 0)
            genNop(&comp->gen);                                     // Jump over the nested function block (stub)

        Field *fn = typeAssertFindField(&comp->types, *type, "__fn");

        Const fnConstant = {.intVal = comp->gen.ip};
        Ident *fnConstantIdent = identAddTempConst(&comp->idents, &comp->modules, &comp->blocks, fn->type, fnConstant);
        parseFnBlock(comp, fnConstantIdent, NULL);

        if (comp->blocks.top != 0)
            genGoFromTo(&comp->gen, beforeEntry, comp->gen.ip);     // Jump over the nested function block (fixup)

        // Assign closure function
        closure->entryOffset = fnConstant.intVal;
        constant->ptrVal = closure;
    }
    else
    {
        // Allocate closure
        Ident *closureIdent = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, *type, false);
        doZeroVar(comp, closureIdent);

        Type *upvaluesStructType = NULL;

        // ["|" ident {"," ident} "|"]
        if (comp->lex.tok.kind == TOK_OR)
        {
            lexNext(&comp->lex);

            // Determine upvalues structure type
            upvaluesStructType = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
            while (1)
            {
                lexCheck(&comp->lex, TOK_IDENT);

                Ident *capturedIdent = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, comp->lex.tok.name, NULL);
                if (capturedIdent->kind != IDENT_VAR)
                    comp->error.handler(comp->error.context, "%s is not a variable", capturedIdent->name);

                typeAddField(&comp->types, upvaluesStructType, capturedIdent->type, capturedIdent->name);

                lexNext(&comp->lex);

                if (comp->lex.tok.kind != TOK_COMMA)
                    break;
                lexNext(&comp->lex);
            }

            lexEat(&comp->lex, TOK_OR);

            // Allocate upvalues structure
            Ident *upvaluesStructIdent = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, upvaluesStructType, false);
            doZeroVar(comp, upvaluesStructIdent);

            // Assign upvalues structure fields
            for (int i = 0; i < upvaluesStructType->numItems; i++)
            {
                Field *upvalue = upvaluesStructType->field[i];
                Ident *capturedIdent = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, upvalue->name, NULL);

                doPushVarPtr(comp, upvaluesStructIdent);
                genGetFieldPtr(&comp->gen, upvalue->offset);

                doPushVarPtr(comp, capturedIdent);
                genDeref(&comp->gen, capturedIdent->type->kind);

                genChangeRefCntAssign(&comp->gen, upvalue->type);
            }

            // Assign closure upvalues
            Field *upvalues = typeAssertFindField(&comp->types, closureIdent->type, "__upvalues");
            Type *upvaluesType = upvaluesStructIdent->type;

            doPushVarPtr(comp, closureIdent);
            genGetFieldPtr(&comp->gen, upvalues->offset);

            doPushVarPtr(comp, upvaluesStructIdent);
            genDeref(&comp->gen, upvaluesStructIdent->type->kind);
            doImplicitTypeConv(comp, upvalues->type, &upvaluesType, NULL, false);

            genChangeRefCntAssign(&comp->gen, upvalues->type);
        }

        // fnBlock
        int beforeEntry = comp->gen.ip;

        genNop(&comp->gen);                                     // Jump over the nested function block (stub)

        Field *fn = typeAssertFindField(&comp->types, closureIdent->type, "__fn");

        Const fnConstant = {.intVal = comp->gen.ip};
        Ident *fnConstantIdent = identAddTempConst(&comp->idents, &comp->modules, &comp->blocks, fn->type, fnConstant);
        parseFnBlock(comp, fnConstantIdent, upvaluesStructType);

        genGoFromTo(&comp->gen, beforeEntry, comp->gen.ip);     // Jump over the nested function block (fixup)

        // Assign closure function
        doPushVarPtr(comp, closureIdent);
        genGetFieldPtr(&comp->gen, fn->offset);

        doPushConst(comp, fn->type, &fnConstant);

        genChangeRefCntAssign(&comp->gen, fn->type);

        doPushVarPtr(comp, closureIdent);
    }
}


// untypedLiteral = arrayLiteral | dynArrayLiteral | mapLiteral | structLiteral | closureLiteral.
static void parseUntypedLiteral(Compiler *comp, Type **type, Const *constant)
{
    if ((*type)->kind == TYPE_ARRAY || (*type)->kind == TYPE_STRUCT)
        parseArrayOrStructLiteral(comp, type, constant);
    else if ((*type)->kind == TYPE_DYNARRAY)
        parseDynArrayLiteral(comp, type, constant);
    else if ((*type)->kind == TYPE_MAP)
        parseMapLiteral(comp, type, constant);
    else if ((*type)->kind == TYPE_CLOSURE)
        parseClosureLiteral(comp, type, constant);
    else
        comp->error.handler(comp->error.context, "Composite literals are only allowed for arrays, maps, structures and closures");
}


// compositeLiteral = type untypedLiteral.
static void parseTypeCastOrCompositeLiteral(Compiler *comp, Ident *ident, Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit)
{
    *type = parseType(comp, ident);

    if (comp->lex.tok.kind == TOK_LPAR)
    {
        parseTypeCast(comp, type, constant);
        *isCompLit = false;
    }
    else if (comp->lex.tok.kind == TOK_LBRACE || comp->lex.tok.kind == TOK_OR)
    {
        parseUntypedLiteral(comp, type, constant);
        *isCompLit = true;
    }
    else
        comp->error.handler(comp->error.context, "Type cast or composite literal expected");

    *isVar = typeStructured(*type);
    *isCall = false;
}


// derefSelector = "^".
static void parseDerefSelector(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    if ((*type)->kind != TYPE_PTR)
        comp->error.handler(comp->error.context, "Typed pointer expected");

    if (*isVar)
    {
        if ((*type)->base->kind == TYPE_PTR)
        {
            if ((*type)->base->base->kind == TYPE_VOID || (*type)->base->base->kind == TYPE_NULL)
                comp->error.handler(comp->error.context, "Typed pointer expected");

            genDeref(&comp->gen, TYPE_PTR);
            *type = (*type)->base;
        }
        else
            comp->error.handler(comp->error.context, "Typed pointer expected");
    }
    else
    {
        // Accept type-cast lvalues like ^T(x)^ which are not variables and don't need to be dereferenced, so just skip the selector
    }

    lexNext(&comp->lex);
    *isVar = true;
    *isCall = false;
}


// indexSelector = "[" exprOrLit "]".
static void parseIndexSelector(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    // Implicit dereferencing: a^[i] == a[i]
    if ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_PTR)
    {
        genDeref(&comp->gen, TYPE_PTR);
        *type = (*type)->base;
    }

    // Explicit dereferencing for a string, since it is just a pointer, not a structured type
    if ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_STR)
        genDeref(&comp->gen, TYPE_STR);

    if ((*type)->kind == TYPE_PTR &&
       ((*type)->base->kind == TYPE_ARRAY || (*type)->base->kind == TYPE_DYNARRAY || (*type)->base->kind == TYPE_STR || (*type)->base->kind == TYPE_MAP))
        *type = (*type)->base;

    if ((*type)->kind != TYPE_ARRAY && (*type)->kind != TYPE_DYNARRAY && (*type)->kind != TYPE_STR && (*type)->kind != TYPE_MAP)
        comp->error.handler(comp->error.context, "Array, string or map expected");

    // Index or key
    lexNext(&comp->lex);

    if ((*type)->kind == TYPE_MAP)
    {
        Type *keyType = NULL;
        parseExprOrUntypedLiteral(comp, &keyType, typeMapKey(*type), NULL);
        doImplicitTypeConv(comp, typeMapKey(*type), &keyType, NULL, false);
        typeAssertCompatible(&comp->types, typeMapKey(*type), keyType, false);
    }
    else
    {
        Type *indexType = NULL;
        parseExpr(comp, &indexType, NULL);
        typeAssertCompatible(&comp->types, comp->intType, indexType, false);
    }

    lexEat(&comp->lex, TOK_RBRACKET);

    Type *itemType = NULL;
    switch ((*type)->kind)
    {
        case TYPE_ARRAY:
        {
            genGetArrayPtr(&comp->gen, typeSize(&comp->types, (*type)->base), (*type)->numItems);   // Use nominal length for range checking
            itemType = (*type)->base;
            break;
        }
        case TYPE_DYNARRAY:
        {
            genGetDynArrayPtr(&comp->gen);
            itemType = (*type)->base;
            break;
        }
        case TYPE_STR:
        {
            genGetArrayPtr(&comp->gen, typeSize(&comp->types, comp->charType), -1);                 // Use actual length for range checking
            genDeref(&comp->gen, TYPE_CHAR);
            itemType = comp->charType;
            break;
        }
        case TYPE_MAP:
        {
            genGetMapPtr(&comp->gen, *type);
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
            *type = typeAddPtrTo(&comp->types, &comp->blocks, itemType);
        *isVar = true;
    }

    *isCall = false;
}


// fieldSelector = "." ident.
static void parseFieldSelector(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    // Implicit dereferencing: a^.x == a.x
    if ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_PTR)
    {
        genDeref(&comp->gen, TYPE_PTR);
        *type = (*type)->base;
    }

    // Search for a method
    if ((*type)->kind == TYPE_PTR)
        *type = (*type)->base;
    else if (!typeStructured(*type))
        comp->error.handler(comp->error.context, "Addressable value expected");

    lexNext(&comp->lex);
    lexCheck(&comp->lex, TOK_IDENT);

    Type *rcvType = *type;
    int rcvTypeModule = rcvType->typeIdent ? rcvType->typeIdent->module : -1;

    rcvType = typeAddPtrTo(&comp->types, &comp->blocks, rcvType);

    Ident *method = identFind(&comp->idents, &comp->modules, &comp->blocks, rcvTypeModule, comp->lex.tok.name, rcvType, true);
    if (method)
    {
        // Method
        lexNext(&comp->lex);

        // Save concrete method's receiver to dedicated register and push method's entry point
        genPopReg(&comp->gen, VM_REG_SELF);
        doPushConst(comp, method->type, &method->constant);

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
            comp->error.handler(comp->error.context, "Method %s is not defined for %s", comp->lex.tok.name, typeSpelling(*type, typeBuf));
        }

        Field *field = typeAssertFindField(&comp->types, *type, comp->lex.tok.name);
        lexNext(&comp->lex);

        genGetFieldPtr(&comp->gen, field->offset);

        // Save interface method's receiver to dedicated register and push method's entry point
        if (field->type->kind == TYPE_FN && field->type->sig.isMethod && field->type->sig.offsetFromSelf != 0)
        {
            genDup(&comp->gen);
            genGetFieldPtr(&comp->gen, -field->type->sig.offsetFromSelf);
            genDeref(&comp->gen, TYPE_PTR);
            genPopReg(&comp->gen, VM_REG_SELF);
        }

        if (typeStructured(field->type))
            *type = field->type;
        else
            *type = typeAddPtrTo(&comp->types, &comp->blocks, field->type);

        *isVar = true;
        *isCall = false;
    }
}


// callSelector = actualParams.
static void parseCallSelector(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    // Implicit dereferencing
    if ((*type)->kind == TYPE_PTR && ((*type)->base->kind == TYPE_FN || (*type)->base->kind == TYPE_CLOSURE))
    {
        genDeref(&comp->gen, (*type)->base->kind);
        *type = (*type)->base;
    }

    if ((*type)->kind != TYPE_FN && (*type)->kind != TYPE_CLOSURE)
        comp->error.handler(comp->error.context, "Function or closure expected");

    parseCall(comp, type, constant);

    // Push result
    if ((*type)->kind != TYPE_VOID)
        genPushReg(&comp->gen, VM_REG_RESULT);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    if (typeGarbageCollected(*type))
        doCopyResultToTempVar(comp, *type);

    *isVar = typeStructured(*type);
    *isCall = true;
}


// selectors = {derefSelector | indexSelector | fieldSelector | callSelector}.
static void parseSelectors(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit)
{
    while (comp->lex.tok.kind == TOK_CARET  || comp->lex.tok.kind == TOK_LBRACKET ||
           comp->lex.tok.kind == TOK_PERIOD || comp->lex.tok.kind == TOK_LPAR)
    {
        if (constant)
            comp->error.handler(comp->error.context, "%s is not allowed for constants", lexSpelling(comp->lex.tok.kind));

        *isCompLit = false;

        switch (comp->lex.tok.kind)
        {
            case TOK_CARET:     parseDerefSelector(comp, type, constant, isVar, isCall); break;
            case TOK_LBRACKET:  parseIndexSelector(comp, type, constant, isVar, isCall); break;
            case TOK_PERIOD:    parseFieldSelector(comp, type, constant, isVar, isCall); break;
            case TOK_LPAR:      parseCallSelector (comp, type, constant, isVar, isCall); break;
            default:            break;
        } // switch
    } // while
}


// designator = (primary | typeCast | compositeLiteral) selectors.
static void parseDesignator(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit)
{
    Ident *ident = NULL;
    if (comp->lex.tok.kind == TOK_IDENT && (ident = parseQualIdent(comp)) && ident->kind != IDENT_TYPE)
    {
        parsePrimary(comp, ident, type, constant, isVar, isCall);
        *isCompLit = false;
    }
    else
        parseTypeCastOrCompositeLiteral(comp, ident, type, constant, isVar, isCall, isCompLit);

    parseSelectors(comp, type, constant, isVar, isCall, isCompLit);

    if (((*type)->kind == TYPE_FN && (*type)->sig.isMethod) ||
        ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_FN && (*type)->base->sig.isMethod))
    {
        comp->error.handler(comp->error.context, "Method must be called");
    }
}


// designatorList = designator {"," designator}.
void parseDesignatorList(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    bool isCompLit = false;
    parseDesignator(comp, type, constant, isVar, isCall, &isCompLit);

    if (comp->lex.tok.kind == TOK_COMMA && (*isVar) && !(*isCall))
    {
        // Designator list (types formally encoded as structure field types - not a real structure)
        if (constant)
            comp->error.handler(comp->error.context, "Designator lists are not allowed for constants");

        Type *fieldType = *type;
        *type = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
        (*type)->isExprList = true;

        while (1)
        {
            typeAddField(&comp->types, *type, fieldType, NULL);

            if (comp->lex.tok.kind != TOK_COMMA)
                break;

            lexNext(&comp->lex);

            bool fieldIsVar, fieldIsCall, fieldIsCompLit;
            parseDesignator(comp, &fieldType, NULL, &fieldIsVar, &fieldIsCall, &fieldIsCompLit);

            if (!fieldIsVar || fieldIsCall)
                comp->error.handler(comp->error.context, "Inconsistent designator list");
        }
    }
}


// factor = designator | intNumber | realNumber | charLiteral | stringLiteral |
//          ("+" | "-" | "!" | "~" ) factor | "&" designator | "(" expr ")".
static void parseFactor(Compiler *comp, Type **type, Const *constant)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_IDENT:
        case TOK_CARET:
        case TOK_WEAK:
        case TOK_LBRACKET:
        case TOK_STR:
        case TOK_MAP:
        case TOK_STRUCT:
        case TOK_INTERFACE:
        case TOK_FN:
        {
            // A designator that isVar is always an addressable quantity (a structured type or a pointer to a value type)
            bool isVar, isCall, isCompLit;
            parseDesignator(comp, type, constant, &isVar, &isCall, &isCompLit);
            if (isVar)
            {
                if (!typeStructured(*type))
                {
                    genDeref(&comp->gen, (*type)->base->kind);
                    *type = (*type)->base;
                }
            }
            break;
        }

        case TOK_INTNUMBER:
        {
            if (comp->lex.tok.uintVal > (uint64_t)INT64_MAX)
            {
                if (constant)
                    constant->uintVal = comp->lex.tok.uintVal;
                else
                    genPushUIntConst(&comp->gen, comp->lex.tok.uintVal);
                *type = comp->uintType;
            }
            else
            {
                if (constant)
                    constant->intVal = comp->lex.tok.intVal;
                else
                    genPushIntConst(&comp->gen, comp->lex.tok.intVal);
                *type = comp->intType;
            }
            lexNext(&comp->lex);
            break;
        }

        case TOK_REALNUMBER:
        {
            if (constant)
                constant->realVal = comp->lex.tok.realVal;
            else
                genPushRealConst(&comp->gen, comp->lex.tok.realVal);
            lexNext(&comp->lex);
            *type = comp->realType;
            break;
        }

        case TOK_CHARLITERAL:
        {
            if (constant)
                constant->uintVal = comp->lex.tok.uintVal;
            else
                genPushIntConst(&comp->gen, comp->lex.tok.intVal);
            lexNext(&comp->lex);
            *type = comp->charType;
            break;
        }

        case TOK_STRLITERAL:
        {
            if (constant)
                constant->ptrVal = comp->lex.tok.strVal;
            else
                genPushGlobalPtr(&comp->gen, comp->lex.tok.strVal);
            lexNext(&comp->lex);

            *type = typeAdd(&comp->types, &comp->blocks, TYPE_STR);
            break;
        }

        case TOK_PLUS:
        case TOK_MINUS:
        case TOK_NOT:
        case TOK_XOR:
        {
            TokenKind op = comp->lex.tok.kind;
            lexNext(&comp->lex);

            parseFactor(comp, type, constant);
            typeAssertValidOperator(&comp->types, *type, op);

            if (constant)
                constUnary(&comp->consts, constant, op, (*type)->kind);
            else
                genUnary(&comp->gen, op, (*type)->kind);
            break;
        }

        case TOK_AND:
        {
            if (constant)
                comp->error.handler(comp->error.context, "Address operator is not allowed in constant expressions");

            lexNext(&comp->lex);

            bool isVar, isCall, isCompLit;
            parseDesignator(comp, type, constant, &isVar, &isCall, &isCompLit);

            if (!isVar)
                comp->error.handler(comp->error.context, "Unable to take address");

            if (isCompLit)
                doEscapeToHeap(comp, typeAddPtrTo(&comp->types, &comp->blocks, *type), true);

            // A value type is already a pointer, a structured type needs to have it added
            if (typeStructured(*type))
                *type = typeAddPtrTo(&comp->types, &comp->blocks, *type);

            break;
        }

        case TOK_LPAR:
        {
            lexEat(&comp->lex, TOK_LPAR);
            parseExpr(comp, type, constant);
            lexEat(&comp->lex, TOK_RPAR);
            break;
        }

        default: comp->error.handler(comp->error.context, "Illegal expression");
    }
}


// term = factor {("*" | "/" | "%" | "<<" | ">>" | "&") factor}.
static void parseTerm(Compiler *comp, Type **type, Const *constant)
{
    parseFactor(comp, type, constant);

    while (comp->lex.tok.kind == TOK_MUL || comp->lex.tok.kind == TOK_DIV || comp->lex.tok.kind == TOK_MOD ||
           comp->lex.tok.kind == TOK_SHL || comp->lex.tok.kind == TOK_SHR || comp->lex.tok.kind == TOK_AND)
    {
        TokenKind op = comp->lex.tok.kind;
        lexNext(&comp->lex);

        Const rightConstantBuf, *rightConstant;
        if (constant)
            rightConstant = &rightConstantBuf;
        else
            rightConstant = NULL;

        Type *rightType;
        parseFactor(comp, &rightType, rightConstant);
        doApplyOperator(comp, type, &rightType, constant, rightConstant, op, true, true);
    }
}


// relationTerm = term {("+" | "-" | "|" | "^") term}.
static void parseRelationTerm(Compiler *comp, Type **type, Const *constant)
{
    parseTerm(comp, type, constant);

    while (comp->lex.tok.kind == TOK_PLUS || comp->lex.tok.kind == TOK_MINUS ||
           comp->lex.tok.kind == TOK_OR   || comp->lex.tok.kind == TOK_XOR)
    {
        TokenKind op = comp->lex.tok.kind;
        lexNext(&comp->lex);

        Const rightConstantBuf, *rightConstant;
        if (constant)
            rightConstant = &rightConstantBuf;
        else
            rightConstant = NULL;

        Type *rightType;
        parseTerm(comp, &rightType, rightConstant);
        doApplyOperator(comp, type, &rightType, constant, rightConstant, op, true, true);
    }
}


// relation = relationTerm [("==" | "!=" | "<" | "<=" | ">" | ">=") relationTerm].
static void parseRelation(Compiler *comp, Type **type, Const *constant)
{
    parseRelationTerm(comp, type, constant);

    if (comp->lex.tok.kind == TOK_EQEQ   || comp->lex.tok.kind == TOK_NOTEQ   || comp->lex.tok.kind == TOK_LESS ||
        comp->lex.tok.kind == TOK_LESSEQ || comp->lex.tok.kind == TOK_GREATER || comp->lex.tok.kind == TOK_GREATEREQ)
    {
        TokenKind op = comp->lex.tok.kind;
        lexNext(&comp->lex);

        Const rightConstantBuf, *rightConstant;
        if (constant)
            rightConstant = &rightConstantBuf;
        else
            rightConstant = NULL;

        Type *rightType;
        parseRelationTerm(comp, &rightType, rightConstant);
        doApplyOperator(comp, type, &rightType, constant, rightConstant, op, true, true);

        *type = comp->boolType;
    }
}


// logicalTerm = relation {"&&" relation}.
static void parseLogicalTerm(Compiler *comp, Type **type, Const *constant)
{
    parseRelation(comp, type, constant);

    while (comp->lex.tok.kind == TOK_ANDAND)
    {
        TokenKind op = comp->lex.tok.kind;
        lexNext(&comp->lex);

        if (constant)
        {
            if (constant->intVal)
            {
                Const rightConstantBuf, *rightConstant = &rightConstantBuf;

                Type *rightType;
                parseRelation(comp, &rightType, rightConstant);
                doApplyOperator(comp, type, &rightType, constant, rightConstant, op, false, true);
                constant->intVal = rightConstant->intVal;
            }
            else
                constant->intVal = false;
        }
        else
        {
            genShortCircuitProlog(&comp->gen, op);

            blocksEnter(&comp->blocks, NULL);

            Type *rightType;
            parseRelation(comp, &rightType, NULL);
            doApplyOperator(comp, type, &rightType, NULL, NULL, op, false, true);

            doGarbageCollection(comp, blocksCurrent(&comp->blocks));
            identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
            blocksLeave(&comp->blocks);

            genShortCircuitEpilog(&comp->gen);
        }
    }
}


// logicalExpr = logicalTerm {"||" logicalTerm}.
static void parseLogicalExpr(Compiler *comp, Type **type, Const *constant)
{
    parseLogicalTerm(comp, type, constant);

    while (comp->lex.tok.kind == TOK_OROR)
    {
        TokenKind op = comp->lex.tok.kind;
        lexNext(&comp->lex);

        if (constant)
        {
            if (!constant->intVal)
            {
                Const rightConstantBuf, *rightConstant = &rightConstantBuf;

                Type *rightType;
                parseLogicalTerm(comp, &rightType, rightConstant);
                doApplyOperator(comp, type, &rightType, constant, rightConstant, op, false, true);
                constant->intVal = rightConstant->intVal;
            }
            else
                constant->intVal = true;
        }
        else
        {
            genShortCircuitProlog(&comp->gen, op);

            blocksEnter(&comp->blocks, NULL);

            Type *rightType;
            parseLogicalTerm(comp, &rightType, NULL);
            doApplyOperator(comp, type, &rightType, NULL, NULL, op, false, true);

            doGarbageCollection(comp, blocksCurrent(&comp->blocks));
            identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
            blocksLeave(&comp->blocks);

            genShortCircuitEpilog(&comp->gen);
        }
    }
}


// expr = logicalExpr ["?" expr ":" expr].
void parseExpr(Compiler *comp, Type **type, Const *constant)
{
    parseLogicalExpr(comp, type, constant);

    // "?"
    if (comp->lex.tok.kind == TOK_QUESTION)
    {
        typeAssertCompatible(&comp->types, comp->boolType, *type, false);
        lexNext(&comp->lex);

        Type *leftType = NULL, *rightType = NULL;

        if (constant)
        {
            Const leftConstantBuf, *leftConstant = &leftConstantBuf;
            parseExpr(comp, &leftType, leftConstant);

            // ":"
            lexEat(&comp->lex, TOK_COLON);

            Const rightConstantBuf, *rightConstant = &rightConstantBuf;
            parseExpr(comp, &rightType, rightConstant);

            // Convert to left-hand side's type
            doImplicitTypeConv(comp, leftType, &rightType, rightConstant, false);
            typeAssertCompatible(&comp->types, leftType, rightType, false);

            *constant = constant->intVal ? (*leftConstant) : (*rightConstant);
        }
        else
        {
            genIfCondEpilog(&comp->gen);

            // Left-hand side expression
            blocksEnter(&comp->blocks, NULL);

            parseExpr(comp, &leftType, NULL);

            Ident *result = NULL;
            if (typeGarbageCollected(leftType))
            {
                // Create a temporary result variable in the outer block, so that it could outlive both left- and right-hand side expression blocks
                blocksLeave(&comp->blocks);
                result = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, leftType, false);
                blocksReenter(&comp->blocks);

                // Copy result to temporary variable
                genDup(&comp->gen);
                genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, leftType);
                doPushVarPtr(comp, result);
                genSwapAssign(&comp->gen, result->type->kind, typeSize(&comp->types, result->type));
            }

            doGarbageCollection(comp, blocksCurrent(&comp->blocks));
            identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
            blocksLeave(&comp->blocks);

            // ":"
            lexEat(&comp->lex, TOK_COLON);
            genElseProlog(&comp->gen);

            // Right-hand side expression
            blocksEnter(&comp->blocks, NULL);

            parseExpr(comp, &rightType, NULL);

            // Convert to left-hand side's type
            doImplicitTypeConv(comp, leftType, &rightType, NULL, false);
            typeAssertCompatible(&comp->types, leftType, rightType, false);

            if (typeGarbageCollected(leftType))
            {
                // Copy result to temporary variable
                genDup(&comp->gen);
                genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, leftType);
                doPushVarPtr(comp, result);
                genSwapAssign(&comp->gen, result->type->kind, typeSize(&comp->types, result->type));
            }

            doGarbageCollection(comp, blocksCurrent(&comp->blocks));
            identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
            blocksLeave(&comp->blocks);

            genIfElseEpilog(&comp->gen);
        }

        *type = leftType;
    }
}


// exprOrLit = expr | untypedLiteral.
void parseExprOrUntypedLiteral(Compiler *comp, Type **type, Type *untypedLiteralType, Const *constant)
{
    if (untypedLiteralType && (comp->lex.tok.kind == TOK_LBRACE || comp->lex.tok.kind == TOK_OR))
    {
        *type = untypedLiteralType;
        parseUntypedLiteral(comp, type, constant);
    }
    else
        parseExpr(comp, type, constant);
}


// exprOrLitList = exprOrLit {"," exprOrLit}.
void parseExprOrUntypedLiteralList(Compiler *comp, Type **type, Type *destType, Const *constant)
{
    Type *untypedLiteralType = destType;
    if (destType && typeExprListStruct(destType) && destType->numItems > 0)
        untypedLiteralType = destType->field[0]->type;

    parseExprOrUntypedLiteral(comp, type, untypedLiteralType, constant);

    if (comp->lex.tok.kind == TOK_COMMA)
    {
        // Expression list (syntactic sugar - actually a structure literal)
        Const fieldConstantBuf[MAX_FIELDS], *fieldConstant = NULL;
        if (constant)
        {
            fieldConstantBuf[0] = *constant;
            fieldConstant = &fieldConstantBuf[0];
        }

        Type *fieldType = *type;
        *type = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
        (*type)->isExprList = true;

        // Evaluate expressions and get the total structure size
        while (1)
        {
            // Convert field to the desired type if necessary and possible (no error is thrown anyway)
            if (destType && destType->numItems > (*type)->numItems)
            {
                Type *destFieldType = destType->field[(*type)->numItems]->type;
                doImplicitTypeConv(comp, destFieldType, &fieldType, fieldConstant, false);
                if (typeCompatible(destFieldType, fieldType, false))
                    fieldType = destFieldType;
            }

            if (typeExprListStruct(fieldType))
                comp->error.handler(comp->error.context, "Nested expression lists are not allowed");

            typeAddField(&comp->types, *type, fieldType, NULL);

            if (comp->lex.tok.kind != TOK_COMMA)
                break;

            fieldConstant = constant ? &fieldConstantBuf[(*type)->numItems] : NULL;

            lexNext(&comp->lex);

            untypedLiteralType = NULL;
            if (destType && typeExprListStruct(destType) && destType->numItems > (*type)->numItems)
                untypedLiteralType = destType->field[(*type)->numItems]->type;

            parseExprOrUntypedLiteral(comp, &fieldType, untypedLiteralType, fieldConstant);
        }

        // Allocate structure
        Ident *exprList = NULL;
        if (constant)
            constant->ptrVal = storageAdd(&comp->storage, typeSize(&comp->types, *type));
        else
        {
            exprList = identAllocTempVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, *type, false);
            doZeroVar(comp, exprList);
        }

        // Assign expressions
        for (int i = (*type)->numItems - 1; i >= 0; i--)
        {
            Field *field = (*type)->field[i];
            int fieldSize = typeSize(&comp->types, field->type);

            if (constant)
                constAssign(&comp->consts, (char *)constant->ptrVal + field->offset, &fieldConstantBuf[i], field->type->kind, fieldSize);
            else
            {
                genPushLocalPtr(&comp->gen, exprList->offset + field->offset);
                genSwapChangeRefCntAssign(&comp->gen, field->type);
            }
        }

        if (!constant)
            doPushVarPtr(comp, exprList);
    }
}

