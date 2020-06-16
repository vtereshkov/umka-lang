#include <string.h>
#include <limits.h>
#include <stddef.h>

#include "umka_expr.h"
#include "umka_decl.h"


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
        constCallBuiltin(&comp->consts, constant, builtin);
    else
        genCallBuiltin(&comp->gen, TYPE_INT, builtin);

    *src = dest;
}


static void doDynArrayPtrToArrayPtrConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error("Conversion to array is not allowed in constant expressions");

    genGetFieldPtr(&comp->gen, offsetof(DynArray, data));
    genDeref(&comp->gen, TYPE_PTR);

    *src = dest;
}


static void doArrayToDynArrayConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error("Conversion to array is not allowed in constant expressions");

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


static void doConcreteToInterfaceConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error("Conversion to interface is not allowed in constant expressions");

    Type *rcvType = *src;
    if (typeStructured(rcvType))
        rcvType = typeAddPtrTo(&comp->types, &comp->blocks, rcvType);

    int destSize   = typeSize(&comp->types, dest);
    int destOffset = identAllocStack(&comp->idents, &comp->blocks, destSize);

    // Assign __self (offset 0)
    // Assignment to an anonymous stack area does not require updating reference counts
    genPushLocalPtr(&comp->gen, destOffset);                    // Push dest pointer
    genSwapAssignOfs(&comp->gen, 0);                            // Assign to dest with zero offset

    // Assign __selftype (RTTI)
    Field *__selftype = typeAssertFindField(&comp->types, dest, "__selftype");
    genPushGlobalPtr(&comp->gen, rcvType);                      // Push src type pointer
    genPushLocalPtr(&comp->gen, destOffset);                    // Push dest pointer
    genSwapAssignOfs(&comp->gen, __selftype->offset);           // Assign to dest with offset

    // Assign methods
    for (int i = 2; i < dest->numItems; i++)
    {
        char *name = dest->field[i]->name;
        Ident *srcMethod = identFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, name, rcvType);
        if (!srcMethod)
            comp->error("Method %s is not implemented", name);

        typeAssertCompatible(&comp->types, dest->field[i]->type, srcMethod->type, false);

        genPushLocalPtr(&comp->gen, destOffset);                // Push dest pointer
        genPushIntConst(&comp->gen, srcMethod->offset);         // Push src value
        genAssignOfs(&comp->gen, dest->field[i]->offset);       // Assign to dest with non-zero offset
    }

    genPushLocalPtr(&comp->gen, destOffset);
    *src = dest;
}


static void doInterfaceToInterfaceConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error("Conversion to interface is not allowed in constant expressions");

    int destSize = typeSize(&comp->types, dest);
    int destOffset = identAllocStack(&comp->idents, &comp->blocks, destSize);

    // Assign __self (offset 0)
    // Assignment to an anonymous stack area does not require updating reference counts
    genDup(&comp->gen);                                         // Duplicate src pointer
    genDeref(&comp->gen, TYPE_PTR);                             // Get __self value
    genPushLocalPtr(&comp->gen, destOffset);                    // Push dest pointer
    genSwapAssignOfs(&comp->gen, 0);                            // Assign to dest with zero offset

    // Assign __selftype (RTTI)
    Field *__selftype = typeAssertFindField(&comp->types, dest, "__selftype");
    genDup(&comp->gen);                                         // Duplicate src pointer
    genGetFieldPtr(&comp->gen, __selftype->offset);             // Get src __selftype pointer
    genDeref(&comp->gen, TYPE_PTR);                             // Get src __selftype
    genPushLocalPtr(&comp->gen, destOffset);                    // Push dest pointer
    genSwapAssignOfs(&comp->gen, __selftype->offset);           // Assign to dest with offset

    // Assign methods
    for (int i = 2; i < dest->numItems; i++)
    {
        char *name = dest->field[i]->name;
        Field *srcMethod = typeFindField(*src, name);
        if (!srcMethod)
            comp->error("Method %s is not implemented", name);

        typeAssertCompatible(&comp->types, dest->field[i]->type, srcMethod->type, false);

        genDup(&comp->gen);                                     // Duplicate src pointer
        genGetFieldPtr(&comp->gen, srcMethod->offset);          // Get src method entry point
        genDeref(&comp->gen, TYPE_PTR);                         // Get method entry point
        genPushLocalPtr(&comp->gen, destOffset);                // Push dest pointer
        genSwapAssignOfs(&comp->gen, dest->field[i]->offset);   // Assign to dest with non-zero offset
    }

    genPop(&comp->gen);                                         // Remove src pointer
    genPushLocalPtr(&comp->gen, destOffset);
    *src = dest;
}


