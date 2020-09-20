#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "umka_expr.h"
#include "umka_decl.h"
#include "umka_stmt.h"


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


static void doCopyResultToTempVar(Compiler *comp, Type *type)
{
    IdentName tempName;
    identTempVarName(&comp->idents, tempName);
    Ident *__temp = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, tempName, type, false);

    genDup(&comp->gen);
    doPushVarPtr(comp, __temp);
    genSwapAssign(&comp->gen, type->kind, typeSize(&comp->types, type));
}


static void doIntToRealConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    BuiltinFunc builtin = lhs ? BUILTIN_REAL_LHS : BUILTIN_REAL;
    if (constant)
        constCallBuiltin(&comp->consts, constant, NULL, *src, builtin);
    else
        genCallBuiltin(&comp->gen, (*src)->kind, builtin);

    *src = dest;
}


static void doCharToStrConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    if (constant)
    {
        char *buf = &comp->storage.data[comp->storage.len];
        comp->storage.len += 2 * typeSize(&comp->types, *src);

        buf[0] = constant->intVal;
        buf[1] = 0;
        constant->ptrVal = (int64_t)buf;
    }
    else
    {
        if (lhs)
            genSwap(&comp->gen);

        // Allocate heap for two chars
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


static void doDynArrayPtrToArrayPtrConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to array is not allowed in constant expressions");

    genGetFieldPtr(&comp->gen, offsetof(DynArray, data));
    genDeref(&comp->gen, TYPE_PTR);

    *src = dest;
}


static void doArrayToDynArrayConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to array is not allowed in constant expressions");

    // fn makefrom(array: [...] type, itemSize: int, len: int): [] type

    genPushIntConst(&comp->gen, typeSize(&comp->types, (*src)->base));  // Dynamic array item size
    genPushIntConst(&comp->gen, (*src)->numItems);                      // Dynamic array length

    int resultOffset = identAllocStack(&comp->idents, &comp->blocks, typeSize(&comp->types, dest));
    genPushLocalPtr(&comp->gen, resultOffset);                          // Pointer to result (hidden parameter)

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_MAKEFROM);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    doCopyResultToTempVar(comp, dest);

    *src = dest;
}


