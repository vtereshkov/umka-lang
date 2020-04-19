#include <string.h>

#include "umka_expr.h"


void doPushConst(Compiler *comp, Type *type, Const *constant)
{
    if (type->kind == TYPE_FN)
        genPushIntConst(&comp->gen, (int64_t)constant->ptrVal);
    else if (typeReal(type))
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


void doImplicitTypeConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs)
{
    if (dest->kind == TYPE_REAL && typeInteger(*src))
    {
        BuiltinFunc builtin = lhs ? BUILTIN_REAL_LHS : BUILTIN_REAL;
        if (constant)
            constCallBuiltin(&comp->consts, constant, builtin);
        else
            genCallBuiltin(&comp->gen, TYPE_INT, builtin);

        *src = dest;
    }
}


void doApplyOperator(Compiler *comp, Type **type, Type **rightType, Const *constant, Const *rightConstant, TokenKind op, bool apply)
{
    doImplicitTypeConv(comp, *type, rightType, rightConstant, false);
    doImplicitTypeConv(comp, *rightType, type, constant, true);

    typeAssertCompatible(&comp->types, *type, *rightType);
    typeAssertValidOperator(&comp->types, *type, op);

    if (apply)
    {
        if (constant)
            constBinary(&comp->consts, constant, rightConstant, op, (*type)->kind);
        else
            genBinary(&comp->gen, op, (*type)->kind);
    }
}


static void parseBuiltinIOCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    lexEat(&comp->lex, TOK_LPAR);

    if (constant)
        comp->error("Function is not allowed in constant expressions");

    // File pointer
    if (builtin == BUILTIN_FPRINTF || builtin == BUILTIN_FSCANF)
    {
        parseExpr(comp, type, constant);
        typeAssertCompatible(&comp->types, *type, comp->ptrVoidType);
        genPopReg(&comp->gen, VM_IO_FILE_REG);
        lexEat(&comp->lex, TOK_COMMA);
    }

    // Format string
    parseExpr(comp, type, constant);
    typeAssertCompatible(&comp->types, *type, comp->stringType);
    genPopReg(&comp->gen, VM_IO_FORMAT_REG);

    // Values, if any
    while (comp->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&comp->lex);
        parseExpr(comp, type, constant);

        if (builtin == BUILTIN_PRINTF || builtin == BUILTIN_FPRINTF)
        {
            if (typeOrdinal(*type))
                genCallBuiltin(&comp->gen, TYPE_INT, builtin);
            else if (typeReal(*type))
                genCallBuiltin(&comp->gen, TYPE_REAL, builtin);
            else if (typeString(*type))
                genCallBuiltin(&comp->gen, TYPE_PTR, builtin);
            else
                comp->error("Incompatible type in printf()");
        }
        else  // BUILTIN_SCANF, BUILTIN_FSCANF
        {
            typeAssertCompatible(&comp->types, *type, comp->ptrVoidType);

            if (typeOrdinal((*type)->base) || typeReal((*type)->base))
                genCallBuiltin(&comp->gen, (*type)->base->kind, builtin);
            else if (typeString((*type)->base))
                genCallBuiltin(&comp->gen, TYPE_PTR, builtin);
            else
                comp->error("Incompatible type in scanf()");
        }
        genPop(&comp->gen); // Manually remove parameter

    } // while

    // The rest of format string
    genPushIntConst(&comp->gen, 0);
    genCallBuiltin(&comp->gen, TYPE_VOID, builtin);
    genPop(&comp->gen);  // Manually remove parameter

    *type = comp->voidType;
    lexEat(&comp->lex, TOK_RPAR);
}


static void parseBuiltinMathCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    lexEat(&comp->lex, TOK_LPAR);
    parseExpr(comp, type, constant);
    doImplicitTypeConv(comp, comp->realType, type, constant, false);
    typeAssertCompatible(&comp->types, *type, comp->realType);

    if (constant)
        constCallBuiltin(&comp->consts, constant, builtin);
    else
        genCallBuiltin(&comp->gen, TYPE_REAL, builtin);

    if (builtin == BUILTIN_ROUND || builtin == BUILTIN_TRUNC)
        *type = comp->intType;
    else
        *type = comp->realType;
    lexEat(&comp->lex, TOK_RPAR);
}