static void doInterfaceToConcreteConv(Compiler *comp, Type *dest, Type **src, Const *constant)
{
    if (constant)
        comp->error("Conversion from interface is not allowed in constant expressions");

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
    else if (dest->kind == TYPE_INTERFACE && ((*src)->kind == TYPE_PTR || typeStructured(*src)))
    {
        if ((*src)->kind == TYPE_INTERFACE)
        {
            if (!typeEquivalent(dest, *src))
                doInterfaceToInterfaceConv(comp, dest, src, constant);
        }
        else
            doConcreteToInterfaceConv(comp, dest, src, constant);
    }

    // Interface to concrete (type assertion)
    else if (dest->kind == TYPE_PTR && (*src)->kind == TYPE_INTERFACE)
    {
        doInterfaceToConcreteConv(comp, dest, src, constant);
    }
}


static void doApplyStrCat(Compiler *comp, Const *constant, Const *rightConstant)
{
    if (constant)
    {
        char *buf = &comp->storage.data[comp->storage.len];
        comp->storage.len += DEFAULT_STR_LEN + 1;
        strcpy(buf, constant->ptrVal);
        constant->ptrVal = buf;
        constBinary(&comp->consts, constant, rightConstant, TOK_PLUS, TYPE_STR);
    }
    else
    {
        int bufOffset = identAllocStack(&comp->idents, &comp->blocks, DEFAULT_STR_LEN + 1);
        genBinary(&comp->gen, TOK_PLUS, TYPE_STR, bufOffset);
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
        comp->error("Function is not allowed in constant expressions");

    // Count (number of characters for printf(), number of items for scanf())
    genPushIntConst(&comp->gen, 0);
    genPopReg(&comp->gen, VM_IO_COUNT_REG);

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
        genPopReg(&comp->gen, VM_IO_STREAM_REG);
        lexEat(&comp->lex, TOK_COMMA);
    }

    // Format string
    parseExpr(comp, type, constant);
    typeAssertCompatible(&comp->types, comp->strType, *type, false);
    genPopReg(&comp->gen, VM_IO_FORMAT_REG);

    // Values, if any
    while (comp->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&comp->lex);
        parseExpr(comp, type, constant);

        if (builtin == BUILTIN_PRINTF || builtin == BUILTIN_FPRINTF || builtin == BUILTIN_SPRINTF)
        {
            if (typeOrdinal(*type))
                genCallBuiltin(&comp->gen, TYPE_INT, builtin);
            else if (typeReal(*type))
                genCallBuiltin(&comp->gen, TYPE_REAL, builtin);
            else if ((*type)->kind == TYPE_STR)
                genCallBuiltin(&comp->gen, TYPE_STR, builtin);
            else
                comp->error("Incompatible type in printf()");
        }
        else  // BUILTIN_SCANF, BUILTIN_FSCANF
        {
            if ((*type)->kind == TYPE_PTR && (typeOrdinal((*type)->base) || typeReal((*type)->base)))
                genCallBuiltin(&comp->gen, (*type)->base->kind, builtin);
            else if ((*type)->base->kind == TYPE_STR)
                genCallBuiltin(&comp->gen, TYPE_STR, builtin);
            else
                comp->error("Incompatible type in scanf()");
        }
        genPop(&comp->gen); // Manually remove parameter

    } // while

    // The rest of format string
    genPushIntConst(&comp->gen, 0);
    genCallBuiltin(&comp->gen, TYPE_VOID, builtin);
    genPop(&comp->gen);  // Manually remove parameter

    // Result
    genPushReg(&comp->gen, VM_IO_COUNT_REG);

    *type = comp->intType;
}