static void doPtrToInterfaceConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion to interface is not allowed in constant expressions");

    int destSize   = typeSize(&comp->types, dest);
    int destOffset = identAllocStack(&comp->idents, &comp->blocks, destSize);

    // Assign to __self
    genPushLocalPtr(&comp->gen, destOffset);                                // Push dest.__self pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__self

    // Assign to __selftype (RTTI)
    Field *__selftype = typeAssertFindField(&comp->types, dest, "__selftype");

    genPushGlobalPtr(&comp->gen, *src);                                     // Push src type
    genPushLocalPtr(&comp->gen, destOffset + __selftype->offset);           // Push dest.__selftype pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__selftype

    // Assign to methods
    for (int i = 2; i < dest->numItems; i++)
    {
        char *name = dest->field[i]->name;
        Ident *srcMethod = identFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, name, *src);
        if (!srcMethod)
            comp->error.handler(comp->error.context, "Method %s is not implemented", name);

        typeAssertCompatible(&comp->types, dest->field[i]->type, srcMethod->type, false);

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

    int destSize = typeSize(&comp->types, dest);
    int destOffset = identAllocStack(&comp->idents, &comp->blocks, destSize);

    // Assign to __self
    genDup(&comp->gen);                                                     // Duplicate src pointer
    genDeref(&comp->gen, TYPE_PTR);                                         // Get src.__self value
    genPushLocalPtr(&comp->gen, destOffset);                                // Push dest pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__self (NULL means a dynamic type)

    // Assign to __selftype (RTTI)
    Field *__selftype = typeAssertFindField(&comp->types, dest, "__selftype");

    genDup(&comp->gen);                                                     // Duplicate src pointer
    genGetFieldPtr(&comp->gen, __selftype->offset);                         // Get src.__selftype pointer
    genDeref(&comp->gen, TYPE_PTR);                                         // Get src.__selftype value
    genPushLocalPtr(&comp->gen, destOffset + __selftype->offset);           // Push dest.__selftype pointer
    genSwapAssign(&comp->gen, TYPE_PTR, 0);                                 // Assign to dest.__selftype

    // Assign to methods
    for (int i = 2; i < dest->numItems; i++)
    {
        char *name = dest->field[i]->name;
        Field *srcMethod = typeFindField(*src, name);
        if (!srcMethod)
            comp->error.handler(comp->error.context, "Method %s is not implemented", name);

        typeAssertCompatible(&comp->types, dest->field[i]->type, srcMethod->type, false);

        genDup(&comp->gen);                                                 // Duplicate src pointer
        genGetFieldPtr(&comp->gen, srcMethod->offset);                      // Get src.method pointer
        genDeref(&comp->gen, TYPE_PTR);                                     // Get src.method value (entry point)
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

    Type *srcPtrType = typeAddPtrTo(&comp->types, &comp->blocks, *src);

    // Allocate heap
    genPushIntConst(&comp->gen, typeSize(&comp->types, *src));
    genCallBuiltin(&comp->gen, TYPE_PTR, BUILTIN_NEW);
    doCopyResultToTempVar(comp, srcPtrType);

    // Save heap pointer
    genDup(&comp->gen);
    genPopReg(&comp->gen, VM_REG_COMMON_0);

    // Copy to heap and use heap pointer (interfaces accept only pointers as their dynamic types)
    genSwapAssign(&comp->gen, (*src)->kind, typeSize(&comp->types, *src));
    genPushReg(&comp->gen, VM_REG_COMMON_0);

    *src = srcPtrType;
    doPtrToInterfaceConv(comp, dest, src, constant);
}


static void doInterfaceToPtrConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Conversion from interface is not allowed in constant expressions");

    genAssertType(&comp->gen, dest);
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

    // Array to dynamic array
    else if (dest->kind == TYPE_DYNARRAY && (*src)->kind == TYPE_ARRAY && typeEquivalent(dest->base, (*src)->base))
    {
        doArrayToDynArrayConv(comp, dest, src, constant);
    }

    // Dynamic array pointer to array pointer
    else if (dest->kind == TYPE_PTR && dest->base->kind == TYPE_ARRAY &&
            (*src)->kind == TYPE_PTR && (*src)->base->kind == TYPE_DYNARRAY &&
             typeEquivalent(dest->base->base, (*src)->base->base))
    {
        doDynArrayPtrToArrayPtrConv(comp, dest, src, constant);
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
            doPtrToInterfaceConv(comp, dest, src, constant);
        }
        else
        {
            // Value to interface
            doValueToInterfaceConv(comp, dest, src, constant);
        }
    }

    // Interface to concrete (type assertion)
    else if (dest->kind == TYPE_PTR && (*src)->kind == TYPE_INTERFACE)
    {
        doInterfaceToPtrConv(comp, dest, src, constant);
    }
}


