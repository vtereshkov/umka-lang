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

    // Increase parameter's reference count
    genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, formalParamType);

    // Copy structured parameter if passed by value
    if (typeStructured(formalParamType))
        genPushStruct(&comp->gen, typeSize(&comp->types, formalParamType));
}


void doCopyResultToTempVar(Compiler *comp, Type *type)
{
    IdentName tempName;
    identTempVarName(&comp->idents, tempName);

    Ident *temp = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, tempName, type, false);
    doZeroVar(comp, temp);

    genDup(&comp->gen);
    doPushVarPtr(comp, temp);
    genSwapAssign(&comp->gen, type->kind, typeSize(&comp->types, type));
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
        char *buf = storageAdd(&comp->storage, 2 * typeSize(&comp->types, *src));
        buf[0] = constant->intVal;
        buf[1] = 0;
        constant->ptrVal = buf;
    }
    else
    {
        if (lhs)
            genSwap(&comp->gen);

        // Allocate heap for two chars
        genPushGlobalPtr(&comp->gen, NULL);
        genPushIntConst(&comp->gen, 2 * typeSize(&comp->types, *src));
        genCallBuiltin(&comp->gen, TYPE_PTR, BUILTIN_NEW);
        doCopyResultToTempVar(comp, comp->strType);

        // Save heap pointer
        genDup(&comp->gen);
        genPopReg(&comp->gen, VM_REG_COMMON_0);

        // Copy to heap and use heap pointer
        genSwapAssign(&comp->gen, (*src)->kind, typeSize(&comp->types, *src));
        genPushReg(&comp->gen, VM_REG_COMMON_0);

        if (lhs)
            genSwap(&comp->gen);
    }

    *src = dest;
}


static void doDynArrayToStrConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to string is not allowed in constant expressions");

    // fn maketostr(src: []ItemType): str

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
        int rcvTypeModule = rcvType->typeIdent ? rcvType->typeIdent->module : -1;

        Ident *srcMethod = identFind(&comp->idents, &comp->modules, &comp->blocks, rcvTypeModule, name, *src, true);
        if (!srcMethod)
            comp->error.handler(comp->error.context, "Method %s is not implemented", name);

        if (!typeCompatible(dest->field[i]->type, srcMethod->type, false))
            comp->error.handler(comp->error.context, "Method %s has incompatible signature", name);

        genPushIntConst(&comp->gen, srcMethod->offset);                     // Push src value
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
}


static void doExplicitTypeConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
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
}


static void doApplyStrCat(Compiler *comp, Const *constant, Const *rightConstant)
{
    if (constant)
    {
        int bufLen = strlen((char *)constant->ptrVal) + strlen((char *)rightConstant->ptrVal) + 1;
        char *buf = storageAdd(&comp->storage, bufLen);
        strcpy(buf, (char *)constant->ptrVal);

        constant->ptrVal = buf;
        constBinary(&comp->consts, constant, rightConstant, TOK_PLUS, TYPE_STR);
    }
    else
    {
        genBinary(&comp->gen, TOK_PLUS, TYPE_STR, 0);
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
        if ((*type)->kind == TYPE_STR && op == TOK_PLUS)
            doApplyStrCat(comp, constant, rightConstant);
        else
        {
            if (constant)
                constBinary(&comp->consts, constant, rightConstant, op, (*type)->kind);
            else
                genBinary(&comp->gen, op, (*type)->kind, 0);
        }
    }
}


// qualIdent = [ident "."] ident.
Ident *parseQualIdent(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    int module = moduleFindImported(&comp->modules, &comp->blocks, comp->lex.tok.name, true);
    if (module >= 0)
    {
        if (identFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, comp->lex.tok.name, NULL, false))
            comp->error.handler(comp->error.context, "Conflict between module %s and identifier %s", comp->lex.tok.name, comp->lex.tok.name);

        lexNext(&comp->lex);
        lexEat(&comp->lex, TOK_PERIOD);
        lexCheck(&comp->lex, TOK_IDENT);
    }
    else
        module = comp->blocks.module;

    Ident *ident = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, module, comp->lex.tok.name, NULL);

    if (identIsOuterLocalVar(&comp->blocks, ident))
        comp->error.handler(comp->error.context, "Closures are not supported, cannot close over %s", ident->name);

    return ident;
}