static void parseBuiltinMathCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    parseExpr(comp, type, constant);
    doImplicitTypeConv(comp, comp->realType, type, constant, false);
    typeAssertCompatible(&comp->types, comp->realType, *type, false);

    if (constant)
        constCallBuiltin(&comp->consts, constant, builtin);
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
        comp->error("Function is not allowed in constant expressions");

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
        comp->error("Function is not allowed in constant expressions");

    // Dynamic array type and item size
    *type = parseType(comp, NULL);
    if ((*type)->kind != TYPE_DYNARRAY)
        comp->error("Incompatible type in make()");

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
        comp->error("Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    if ((*type)->kind != TYPE_DYNARRAY)
        comp->error("Incompatible type in append()");

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
        comp->error("Function is not allowed in constant expressions");

    // Dynamic array
    parseExpr(comp, type, NULL);
    if ((*type)->kind != TYPE_DYNARRAY)
        comp->error("Incompatible type in delete()");

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
                comp->error("Function is not allowed in constant expressions");

            genPop(&comp->gen);
            genPushIntConst(&comp->gen, (*type)->numItems);
            break;
        }
        case TYPE_DYNARRAY:
        {
            if (constant)
                comp->error("Function is not allowed in constant expressions");

            genCallBuiltin(&comp->gen, TYPE_DYNARRAY, BUILTIN_LEN);
            break;
        }
        case TYPE_STR:
        {
            if (constant)
                constCallBuiltin(&comp->consts, constant, BUILTIN_LEN);
            else
                genCallBuiltin(&comp->gen, TYPE_STR, BUILTIN_LEN);
            break;
        }
        default: comp->error("Incompatible type in len()"); return;
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


// type FiberFunc = fn(parent: ^fiber, anyParam: ^type)
// fn fiberspawn(childFunc: FiberFunc, anyParam: ^type): ^fiber
// fn fibercall(child: ^fiber)
// fn fiberalive(child: ^fiber)
static void parseBuiltinFiberCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    if (constant)
        comp->error("Function is not allowed in constant expressions");

    if (builtin == BUILTIN_FIBERSPAWN)
    {
        // Parent fiber pointer
        parseExpr(comp, type, constant);
        if (!typeFiberFunc(*type))
            comp->error("Incompatible function type in fiberspawn()");

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


static void parseBuiltinErrorCall(Compiler *comp, Type **type, Const *constant)
{
    if (constant)
        comp->error("Function is not allowed in constant expressions");

    parseExpr(comp, type, constant);
    doImplicitTypeConv(comp, comp->strType, type, constant, false);
    typeAssertCompatible(&comp->types, comp->strType, *type, false);

    genCallBuiltin(&comp->gen, TYPE_STR, BUILTIN_ERROR);
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
        case BUILTIN_SSCANF:        parseBuiltinIOCall(comp, type, constant, builtin); break;

        // Math
        case BUILTIN_ROUND:
        case BUILTIN_TRUNC:
        case BUILTIN_FABS:
        case BUILTIN_SQRT:
        case BUILTIN_SIN:
        case BUILTIN_COS:
        case BUILTIN_ATAN:
        case BUILTIN_EXP:
        case BUILTIN_LOG:           parseBuiltinMathCall(comp, type, constant, builtin); break;

        // Memory
        case BUILTIN_NEW:           parseBuiltinNewCall(comp, type, constant); break;
        case BUILTIN_MAKE:          parseBuiltinMakeCall(comp, type, constant); break;
        case BUILTIN_APPEND:        parseBuiltinAppendCall(comp, type, constant); break;
        case BUILTIN_DELETE:        parseBuiltinDeleteCall(comp, type, constant); break;
        case BUILTIN_LEN:           parseBuiltinLenCall(comp, type, constant); break;
        case BUILTIN_SIZEOF:        parseBuiltinSizeofCall(comp, type, constant); break;

        // Fibers
        case BUILTIN_FIBERSPAWN:
        case BUILTIN_FIBERCALL:
        case BUILTIN_FIBERALIVE:    parseBuiltinFiberCall(comp, type, constant, builtin); break;

        // Misc
        case BUILTIN_ERROR:         parseBuiltinErrorCall(comp, type, constant); break;

        default: comp->error("Illegal built-in function");
    }
    lexEat(&comp->lex, TOK_RPAR);
}


// actualParams = "(" [expr {"," expr}] ")".
static void parseCall(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LPAR);

    if (constant)
        comp->error("Function is not allowed in constant expressions");

    // Actual parameters: [__self,] param1, param2 ...[__result]
    int numExplicitParams = 0, numPreHiddenParams = 0, numPostHiddenParams = 0;
    int i = 0;

    // Method receiver
    if ((*type)->sig.method)
    {
        genPushReg(&comp->gen, VM_SELF_REG);

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
                comp->error("Too many actual parameters");

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
        comp->error("Too few actual parameters");

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
                comp->error("Constant expected but variable %s found", ident->name);

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

        default: comp->error("Illegal identifier");
    }
}