static void doApplyStrCat(Compiler *comp, Const *constant, Const *rightConstant)
{
    if (constant)
    {
        int bufLen = strlen((char *)constant->ptrVal) + strlen((char *)rightConstant->ptrVal) + 1;
        char *buf = &comp->storage.data[comp->storage.len];
        comp->storage.len += bufLen;

        strcpy(buf, (char *)constant->ptrVal);
        constant->ptrVal = (int64_t)buf;
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
    doImplicitTypeConv(comp, *type, rightType, rightConstant, false);

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
    int module = moduleFind(&comp->modules, comp->lex.tok.name);
    if (module >= 0)
    {
        lexNext(&comp->lex);
        lexEat(&comp->lex, TOK_PERIOD);
        lexCheck(&comp->lex, TOK_IDENT);
    }
    else
        module = comp->blocks.module;

    return identAssertFind(&comp->idents, &comp->modules, &comp->blocks, module, comp->lex.tok.name, NULL);
}


static void parseBuiltinIOCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Count (number of characters for printf(), number of items for scanf())
    genPushIntConst(&comp->gen, 0);
    genPopReg(&comp->gen, VM_REG_IO_COUNT);

    // File/string pointer
    if (builtin == BUILTIN_FPRINTF || builtin == BUILTIN_SPRINTF ||
        builtin == BUILTIN_FSCANF  || builtin == BUILTIN_SSCANF)
    {
        Type *expectedType;
        if (builtin == BUILTIN_FPRINTF || builtin == BUILTIN_FSCANF)
            expectedType = comp->ptrVoidType;
        else
            expectedType = comp->strType;

        parseExpr(comp, type, constant);
        typeAssertCompatible(&comp->types, expectedType, *type, false);
        genPopReg(&comp->gen, VM_REG_IO_STREAM);
        lexEat(&comp->lex, TOK_COMMA);
    }

    // Format string
    parseExpr(comp, type, constant);
    typeAssertCompatible(&comp->types, comp->strType, *type, false);
    genPopReg(&comp->gen, VM_REG_IO_FORMAT);

    // Values, if any
    while (comp->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&comp->lex);
        parseExpr(comp, type, constant);

        if (builtin == BUILTIN_PRINTF || builtin == BUILTIN_FPRINTF || builtin == BUILTIN_SPRINTF)
        {
            if (typeOrdinal(*type) || typeReal(*type) || (*type)->kind == TYPE_STR)
                genCallBuiltin(&comp->gen, (*type)->kind, builtin);
            else
                comp->error.handler(comp->error.context, "Incompatible type in printf()");
        }
        else  // BUILTIN_SCANF, BUILTIN_FSCANF, BUILTIN_SSCANF
        {
            if ((*type)->kind == TYPE_PTR && (typeOrdinal((*type)->base) || typeReal((*type)->base) || (*type)->base->kind == TYPE_STR))
            {
                if ((*type)->base->kind == TYPE_STR)
                    genDeref(&comp->gen, TYPE_STR);

                genCallBuiltin(&comp->gen, (*type)->base->kind, builtin);
            }
            else
                comp->error.handler(comp->error.context, "Incompatible type in scanf()");
        }
        genPop(&comp->gen); // Manually remove parameter

    } // while

    // The rest of format string
    genPushIntConst(&comp->gen, 0);
    genCallBuiltin(&comp->gen, TYPE_VOID, builtin);
    genPop(&comp->gen);  // Manually remove parameter

    // Result
    genPushReg(&comp->gen, VM_REG_IO_COUNT);

    *type = comp->intType;
}


static void parseBuiltinMathCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    parseExpr(comp, type, constant);
    doImplicitTypeConv(comp, comp->realType, type, constant, false);
    typeAssertCompatible(&comp->types, comp->realType, *type, false);

    Const *constant2 = NULL;

    // fn atan2(y, x: real): real
    if (builtin == BUILTIN_ATAN2)
    {
        lexEat(&comp->lex, TOK_COMMA);

        Type *type2;
        Const constant2Val;
        if (constant)
            constant2 = &constant2Val;

        parseExpr(comp, &type2, constant2);
        doImplicitTypeConv(comp, comp->realType, &type2, constant2, false);
        typeAssertCompatible(&comp->types, comp->realType, type2, false);
    }

    if (constant)
        constCallBuiltin(&comp->consts, constant, constant2, *type, builtin);
    else
        genCallBuiltin(&comp->gen, TYPE_REAL, builtin);

    if (builtin == BUILTIN_ROUND || builtin == BUILTIN_TRUNC)
        *type = comp->intType;
    else
        *type = comp->realType;
}


// fn new(type (actually size: int)): ^type
static void parseBuiltinNewCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    *type = parseType(comp, NULL);
    int size = typeSize(&comp->types, *type);

    genPushIntConst(&comp->gen, size);
    genCallBuiltin(&comp->gen, TYPE_PTR, BUILTIN_NEW);

    *type = typeAddPtrTo(&comp->types, &comp->blocks, *type);
}