static void parseBuiltinIOCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Parameters: count, stream, format, value

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
            typeAssertCompatibleBuiltin(&comp->types, *type, builtin, typeOrdinal(*type) || typeReal(*type) || (*type)->kind == TYPE_STR);
            genCallBuiltin(&comp->gen, (*type)->kind, builtin);
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


// fn new(type: Type, size: int): ^type
static void parseBuiltinNewCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    *type = parseType(comp, NULL);
    int size = typeSize(&comp->types, *type);

    genPushGlobalPtr(&comp->gen, *type);
    genPushIntConst(&comp->gen, size);
    genCallBuiltin(&comp->gen, TYPE_PTR, BUILTIN_NEW);

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
static void parseBuiltinCopyCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_COPY, (*type)->kind == TYPE_DYNARRAY);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_COPY);
}


// fn append(array: [] type, item: (^type | [] type), single: bool): [] type
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

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_APPEND);
}


// fn insert(array: [] type, index: int, item: type): [] type
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


// fn slice(array: [] type | str, startIndex [, endIndex]: int): [] type | str
static void parseBuiltinSliceCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
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


static void parseBuiltinValidCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_VALID, (*type)->kind == TYPE_DYNARRAY || (*type)->kind == TYPE_MAP || (*type)->kind == TYPE_INTERFACE || (*type)->kind == TYPE_FN || (*type)->kind == TYPE_FIBER);

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
    parseExpr(comp, &keyType, constant);
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
        Type *expectedAnyParamType = fiberFuncType->sig.param[1]->type;

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


// fn repr(val: type, type): str
static void parseBuiltinReprCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    typeAssertCompatibleBuiltin(&comp->types, *type, BUILTIN_REPR, (*type)->kind != TYPE_VOID);

    genPushGlobalPtr(&comp->gen, *type);

    genCallBuiltin(&comp->gen, TYPE_STR, BUILTIN_REPR);
    *type = comp->strType;
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
        case BUILTIN_SIZEOF:        parseBuiltinSizeofCall(comp, type, constant);           break;
        case BUILTIN_SIZEOFSELF:    parseBuiltinSizeofselfCall(comp, type, constant);       break;
        case BUILTIN_SELFHASPTR:    parseBuiltinSelfhasptrCall(comp, type, constant);       break;
        case BUILTIN_SELFTYPEEQ:    parseBuiltinSelftypeeqCall(comp, type, constant);       break;
        case BUILTIN_VALID:         parseBuiltinValidCall(comp, type, constant);            break;

        // Maps
        case BUILTIN_VALIDKEY:      parseBuiltinValidkeyCall(comp, type, constant);         break;
        case BUILTIN_KEYS:          parseBuiltinKeysCall(comp, type, constant);             break;

        // Fibers
        case BUILTIN_FIBERSPAWN:
        case BUILTIN_FIBERCALL:
        case BUILTIN_FIBERALIVE:    parseBuiltinFiberCall(comp, type, constant, builtin);   break;

        // Misc
        case BUILTIN_REPR:          parseBuiltinReprCall(comp, type, constant);             break;
        case BUILTIN_EXIT:          parseBuiltinExitCall(comp, type, constant);             break;
        case BUILTIN_ERROR:         parseBuiltinErrorCall(comp, type, constant);            break;

        default: comp->error.handler(comp->error.context, "Illegal built-in function");
    }
    lexEat(&comp->lex, TOK_RPAR);
}