// derefSelector = "^".
static void parseDerefSelector(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    if ((*type)->kind != TYPE_PTR || (*type)->base->kind != TYPE_PTR ||
        (*type)->base->base->kind == TYPE_VOID || (*type)->base->base->kind == TYPE_NULL)
        comp->error("Typed pointer expected");

    lexNext(&comp->lex);
    genDeref(&comp->gen, TYPE_PTR);
    *type = (*type)->base;
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

    if ((*type)->kind == TYPE_PTR &&
       ((*type)->base->kind == TYPE_ARRAY || (*type)->base->kind == TYPE_DYNARRAY || (*type)->base->kind == TYPE_STR))
        *type = (*type)->base;

    if ((*type)->kind != TYPE_ARRAY && (*type)->kind != TYPE_DYNARRAY && (*type)->kind != TYPE_STR)
        comp->error("Array or string expected");

    // Index
    lexNext(&comp->lex);
    Type *indexType;
    parseExpr(comp, &indexType, NULL);
    typeAssertCompatible(&comp->types, comp->intType, indexType, false);
    lexEat(&comp->lex, TOK_RBRACKET);

    if ((*type)->kind == TYPE_DYNARRAY)
        genGetDynArrayPtr(&comp->gen);
    else
    {
        genPushIntConst(&comp->gen, (*type)->numItems);                     // Length (for range checking)
        genGetArrayPtr(&comp->gen, typeSize(&comp->types, (*type)->base));
    }

    if (typeStructured((*type)->base))
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
    if (typeStructured(rcvType))
        rcvType = typeAddPtrTo(&comp->types, &comp->blocks, rcvType);

    Ident *method = identFind(&comp->idents, &comp->modules, &comp->blocks,
                               comp->blocks.module, comp->lex.tok.name, rcvType);
    if (method)
    {
        // Method
        lexNext(&comp->lex);

        // Save concrete method's receiver to dedicated register and push method's entry point
        genPopReg(&comp->gen, VM_SELF_REG);
        doPushConst(comp, method->type, &method->constant);

        *type = method->type;
        *isVar = false;
        *isCall = false;
    }
    else
    {
        // Field
        if ((*type)->kind != TYPE_STRUCT && (*type)->kind != TYPE_INTERFACE)
            comp->error("Structure expected");

        Field *field = typeAssertFindField(&comp->types, *type, comp->lex.tok.name);
        lexNext(&comp->lex);

        genGetFieldPtr(&comp->gen, field->offset);

        // Save interface method's receiver to dedicated register and push method's entry point
        if (field->type->kind == TYPE_FN && field->type->sig.method && field->type->sig.offsetFromSelf != 0)
        {
            genDup(&comp->gen);
            genGetFieldPtr(&comp->gen, -field->type->sig.offsetFromSelf);
            genDeref(&comp->gen, TYPE_PTR);
            genPopReg(&comp->gen, VM_SELF_REG);
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
        comp->error("Function expected");

    parseCall(comp, type, constant);

    // Push result
    if ((*type)->kind != TYPE_VOID)
        genPushReg(&comp->gen, VM_RESULT_REG_0);

    // Copy result to a temporary local variable to collect it as garbage when leaving the block
    if (typeGarbageCollected(*type))
        doCopyResultToTempVar(comp, *type);

    // All temporary reals are 64-bit
    if ((*type)->kind == TYPE_REAL32)
        *type = comp->realType;

    if (typeStructured(*type))
        *isVar = true;
    else
        *isVar = false;
    *isCall = true;
}


// selectors = {derefSelector | indexSelector | fieldSelector | callSelector}.
static void parseSelectors(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    while (comp->lex.tok.kind == TOK_CARET  || comp->lex.tok.kind == TOK_LBRACKET ||
           comp->lex.tok.kind == TOK_PERIOD || comp->lex.tok.kind == TOK_LPAR)
    {
        if (constant)
            comp->error("%s is not allowed for constants", lexSpelling(comp->lex.tok.kind));

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


// designator = primary selectors.
void parseDesignator(Compiler *comp, Ident *ident, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    parsePrimary(comp, ident, type, constant, isVar, isCall);
    parseSelectors(comp, type, constant, isVar, isCall);
}


// typeCast = type "(" expr ")".
static void parseTypeCast(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LPAR);

    Type *originalType;
    parseExpr(comp, &originalType, constant);
    doImplicitTypeConv(comp, *type, &originalType, constant, false);

    if (!typeEquivalent(*type, originalType) &&
        !(typeCastable(*type) && typeCastable(originalType)) &&
        !((*type)->kind == TYPE_PTR && originalType->kind == TYPE_PTR))
        comp->error("Invalid type cast");

    lexEat(&comp->lex, TOK_RPAR);
}


// compositeLiteral = arrayLiteral | structLiteral.
// arrayLiteral     = "{" [expr {"," expr}] "}".
// structLiteral    = "{" [ident ":" expr {"," ident ":" expr}] "}".
static void parseCompositeLiteral(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LBRACE);

    if ((*type)->kind != TYPE_ARRAY && (*type)->kind != TYPE_STRUCT)
        comp->error("Composite literal is only allowed for arrays or structures");

    int bufOffset = 0;
    if (constant)
    {
        constant->ptrVal = &comp->storage.data[comp->storage.len];
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
                comp->error("Too many elements in literal");

            if (!constant)
                genPushLocalPtr(&comp->gen, bufOffset + itemOffset);

            Type *expectedItemType = (*type)->kind == TYPE_ARRAY ? (*type)->base : (*type)->field[numItems]->type;
            Type *itemType;
            Const itemConstantBuf, *itemConstant = constant ? &itemConstantBuf : NULL;
            int itemSize = typeSize(&comp->types, expectedItemType);

            // ident ":"
            if ((*type)->kind == TYPE_STRUCT)
            {
                lexCheck(&comp->lex, TOK_IDENT);
                Field *field = typeAssertFindField(&comp->types, *type, comp->lex.tok.name);
                if (field->offset != itemOffset)
                    comp->error("Wrong field position in literal");

                lexNext(&comp->lex);
                lexEat(&comp->lex, TOK_COLON);
            }

            // expr
            parseExpr(comp, &itemType, itemConstant);

            doImplicitTypeConv(comp, expectedItemType, &itemType, itemConstant, false);
            typeAssertCompatible(&comp->types, expectedItemType, itemType, false);

            if (constant)
                constAssign(&comp->consts, constant->ptrVal + itemOffset, itemConstant, expectedItemType->kind, itemSize);
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
        comp->error("Too few elements in literal");

    if (!constant)
        genPushLocalPtr(&comp->gen, bufOffset);

    lexEat(&comp->lex, TOK_RBRACE);
}


static void parseTypeCastOrCompositeLiteral(Compiler *comp, Ident *ident, Type **type, Const *constant)
{
    *type = parseType(comp, ident);

    if (comp->lex.tok.kind == TOK_LPAR)
        parseTypeCast(comp, type, constant);
    else if (comp->lex.tok.kind == TOK_LBRACE)
        parseCompositeLiteral(comp, type, constant);
    else
        comp->error("Type cast or composite literal expected");
}


// factor = designator | intNumber | realNumber | charLiteral | stringLiteral | compositeLiteral | typeCast |
//          ("+" | "-" | "!" | "~" | "&") factor | "(" expr ")".
static void parseFactor(Compiler *comp, Type **type, Const *constant)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_IDENT:
        {
            Ident *ident = parseQualIdent(comp);
            if (ident->kind == IDENT_TYPE)
                parseTypeCastOrCompositeLiteral(comp, ident, type, constant);
            else
            {
                // A designator that isVar is always an addressable quantity
                // (a structured type or a pointer to a value type)

                bool isVar, isCall;
                parseDesignator(comp, ident, type, constant, &isVar, &isCall);
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
            }
            break;
        }

        case TOK_INTNUMBER:
        {
            if (constant)
                constant->intVal = comp->lex.tok.intVal;
            else
                genPushIntConst(&comp->gen, comp->lex.tok.intVal);
            lexNext(&comp->lex);
            *type = comp->intType;
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
                constant->intVal = comp->lex.tok.intVal;
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
            (*type)->base = comp->charType;
            (*type)->numItems = strlen(comp->lex.tok.strVal) + 1;
            break;
        }

        case TOK_CARET:
        case TOK_WEAK:
        case TOK_LBRACKET:
        case TOK_STRUCT:
        {
            parseTypeCastOrCompositeLiteral(comp, NULL, type, constant);
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
                comp->error("Address operator is not allowed in constant expressions");

            lexNext(&comp->lex);

            Ident *ident = parseQualIdent(comp);
            bool isVar, isCall;
            parseDesignator(comp, ident, type, constant, &isVar, &isCall);
            if (!isVar)
                comp->error("Unable to take address");

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

        default: comp->error("Illegal expression");
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