// fn make([...] type (actually itemSize: int), len: int): [] type
static void parseBuiltinMakeCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array type and item size
    *type = parseType(comp, NULL);
    if ((*type)->kind != TYPE_DYNARRAY)
        comp->error.handler(comp->error.context, "Incompatible type in make()");

    int itemSize = typeSize(&comp->types, (*type)->base);
    genPushIntConst(&comp->gen, itemSize);

    lexEat(&comp->lex, TOK_COMMA);

    // Dynamic array length
    Type *lenType;
    parseExpr(comp, &lenType, NULL);
    typeAssertCompatible(&comp->types, comp->intType, lenType, false);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->blocks, typeSize(&comp->types, *type));
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_MAKE);
}


// fn append(array: [] type, item: ^type): [] type
static void parseBuiltinAppendCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    if ((*type)->kind != TYPE_DYNARRAY)
        comp->error.handler(comp->error.context, "Incompatible type in append()");

    lexEat(&comp->lex, TOK_COMMA);

    // New item (must always be a pointer, even for value types)
    Type *itemType;
    parseExpr(comp, &itemType, NULL);
    doImplicitTypeConv(comp, (*type)->base, &itemType, NULL, false);
    typeAssertCompatible(&comp->types, (*type)->base, itemType, false);

    if (!typeStructured(itemType))
    {
        // Assignment to an anonymous stack area does not require updating reference counts
        int itemOffset = identAllocStack(&comp->idents, &comp->blocks, typeSize(&comp->types, itemType));
        genPushLocalPtr(&comp->gen, itemOffset);
        genSwapAssign(&comp->gen, itemType->kind, 0);

        genPushLocalPtr(&comp->gen, itemOffset);
    }

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->blocks, typeSize(&comp->types, *type));
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_APPEND);
}


// fn delete(array: [] type, index: int): [] type
static void parseBuiltinDeleteCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    if ((*type)->kind != TYPE_DYNARRAY)
        comp->error.handler(comp->error.context, "Incompatible type in delete()");

    lexEat(&comp->lex, TOK_COMMA);

    // Item index
    Type *indexType;
    parseExpr(comp, &indexType, NULL);
    doImplicitTypeConv(comp, comp->intType, &indexType, NULL, false);
    typeAssertCompatible(&comp->types, comp->intType, indexType, false);

    // Pointer to result (hidden parameter)
    int resultOffset = identAllocStack(&comp->idents, &comp->blocks, typeSize(&comp->types, *type));
    genPushLocalPtr(&comp->gen, resultOffset);

    genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_DELETE);
}


static void parseBuiltinLenCall(Compiler *comp, Type **type, Const *constant)
{
    parseExpr(comp, type, constant);

    switch ((*type)->kind)
    {
        case TYPE_ARRAY:
        {
            if (constant)
                comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

            genPop(&comp->gen);
            genPushIntConst(&comp->gen, (*type)->numItems);
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
                constCallBuiltin(&comp->consts, constant, NULL, comp->strType, BUILTIN_LEN);
            else
                genCallBuiltin(&comp->gen, TYPE_STR, BUILTIN_LEN);
            break;
        }
        default: comp->error.handler(comp->error.context, "Incompatible type in len()"); return;
    }

    *type = comp->intType;
}


static void parseBuiltinSizeofCall(Compiler *comp, Type **type, Const *constant)
{
    parseExpr(comp, type, constant);
    int size = typeSize(&comp->types, *type);

    if (constant)
        constant->intVal = size;
    else
    {
        genPop(&comp->gen);
        genPushIntConst(&comp->gen, size);
    }

    *type = comp->intType;
}


static void parseBuiltinSizeofselfCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    if ((*type)->kind != TYPE_INTERFACE)
        comp->error.handler(comp->error.context, "Incompatible type in sizeofself()");

    genCallBuiltin(&comp->gen, TYPE_INTERFACE, BUILTIN_SIZEOFSELF);
    *type = comp->intType;
}