// actualParams = "(" [expr {"," expr}] ")".
static void parseCall(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LPAR);

    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Decide whether a (default) indirect call can be replaced with a direct call
    int immediateEntryPoint = genTryRemoveImmediateEntryPoint(&comp->gen);

    // Actual parameters: [__self,] param1, param2 ...[__result]
    int numExplicitParams = 0, numPreHiddenParams = 0, numPostHiddenParams = 0;
    int i = 0;

    // Method receiver
    if ((*type)->sig.method)
    {
        genPushReg(&comp->gen, VM_REG_SELF);

        // Increase receiver's reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, (*type)->sig.param[0]->type);

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
                comp->error.handler(comp->error.context, "Too many actual parameters");

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
                parseExpr(comp, &actualParamType, constant);

                doImplicitTypeConv(comp, formalParamType, &actualParamType, constant, false);
                typeAssertCompatible(&comp->types, formalParamType, actualParamType, false);
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
        comp->error.handler(comp->error.context, "Too few actual parameters");

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
            if (typeGarbageCollected(*type))
                doCopyResultToTempVar(comp, *type);

            *isVar = false;
            *isCall = true;
            break;
        }

        default: comp->error.handler(comp->error.context, "Illegal identifier");
    }
}


// typeCast = type "(" expr ")".
static void parseTypeCast(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LPAR);

    Type *originalType;
    parseExpr(comp, &originalType, constant);
    doExplicitTypeConv(comp, *type, &originalType, constant, false);

    if (!typeEquivalent(*type, originalType))
        comp->error.handler(comp->error.context, "Invalid type cast");

    lexEat(&comp->lex, TOK_RPAR);
}


// arrayLiteral     = "{" [expr {"," expr}] "}".
// structLiteral    = "{" [[ident ":"] expr {"," [ident ":"] expr}] "}".
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
    int bufOffset = 0;

    if (constant)
    {
        constant->ptrVal = storageAdd(&comp->storage, size);
        if (namedFields)
            constZero(constant->ptrVal, size);
    }
    else
    {
        bufOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);

        if (namedFields)
        {
            genPushLocalPtr(&comp->gen, bufOffset);
            genZero(&comp->gen, size);
        }
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
                genPushLocalPtr(&comp->gen, bufOffset + itemOffset);

            Type *expectedItemType = (*type)->kind == TYPE_ARRAY ? (*type)->base : field->type;
            Type *itemType;
            Const itemConstantBuf, *itemConstant = constant ? &itemConstantBuf : NULL;
            int itemSize = typeSize(&comp->types, expectedItemType);

            // expr
            parseExpr(comp, &itemType, itemConstant);

            doImplicitTypeConv(comp, expectedItemType, &itemType, itemConstant, false);
            typeAssertCompatible(&comp->types, expectedItemType, itemType, false);

            if (constant)
                constAssign(&comp->consts, (char *)constant->ptrVal + itemOffset, itemConstant, expectedItemType->kind, itemSize);
            else
                // Assignment to an anonymous stack area does not require updating reference counts
                genAssign(&comp->gen, expectedItemType->kind, itemSize);

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
    {
        genPushLocalPtr(&comp->gen, bufOffset);
        doEscapeToHeap(comp, typeAddPtrTo(&comp->types, &comp->blocks, *type), true);
    }

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
            parseExpr(comp, &itemType, NULL);

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
        lexEat(&comp->lex, TOK_RBRACE);

    // Allocate array
    int bufOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, staticArrayType);

    // Assign items
    for (int i = staticArrayType->numItems - 1; i >= 0; i--)
    {
        // Assignment to an anonymous stack area does not require updating reference counts
        genPushLocalPtr(&comp->gen, bufOffset + i * itemSize);
        genSwapAssign(&comp->gen, staticArrayType->base->kind, itemSize);
    }

    // Convert to dynamic array
    genPushLocalPtr(&comp->gen, bufOffset);
    doEscapeToHeap(comp, typeAddPtrTo(&comp->types, &comp->blocks, staticArrayType), true);

    doImplicitTypeConv(comp, *type, &staticArrayType, NULL, false);
    typeAssertCompatible(&comp->types, *type, staticArrayType, false);
}