// builtinCall = ident "(" [expr {"," expr}] ")".
static void parseBuiltinCall(Compiler *comp, Type **type, Const *constant, BuiltinFunc builtin)
{
    switch (builtin)
    {
        // I/O functions
        case BUILTIN_PRINTF:
        case BUILTIN_FPRINTF:
        case BUILTIN_SCANF:
        case BUILTIN_FSCANF:   parseBuiltinIOCall(comp, type, constant, builtin); break;

        // Math functions
        case BUILTIN_ROUND:
        case BUILTIN_TRUNC:
        case BUILTIN_FABS:
        case BUILTIN_SQRT:
        case BUILTIN_SIN:
        case BUILTIN_COS:
        case BUILTIN_ATAN:
        case BUILTIN_EXP:
        case BUILTIN_LOG:      parseBuiltinMathCall(comp, type, constant, builtin); break;

        default: comp->error("Illegal built-in function");
    }
}


// call = designator "(" [expr {"," expr}] ")".
static void parseCall(Compiler *comp, Type **type, Const *constant)
{
    lexEat(&comp->lex, TOK_LPAR);

    if (constant)
        comp->error("Function is not allowed in constant expressions");

    // Actual parameters
    int i = 0;
    if (comp->lex.tok.kind != TOK_RPAR)
    {
        while (1)
        {
            if (i > (*type)->sig.numParams - 1)
                comp->error("Too many actual parameters");

            Type *formalParamType = (*type)->sig.param[i]->type;
            Type *actualParamType;

            parseExpr(comp, &actualParamType, constant);

            doImplicitTypeConv(comp, formalParamType, &actualParamType, constant, false);
            typeAssertCompatible(&comp->types, actualParamType, formalParamType);

            // Copy structured parameter if passed by value
            if (formalParamType->kind == TYPE_ARRAY || formalParamType->kind == TYPE_STRUCT)
                genPushStruct(&comp->gen, typeSize(&comp->types, formalParamType));

            i++;
            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }

    if (i < (*type)->sig.numParams)
        comp->error("Too few actual parameters");

    int paramSlots = typeParamSizeTotal(&comp->types, &(*type)->sig) / sizeof(Slot);
    genCall(&comp->gen, paramSlots);

    *type = (*type)->sig.resultType[0];
    lexEat(&comp->lex, TOK_RPAR);
}


// primary     = ident | typeCast | builtinCall.
// typeCast    = ident "(" expr ")".
static void parsePrimary(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    lexCheck(&comp->lex, TOK_IDENT);
    Ident *ident = identAssertFind(&comp->idents, &comp->blocks, comp->lex.tok.name);

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
            *type = typeAddPtrTo(&comp->types, &comp->blocks, ident->type);
            *isVar = true;
            *isCall = false;
            lexNext(&comp->lex);
            break;
        }

        // Type cast
        case IDENT_TYPE:
        {
            lexNext(&comp->lex);
            lexEat(&comp->lex, TOK_LPAR);
            Type *originalType;
            parseExpr(comp, &originalType, constant);
            lexEat(&comp->lex, TOK_RPAR);

            *type = ident->type;
            *isVar = false;
            *isCall = false;
            break;
        }

        // Built-in function call
        case IDENT_BUILTIN_FN:
        {
            lexNext(&comp->lex);
            parseBuiltinCall(comp, type, constant, ident->builtin);
            *type = ident->type;
            *isVar = false;
            *isCall = true;
            break;
        }
    }
}