// type FiberFunc = fn(parent: ^fiber, anyParam: ^type)
// fn fiberspawn(childFunc: FiberFunc, anyParam: ^type): ^fiber
// fn fibercall(child: ^fiber)
// fn fiberalive(child: ^fiber)
static void parseBuiltinFiberCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    if (constant)
        comp->error.handler(comp->error.context, "Function is not allowed in constant expressions");

    if (builtin == BUILTIN_FIBERSPAWN)
    {
        // Parent fiber pointer
        parseExpr(comp, type, constant);
        if (!typeFiberFunc(*type))
            comp->error.handler(comp->error.context, "Incompatible function type in fiberspawn()");

        lexEat(&comp->lex, TOK_COMMA);

        // Arbitrary pointer parameter
        parseExpr(comp, type, constant);
        doImplicitTypeConv(comp, comp->ptrVoidType, type, constant, false);
        typeAssertCompatible(&comp->types, comp->ptrVoidType, *type, false);

        // Increase parameter's reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, *type);

        *type = comp->ptrFiberType;
    }
    else    // BUILTIN_FIBERCALL, BUILTIN_FIBERALIVE
    {
        parseExpr(comp, type, constant);
        doImplicitTypeConv(comp, comp->ptrFiberType, type, constant, false);
        typeAssertCompatible(&comp->types, comp->ptrFiberType, *type, false);

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
    genPushGlobalPtr(&comp->gen, *type);

    genCallBuiltin(&comp->gen, TYPE_STR, BUILTIN_REPR);
    *type = comp->strType;
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
        case BUILTIN_APPEND:        parseBuiltinAppendCall(comp, type, constant);           break;
        case BUILTIN_DELETE:        parseBuiltinDeleteCall(comp, type, constant);           break;
        case BUILTIN_LEN:           parseBuiltinLenCall(comp, type, constant);              break;
        case BUILTIN_SIZEOF:        parseBuiltinSizeofCall(comp, type, constant);           break;
        case BUILTIN_SIZEOFSELF:    parseBuiltinSizeofselfCall(comp, type, constant);       break;

        // Fibers
        case BUILTIN_FIBERSPAWN:
        case BUILTIN_FIBERCALL:
        case BUILTIN_FIBERALIVE:    parseBuiltinFiberCall(comp, type, constant, builtin);   break;

        // Misc
        case BUILTIN_REPR:          parseBuiltinReprCall(comp, type, constant);             break;
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
    if (typeStructured((*type)->sig.resultType[0]))
        numPostHiddenParams++;

    if (comp->lex.tok.kind != TOK_RPAR)
    {
        while (1)
        {
            if (numPreHiddenParams + numExplicitParams + numPostHiddenParams > (*type)->sig.numParams - 1)
                comp->error.handler(comp->error.context, "Too many actual parameters");

            Type *formalParamType = (*type)->sig.param[i]->type;
            Type *actualParamType;

            parseExpr(comp, &actualParamType, constant);

            doImplicitTypeConv(comp, formalParamType, &actualParamType, constant, false);
            typeAssertCompatible(&comp->types, formalParamType, actualParamType, false);

            // Increase parameter's reference count
            genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, formalParamType);

            // Copy structured parameter if passed by value
            if (typeStructured(formalParamType))
                genPushStruct(&comp->gen, typeSize(&comp->types, formalParamType));

            numExplicitParams++;
            i++;

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }

    if (numPreHiddenParams + numExplicitParams + numPostHiddenParams + (*type)->sig.numDefaultParams < (*type)->sig.numParams)
        comp->error.handler(comp->error.context, "Too few actual parameters");

    // Push default parameters, if not specified explicitly
    while (i < (*type)->sig.numParams - numPostHiddenParams)
    {
        doPushConst(comp, (*type)->sig.param[i]->type, &((*type)->sig.param[i]->defaultVal));
        i++;
    }

    // Push __result pointer
    if (typeStructured((*type)->sig.resultType[0]))
    {
        int size = typeSize(&comp->types, (*type)->sig.resultType[0]);
        int offset = identAllocStack(&comp->idents, &comp->blocks, size);
        genPushLocalPtr(&comp->gen, offset);
        i++;
    }

    int paramSlots = typeParamSizeTotal(&comp->types, &(*type)->sig) / sizeof(Slot);
    genCall(&comp->gen, paramSlots);

    *type = (*type)->sig.resultType[0];
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
    doImplicitTypeConv(comp, *type, &originalType, constant, false);

    if (!typeEquivalent(*type, originalType)                           &&
        !(typeCastable(*type)       && typeCastable(originalType))     &&
        !((*type)->kind == TYPE_PTR && originalType->kind == TYPE_PTR) &&
        !(typeCharArrayPtr(*type)   && originalType->kind == TYPE_STR) &&
        !((*type)->kind == TYPE_STR && typeCharArrayPtr(originalType)))
        comp->error.handler(comp->error.context, "Invalid type cast");

    lexEat(&comp->lex, TOK_RPAR);
}