// mapLiteral = "{" expr ":" expr {"," expr ":" expr} "}".
static void parseMapLiteral(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LBRACE);

    if (constant)
        comp->error.handler(comp->error.context, "Map literals are not allowed for constants");

    // Allocate map
    IdentName mapName;
    identTempVarName(&comp->idents, mapName);
    Ident *mapIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, mapName, *type, false);
    doZeroVar(comp, mapIdent);

    genPushGlobalPtr(&comp->gen, *type);
    genPushIntConst(&comp->gen, 0);
    doPushVarPtr(comp, mapIdent);
    genCallBuiltin(&comp->gen, (*type)->kind, BUILTIN_MAKE);

    doEscapeToHeap(comp, typeAddPtrTo(&comp->types, &comp->blocks, *type), true);

    // Parse map
    if (comp->lex.tok.kind != TOK_RBRACE)
    {
        while (1)
        {
            genDup(&comp->gen);

            // Key
            Type *keyType;
            parseExpr(comp, &keyType, NULL);
            doImplicitTypeConv(comp, typeMapKey(*type), &keyType, NULL, false);
            typeAssertCompatible(&comp->types, typeMapKey(*type), keyType, false);

            lexEat(&comp->lex, TOK_COLON);

            // Get map item by key
            genGetMapPtr(&comp->gen, *type);

            // Item
            Type *itemType;
            parseExpr(comp, &itemType, NULL);
            doImplicitTypeConv(comp, typeMapItem(*type), &itemType, NULL, false);
            typeAssertCompatible(&comp->types, typeMapItem(*type), itemType, false);

            // Assign to map item
            genChangeRefCntAssign(&comp->gen, typeMapItem(*type));

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }

    //genPop(&comp->gen);
    lexEat(&comp->lex, TOK_RBRACE);
}


// fnLiteral = fnBlock.
static void parseFnLiteral(Compiler *comp, Type **type, Const *constant)
{
    int beforeEntry = comp->gen.ip;

    if (comp->blocks.top != 0)
        genNop(&comp->gen);                                     // Jump over the nested function block (stub)

    IdentName tempName;
    identTempVarName(&comp->idents, tempName);

    Const fnConstant = {.intVal = comp->gen.ip};
    Ident *fn = identAddConst(&comp->idents, &comp->modules, &comp->blocks, tempName, *type, false, fnConstant);
    parseFnBlock(comp, fn);

    if (comp->blocks.top != 0)
        genGoFromTo(&comp->gen, beforeEntry, comp->gen.ip);     // Jump over the nested function block (fixup)

    if (constant)
        *constant = fnConstant;
    else
        doPushConst(comp, fn->type, &fn->constant);
}


// compositeLiteral = arrayLiteral | dynArrayLiteral | mapLiteral | structLiteral | fnLiteral.
static void parseCompositeLiteral(Compiler *comp, Type **type, Const *constant)
{
    if ((*type)->kind == TYPE_ARRAY || (*type)->kind == TYPE_STRUCT)
        parseArrayOrStructLiteral(comp, type, constant);
    else if ((*type)->kind == TYPE_DYNARRAY)
        parseDynArrayLiteral(comp, type, constant);
    else if ((*type)->kind == TYPE_MAP)
        parseMapLiteral(comp, type, constant);
    else if ((*type)->kind == TYPE_FN)
        parseFnLiteral(comp, type, constant);
    else
        comp->error.handler(comp->error.context, "Composite literals are only allowed for arrays, maps, structures and functions");
}