// selectors = {"^" | "[" expr "]" | "." ident | "(" actualParams ")"}.
static void parseSelectors(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    while (comp->lex.tok.kind == TOK_CARET  || comp->lex.tok.kind == TOK_LBRACKET ||
           comp->lex.tok.kind == TOK_PERIOD || comp->lex.tok.kind == TOK_LPAR)
    {
        if (constant)
            comp->error("%s is not allowed for constants", lexSpelling(comp->lex.tok.kind));

        switch (comp->lex.tok.kind)
        {
            // Pointer dereference
            case TOK_CARET:
            {
                if ((*type)->kind != TYPE_PTR || (*type)->base->kind != TYPE_PTR || (*type)->base->base->kind == TYPE_VOID)
                    comp->error("Typed pointer expected");

                lexNext(&comp->lex);
                genDeref(&comp->gen, TYPE_PTR);
                *type = (*type)->base;
                *isVar = true;
                *isCall = false;
                break;
            }

            // Array element
            case TOK_LBRACKET:
            {
                if ((*type)->kind != TYPE_PTR || (*type)->base->kind != TYPE_ARRAY)
                    comp->error("Array expected");

                // Index expression
                lexNext(&comp->lex);
                Type *indexType;
                parseExpr(comp, &indexType, NULL);
                typeAssertCompatible(&comp->types, indexType, comp->intType);
                lexEat(&comp->lex, TOK_RBRACKET);

                genGetArrayPtr(&comp->gen, typeSize(&comp->types, (*type)->base->base));
                *type = typeAddPtrTo(&comp->types, &comp->blocks, (*type)->base->base);
                *isVar = true;
                *isCall = false;
                break;
            }

            // Structure field
            case TOK_PERIOD:
            {
                if ((*type)->kind != TYPE_PTR || (*type)->base->kind != TYPE_STRUCT)
                    comp->error("Structure expected");

                // Field
                lexNext(&comp->lex);
                lexCheck(&comp->lex, TOK_IDENT);
                Field *field = typeAssertFindField(&comp->types, (*type)->base, comp->lex.tok.name);
                lexNext(&comp->lex);

                genPushIntConst(&comp->gen, field->offset);
                genBinary(&comp->gen, TOK_PLUS, TYPE_INT);
                *type = typeAddPtrTo(&comp->types, &comp->blocks, field->type);
                *isVar = true;
                *isCall = false;
                break;
            }

            // Function call
            case TOK_LPAR:
            {
                if ((*type)->kind != TYPE_FN)
                    comp->error("Function expected");

                parseCall(comp, type, constant);

                if ((*type)->kind != TYPE_VOID)
                    genPushReg(&comp->gen, VM_RESULT_REG_0);

                // No direct support for 32-bit reals
                if ((*type)->kind == TYPE_REAL32)
                    *type = comp->realType;

                if (typeDefaultRef(*type))
                {
                    *type = typeAddPtrTo(&comp->types, &comp->blocks, *type);
                    *isVar = true;
                }
                else
                    *isVar = false;
                *isCall = true;
                break;
            }

        default: break;
        } // switch
    } // while
}


// designator = primary selectors.
void parseDesignator(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall)
{
    parsePrimary(comp, type, constant, isVar, isCall);
    parseSelectors(comp, type, constant, isVar, isCall);
}


// factor = designator | intNumber | realNumber | charLiteral | stringLiteral |
//          ("+" | "-" | "!" | "~" | "&") factor | "(" expr ")".
static void parseFactor(Compiler *comp, Type **type, Const *constant)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_IDENT:
        {
            bool isVar, isCall;
            parseDesignator(comp, type, constant, &isVar, &isCall);
            if (isVar)
            {
                genDeref(&comp->gen, (*type)->base->kind);
                *type = (*type)->base;

                // No direct support for 32-bit reals
                if ((*type)->kind == TYPE_REAL32)
                    *type = comp->realType;
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

            typeAdd(&comp->types, &comp->blocks, TYPE_ARRAY);
            comp->types.last->base = comp->charType;
            comp->types.last->numItems = strlen(comp->lex.tok.strVal) + 1;
            *type = typeAddPtrTo(&comp->types, &comp->blocks, comp->types.last);
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

            bool isVar, isCall;
            parseDesignator(comp, type, constant, &isVar, &isCall);
            if ((*type)->kind != TYPE_PTR || (*type)->base->kind == TYPE_VOID)
                comp->error("Variable expected");

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
        doApplyOperator(comp, type, &rightType, constant, rightConstant, op, true);
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
        doApplyOperator(comp, type, &rightType, constant, rightConstant, op, true);
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
        doApplyOperator(comp, type, &rightType, constant, rightConstant, op, true);

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
                doApplyOperator(comp, type, &rightType, constant, rightConstant, op, false);
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
            doApplyOperator(comp, type, &rightType, NULL, NULL, op, false);

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
                doApplyOperator(comp, type, &rightType, constant, rightConstant, op, false);
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
            doApplyOperator(comp, type, &rightType, NULL, NULL, op, false);

            genShortCircuitEpilog(&comp->gen);
        }
    }
}