// arrayLiteral     = "{" [expr {"," expr}] "}".
// structLiteral    = "{" [[ident ":"] expr {"," [ident ":"] expr}] "}".
static void parseArrayOrStructLiteral(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LBRACE);

    int bufOffset = 0;
    if (constant)
    {
        constant->ptrVal = (int64_t)&comp->storage.data[comp->storage.len];
        comp->storage.len += typeSize(&comp->types, *type);
    }
    else
        bufOffset = identAllocStack(&comp->idents, &comp->blocks, typeSize(&comp->types, *type));

    int numItems = 0, itemOffset = 0;
    if (comp->lex.tok.kind != TOK_RBRACE)
    {
        while (1)
        {
            if (numItems > (*type)->numItems - 1)
                comp->error.handler(comp->error.context, "Too many elements in literal");

            if (!constant)
                genPushLocalPtr(&comp->gen, bufOffset + itemOffset);

            Type *expectedItemType = (*type)->kind == TYPE_ARRAY ? (*type)->base : (*type)->field[numItems]->type;
            Type *itemType;
            Const itemConstantBuf, *itemConstant = constant ? &itemConstantBuf : NULL;
            int itemSize = typeSize(&comp->types, expectedItemType);

            // [ident ":"]
            if ((*type)->kind == TYPE_STRUCT && comp->lex.tok.kind == TOK_IDENT)
            {
                Lexer lookaheadLex = comp->lex;
                lexNext(&lookaheadLex);

                if (lookaheadLex.tok.kind == TOK_COLON)
                {
                    Field *field = typeAssertFindField(&comp->types, *type, comp->lex.tok.name);
                    if (field->offset != itemOffset)
                        comp->error.handler(comp->error.context, "Wrong field position in literal");

                    lexNext(&comp->lex);
                    lexEat(&comp->lex, TOK_COLON);
                }
            }

            // expr
            parseExpr(comp, &itemType, itemConstant);

            doImplicitTypeConv(comp, expectedItemType, &itemType, itemConstant, false);
            typeAssertCompatible(&comp->types, expectedItemType, itemType, false);

            if (constant)
                constAssign(&comp->consts, (void *)(constant->ptrVal + itemOffset), itemConstant, expectedItemType->kind, itemSize);
            else
                // Assignment to an anonymous stack area does not require updating reference counts
                genAssign(&comp->gen, expectedItemType->kind, itemSize);

            numItems++;
            itemOffset += itemSize;

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }
    if (numItems < (*type)->numItems)
        comp->error.handler(comp->error.context, "Too few elements in literal");

    if (!constant)
        genPushLocalPtr(&comp->gen, bufOffset);

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


// compositeLiteral = arrayLiteral | structLiteral | fnLiteral.
static void parseCompositeLiteral(Compiler *comp, Type **type, Const *constant)
{
    if ((*type)->kind == TYPE_ARRAY || (*type)->kind == TYPE_STRUCT)
        parseArrayOrStructLiteral(comp, type, constant);
    else if ((*type)->kind == TYPE_FN)
        parseFnLiteral(comp, type, constant);
    else
        comp->error.handler(comp->error.context, "Composite literals are only allowed for arrays, structures and functions");
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

    if ((*type)->base->kind == TYPE_PTR)
    {
        if ((*type)->base->base->kind == TYPE_VOID || (*type)->base->base->kind == TYPE_NULL)
            comp->error.handler(comp->error.context, "Typed pointer expected");

        genDeref(&comp->gen, TYPE_PTR);
        *type = (*type)->base;
    }
    // Accept type-cast lvalues like ^T(x)^ which are not variables and don't need to be dereferenced, so just skip the selector
    else if (*isVar)
       comp->error.handler(comp->error.context, "Typed pointer expected");

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
       ((*type)->base->kind == TYPE_ARRAY || (*type)->base->kind == TYPE_DYNARRAY || (*type)->base->kind == TYPE_STR))
        *type = (*type)->base;

    if ((*type)->kind != TYPE_ARRAY && (*type)->kind != TYPE_DYNARRAY && (*type)->kind != TYPE_STR)
        comp->error.handler(comp->error.context, "Array or string expected");

    // Index
    lexNext(&comp->lex);
    Type *indexType;
    parseExpr(comp, &indexType, NULL);
    typeAssertCompatible(&comp->types, comp->intType, indexType, false);
    lexEat(&comp->lex, TOK_RBRACKET);

    if ((*type)->kind == TYPE_DYNARRAY)
    {
        genGetDynArrayPtr(&comp->gen);
    }
    else if ((*type)->kind == TYPE_STR)
    {
        genPushIntConst(&comp->gen, -1);                     // Use actual length for range checking
        genGetArrayPtr(&comp->gen, typeSize(&comp->types, comp->charType));
    }
    else // TYPE_ARRAY
    {
        genPushIntConst(&comp->gen, (*type)->numItems);      // Use nominal length for range checking
        genGetArrayPtr(&comp->gen, typeSize(&comp->types, (*type)->base));
    }

    if ((*type)->kind == TYPE_STR)
        *type = typeAddPtrTo(&comp->types, &comp->blocks, comp->charType);
    else if (typeStructured((*type)->base))
        *type = (*type)->base;
    else
        *type = typeAddPtrTo(&comp->types, &comp->blocks, (*type)->base);

    *isVar = true;
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

    if ((*type)->kind == TYPE_PTR && typeStructured((*type)->base))
        *type = (*type)->base;

    lexNext(&comp->lex);
    lexCheck(&comp->lex, TOK_IDENT);

    Type *rcvType = *type;
    int rcvTypeModule = rcvType->typeIdent ? rcvType->typeIdent->module : -1;

    if (typeStructured(rcvType))
        rcvType = typeAddPtrTo(&comp->types, &comp->blocks, rcvType);

    Ident *method = identFind(&comp->idents, &comp->modules, &comp->blocks,
                               rcvTypeModule, comp->lex.tok.name, rcvType);
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
            comp->error.handler(comp->error.context, "Structure expected");

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

    // All temporary reals are 64-bit
    if ((*type)->kind == TYPE_REAL32)
        *type = comp->realType;

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

                // All temporary reals are 64-bit
                if ((*type)->kind == TYPE_REAL32)
                    *type = comp->realType;
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
                constant->ptrVal = (int64_t)comp->lex.tok.strVal;
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

            Type *rightType;
            parseRelation(comp, &rightType, NULL);
            doApplyOperator(comp, type, &rightType, NULL, NULL, op, false, true);

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

            Type *rightType;
            parseLogicalTerm(comp, &rightType, NULL);
            doApplyOperator(comp, type, &rightType, NULL, NULL, op, false, true);

            genShortCircuitEpilog(&comp->gen);
        }
    }
}