static void parseTypeCastOrCompositeLiteral(Compiler *comp, Ident *ident, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    *type = parseType(comp, ident);

    if (comp->lex.tok.kind == TOK_LPAR)
        parseTypeCast(comp, type, constant);
    else if (comp->lex.tok.kind == TOK_LBRACE)
        parseCompositeLiteral(comp, type, constant);
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


// indexSelector = "[" expr "]".
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
    Type *indexType = NULL;
    parseExpr(comp, &indexType, NULL);

    if ((*type)->kind == TYPE_MAP)
    {
        doImplicitTypeConv(comp, typeMapKey(*type), &indexType, NULL, false);
        typeAssertCompatible(&comp->types, typeMapKey(*type), indexType, false);
    }
    else
        typeAssertCompatible(&comp->types, comp->intType, indexType, false);

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
            comp->error.handler(comp->error.context, "Method %s is not found, structure expected to look for field", comp->lex.tok.name);

        Field *field = typeAssertFindField(&comp->types, *type, comp->lex.tok.name);
        lexNext(&comp->lex);

        genGetFieldPtr(&comp->gen, field->offset);

        // Save interface method's receiver to dedicated register and push method's entry point
        if (field->type->kind == TYPE_FN && field->type->sig.method && field->type->sig.offsetFromSelf != 0)
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
    if ((*type)->kind == TYPE_PTR && (*type)->base->kind == TYPE_FN)
    {
        genDeref(&comp->gen, TYPE_FN);
        *type = (*type)->base;
    }

    if ((*type)->kind != TYPE_FN)
        comp->error.handler(comp->error.context, "Function expected");

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
static void parseSelectors(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    while (comp->lex.tok.kind == TOK_CARET  || comp->lex.tok.kind == TOK_LBRACKET ||
           comp->lex.tok.kind == TOK_PERIOD || comp->lex.tok.kind == TOK_LPAR)
    {
        if (constant)
            comp->error.handler(comp->error.context, "%s is not allowed for constants", lexSpelling(comp->lex.tok.kind));

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
void parseDesignator(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    Ident *ident = NULL;
    if (comp->lex.tok.kind == TOK_IDENT && (ident = parseQualIdent(comp)) && ident->kind != IDENT_TYPE)
        parsePrimary(comp, ident, type, constant, isVar, isCall);
    else
        parseTypeCastOrCompositeLiteral(comp, ident, type, constant, isVar, isCall);

    parseSelectors(comp, type, constant, isVar, isCall);
}


// designatorList = designator {"," designator}.
void parseDesignatorList(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    parseDesignator(comp, type, constant, isVar, isCall);

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

            bool fieldIsVar, fieldIsCall;
            parseDesignator(comp, &fieldType, NULL, &fieldIsVar, &fieldIsCall);

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
            bool isVar, isCall;
            parseDesignator(comp, type, constant, &isVar, &isCall);
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

            bool isVar, isCall;
            parseDesignator(comp, type, constant, &isVar, &isCall);

            if (!isVar)
                comp->error.handler(comp->error.context, "Unable to take address");

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


// expr = logicalTerm {"||" logicalTerm}.
void parseExpr(Compiler *comp, Type **type, Const *constant)
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


// exprList = expr {"," expr}.
void parseExprList(Compiler *comp, Type **type, Type *destType, Const *constant)
{
    parseExpr(comp, type, constant);

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

            typeAddField(&comp->types, *type, fieldType, NULL);

            if (comp->lex.tok.kind != TOK_COMMA)
                break;

            fieldConstant = constant ? &fieldConstantBuf[(*type)->numItems] : NULL;

            lexNext(&comp->lex);
            parseExpr(comp, &fieldType, fieldConstant);
        }

        // Allocate structure
        int bufOffset = 0;
        if (constant)
            constant->ptrVal = storageAdd(&comp->storage, typeSize(&comp->types, *type));
        else
            bufOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, *type);

        // Assign expressions
        for (int i = (*type)->numItems - 1; i >= 0; i--)
        {
            Field *field = (*type)->field[i];
            int fieldSize = typeSize(&comp->types, field->type);

            if (constant)
                constAssign(&comp->consts, (char *)constant->ptrVal + field->offset, &fieldConstantBuf[i], field->type->kind, fieldSize);
            else
            {
                // Assignment to an anonymous stack area does not require updating reference counts
                genPushLocalPtr(&comp->gen, bufOffset + field->offset);
                genSwapAssign(&comp->gen, field->type->kind, fieldSize);
            }
        }

        if (!constant)
        {
            genPushLocalPtr(&comp->gen, bufOffset);
            doEscapeToHeap(comp, typeAddPtrTo(&comp->types, &comp->blocks, *type), true);
        }
    }
}

