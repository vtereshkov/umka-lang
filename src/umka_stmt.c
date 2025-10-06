#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <string.h>

#include "umka_stmt.h"
#include "umka_expr.h"
#include "umka_decl.h"


static void parseStmtList(Umka *umka);
static void parseBlock(Umka *umka);


static void doGarbageCollectionAt(Umka *umka, int blockStackPos)
{
    for (const Ident *ident = umka->idents.first; ident; ident = ident->next)
        if (ident->kind == IDENT_VAR && typeGarbageCollected(ident->type) && ident->block == umka->blocks.item[blockStackPos].block && !(ident->temporary && !ident->used) && strcmp(ident->name, "#result") != 0)
        {
            // Skip unused upvalues
            if (strcmp(ident->name, "#upvalues") == 0)
            {
                if (!umka->blocks.item[blockStackPos].fn)
                    umka->error.handler(umka->error.context, "Upvalues can only be declared in the function scope");

                if (!umka->blocks.item[blockStackPos].hasUpvalues)
                    continue;
            }

            if (ident->block == 0)
                genChangeRefCntGlobal(&umka->gen, TOK_MINUSMINUS, ident->ptr, ident->type);
            else
                genChangeRefCntLocal(&umka->gen, TOK_MINUSMINUS, ident->offset, ident->type);
        }
}


void doGarbageCollection(Umka *umka)
{
    // Collect garbage in the current scope
    doGarbageCollectionAt(umka, umka->blocks.top);
}


void doGarbageCollectionDownToBlock(Umka *umka, int block)
{
    // Collect garbage over all scopes down to the specified block (not inclusive)
    for (int i = umka->blocks.top; i >= 1; i--)
    {
        if (umka->blocks.item[i].block == block)
            break;
        doGarbageCollectionAt(umka, i);
    }
}


void doZeroVar(Umka *umka, const Ident *ident)
{
    if (ident->block == 0)
        constZero(ident->ptr, typeSize(&umka->types, ident->type));
    else
    {
        doPushVarPtr(umka, ident);
        genZero(&umka->gen, typeSize(&umka->types, ident->type));
    }
}


void doResolveExtern(Umka *umka)
{
    for (const Ident *ident = umka->idents.first; ident; ident = ident->next)
        if (ident->module == umka->blocks.module)
        {
            if (ident->prototypeOffset >= 0)
            {
                External *external = externalFind(&umka->externals, ident->name);

                // Try to find the function in the external function list or in an external implementation library
                void *fn = NULL;
                if (external)
                {
                    if (external->resolved)
                        umka->error.handler(umka->error.context, "External %s is already resolved", ident->name);

                    if (!umka->lex.hasSourceString || (external->resolveInTrusted && !umka->lex.trusted))
                        umka->error.handler(umka->error.context, "Cannot resolve %s in this module", ident->name);

                    fn = external->entry;
                    external->resolved = true;
                }
                else
                    fn = moduleGetImplLibFunc(umka->modules.module[umka->blocks.module], ident->name);

                if (!fn)
                    umka->error.handler(umka->error.context, "Unresolved prototype of %s", ident->name);

                blocksEnterFn(&umka->blocks, ident, false);
                genEntryPoint(&umka->gen, ident->prototypeOffset);
                genEnterFrameStub(&umka->gen);

                // All parameters must be declared since they may require garbage collection
                for (int i = 0; i < ident->type->sig.numParams; i++)
                    identAllocParam(&umka->idents, &umka->types, &umka->modules, &umka->blocks, &ident->type->sig, i);

                genCallExtern(&umka->gen, fn);

                doGarbageCollection(umka);
                identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
                identFree(&umka->idents, blocksCurrent(&umka->blocks));

                const ParamLayout *paramLayout = typeMakeParamLayout(&umka->types, &ident->type->sig);

                genLeaveFrameFixup(&umka->gen, typeMakeParamAndLocalVarLayout(&umka->types, paramLayout, 0));
                genReturn(&umka->gen, paramLayout->numParamSlots);

                blocksLeave(&umka->blocks);
            }

            identWarnIfUnused(&umka->idents, ident);
        }
}


static bool doShortVarDeclLookahead(Umka *umka)
{
    // ident {"," ident} ":="
    Lexer lookaheadLex = umka->lex;
    while (1)
    {
        if (lookaheadLex.tok.kind != TOK_IDENT)
            return false;

        lexNext(&lookaheadLex);
        if (lookaheadLex.tok.kind != TOK_COMMA)
            break;

        lexNext(&lookaheadLex);
    }
    return lookaheadLex.tok.kind == TOK_COLONEQ;
}


static bool doTypeSwitchStmtLookahead(Umka *umka)
{
    // "switch" ident ":=" "type"
    Lexer lookaheadLex = umka->lex;
    if (lookaheadLex.tok.kind != TOK_SWITCH)
        return false;

    lexNext(&lookaheadLex);
    if (lookaheadLex.tok.kind != TOK_IDENT)
        return false;

    lexNext(&lookaheadLex);
    if (lookaheadLex.tok.kind != TOK_COLONEQ)
        return false;

    lexNext(&lookaheadLex);
    if (lookaheadLex.tok.kind != TOK_TYPE)
        return false;

    return true;
}


static bool doForPostIncDecStmtLookahead(Umka *umka)
{
    // ident ("++" | "--")
    Lexer lookaheadLex = umka->lex;
    if (lookaheadLex.tok.kind != TOK_IDENT)
        return false;

    lexNext(&lookaheadLex);
    if (lookaheadLex.tok.kind != TOK_PLUSPLUS && lookaheadLex.tok.kind != TOK_MINUSMINUS)
        return false;

    return true;
}


// singleAssignmentStmt = designator "=" expr.
static void parseSingleAssignmentStmt(Umka *umka, const Type *type, Const *varPtrConst)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            umka->error.handler(umka->error.context, "Left side cannot be assigned to");
        type = type->base;
    }

    const Type *rightType = type;
    Const rightConstantBuf, *rightConstant = varPtrConst ? &rightConstantBuf : NULL;

    parseExpr(umka, &rightType, rightConstant);

    if (typeExprListStruct(rightType))
        umka->error.handler(umka->error.context, "1 expression expected but %d found", rightType->numItems);

    doAssertImplicitTypeConv(umka, type, &rightType, rightConstant);

    if (varPtrConst)                                // Initialize global variable
        constAssign(&umka->consts, varPtrConst->ptrVal, rightConstant, type->kind, typeSize(&umka->types, type));
    else                                            // Assign to variable
    {
        if (doTryRemoveCopyResultToTempVar(umka))
        {
            // Optimization: if the right-hand side is a function call, assume its reference count to be already increased before return
            // The left-hand side will hold this additional reference, so we can remove the temporary "reference holder" variable
            genChangeLeftRefCntAssign(&umka->gen, type);

        }
        else
        {
            // General case: update reference counts for both sides
            genChangeRefCntAssign(&umka->gen, type);
        }
    }
}


// listAssignmentStmt = designatorList "=" exprList.
static void parseListAssignmentStmt(Umka *umka, const Type *type, Const *varPtrConstList)
{
    Type *derefLeftListType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
    derefLeftListType->isExprList = true;

    for (int i = 0; i < type->numItems; i++)
    {
        const Type *leftType = type->field[i]->type;
        if (!typeStructured(leftType))
        {
            if (leftType->kind != TYPE_PTR || leftType->base->kind == TYPE_VOID)
                umka->error.handler(umka->error.context, "Left side cannot be assigned to");
            leftType = leftType->base;
        }
        typeAddField(&umka->types, derefLeftListType, leftType, NULL);
    }

    const Type *rightListType = derefLeftListType;
    Const rightListConstantBuf, *rightListConstant = varPtrConstList ? &rightListConstantBuf : NULL;
    parseExprList(umka, &rightListType, rightListConstant);

    const int numExpr = typeExprListStruct(rightListType) ? rightListType->numItems : 1;
    if (numExpr != type->numItems)
        umka->error.handler(umka->error.context, "%d expressions expected but %d found", type->numItems, numExpr);

    for (int i = type->numItems - 1; i >= 0; i--)
    {
        const Type *leftType = derefLeftListType->field[i]->type;
        const Type *rightType = rightListType->field[i]->type;

        if (varPtrConstList)                                // Initialize global variables
        {
            Const rightConstantBuf = {.ptrVal = (char *)rightListConstant->ptrVal + rightListType->field[i]->offset};
            constDeref(&umka->consts, &rightConstantBuf, rightType->kind);

            doAssertImplicitTypeConv(umka, leftType, &rightType, &rightConstantBuf);

            constAssign(&umka->consts, varPtrConstList[i].ptrVal, &rightConstantBuf, leftType->kind, typeSize(&umka->types, leftType));
        }
        else                                                // Assign to variable
        {
            genDup(&umka->gen);                                             // Duplicate expression list pointer
            genPopReg(&umka->gen, REG_EXPR_LIST);                           // Save expression list pointer
            genGetFieldPtr(&umka->gen, rightListType->field[i]->offset);    // Get expression pointer
            genDeref(&umka->gen, rightType->kind);                          // Get expression value

            doAssertImplicitTypeConv(umka, leftType, &rightType, NULL);

            genChangeRefCntAssign(&umka->gen, leftType);                    // Assign expression to variable
            genPushReg(&umka->gen, REG_EXPR_LIST);                          // Restore expression list pointer
        }
    }

    if (!varPtrConstList)
        genPop(&umka->gen);                                                 // Remove expression list pointer
}


// assignmentStmt = singleAssignmentStmt | listAssignmentStmt.
void parseAssignmentStmt(Umka *umka, const Type *type, Const *varPtrConstList)
{
    if (typeExprListStruct(type))
        parseListAssignmentStmt(umka, type, varPtrConstList);
    else
        parseSingleAssignmentStmt(umka, type, varPtrConstList);
}


// shortAssignmentStmt = designator ("+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "~=" | "<<=" | ">>=") expr.
static void parseShortAssignmentStmt(Umka *umka, const Type *type, TokenKind op)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            umka->error.handler(umka->error.context, "Left side cannot be assigned to");
        type = type->base;
    }

    // Duplicate designator and treat it as an expression
    genDup(&umka->gen);
    genDeref(&umka->gen, type->kind);

    const Type *leftType = type;
    const Type *rightType = type;
    parseExpr(umka, &rightType, NULL);

    // Keep "+=" for strings as is for better optimizations
    const TokenKind shortOp = (leftType->kind == TYPE_STR && op == TOK_PLUSEQ) ? op : lexShortAssignment(op);

    doApplyOperator(umka, &leftType, &rightType, NULL, NULL, shortOp, true, false);
    genChangeRefCntAssign(&umka->gen, type);
}


// singleDeclAssignmentStmt = ident ":=" expr.
static void parseSingleDeclAssignmentStmt(Umka *umka, IdentName name, bool exported, bool constExpr)
{
    const Type *rightType = NULL;
    Const rightConstantBuf, *rightConstant = constExpr ? &rightConstantBuf : NULL;
    parseExpr(umka, &rightType, rightConstant);

    if (typeExprListStruct(rightType))
        umka->error.handler(umka->error.context, "1 expression expected but %d found", rightType->numItems);

    const Ident *ident = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, name, rightType, exported);

    if (constExpr)              // Initialize global variable
        constAssign(&umka->consts, ident->ptr, rightConstant, rightType->kind, typeSize(&umka->types, rightType));
    else                        // Assign to variable
    {
        if (doTryRemoveCopyResultToTempVar(umka))
        {
            // Optimization: if the right-hand side is a function call, assume its reference count to be already increased before return
            // The left-hand side will hold this additional reference, so we can remove the temporary "reference holder" variable
        }
        else
        {
            // General case: increase right-hand side reference count
            genChangeRefCnt(&umka->gen, TOK_PLUSPLUS, rightType);
        }

        doPushVarPtr(umka, ident);
        genSwapAssign(&umka->gen, rightType->kind, typeSize(&umka->types, rightType));
    }
}


// listDeclAssignmentStmt = identList ":=" exprList.
static void parseListDeclAssignmentStmt(Umka *umka, IdentName *names, const bool *exported, int num, bool constExpr)
{
    const Type *rightListType = NULL;
    Const rightListConstantBuf, *rightListConstant = constExpr ? &rightListConstantBuf : NULL;
    parseExprList(umka, &rightListType, rightListConstant);

    const int numExpr = typeExprListStruct(rightListType) ? rightListType->numItems : 1;
    if (numExpr != num)
        umka->error.handler(umka->error.context, "%d expressions expected but %d found", num, numExpr);

    bool newVarFound = false;

    for (int i = 0; i < num; i++)
    {
        const Type *rightType = rightListType->field[i]->type;

        bool redecl = false;
        const Ident *ident = identFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, names[i], NULL, false);
        if (ident && ident->kind == IDENT_VAR && ident->block == umka->blocks.item[umka->blocks.top].block)
        {
            // Redeclaration in the same block
            redecl = true;
            identSetUsed(ident);
        }
        else
        {
            // New variable
            newVarFound = true;
            ident = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, names[i], rightType, exported[i]);
        }

        if (constExpr)              // Initialize global variable
        {
            Const rightConstantBuf = {.ptrVal = (char *)rightListConstant->ptrVal + rightListType->field[i]->offset};
            constDeref(&umka->consts, &rightConstantBuf, rightType->kind);

            if (redecl)
                doAssertImplicitTypeConv(umka, ident->type, &rightType, &rightConstantBuf);

            constAssign(&umka->consts, ident->ptr, &rightConstantBuf, ident->type->kind, typeSize(&umka->types, ident->type));
        }
        else                        // Assign to variable
        {
            genDup(&umka->gen);                                             // Duplicate expression list pointer
            genGetFieldPtr(&umka->gen, rightListType->field[i]->offset);    // Get expression pointer
            genDeref(&umka->gen, rightType->kind);                          // Get expression value

            if (redecl)
            {
                doAssertImplicitTypeConv(umka, ident->type, &rightType, NULL);

                doPushVarPtr(umka, ident);
                genSwapChangeRefCntAssign(&umka->gen, ident->type);                                 // Assign expression to variable - both left-hand and right-hand side reference counts modified
            }
            else
            {
                genChangeRefCnt(&umka->gen, TOK_PLUSPLUS, rightType);                               // Increase right-hand side reference count
                doPushVarPtr(umka, ident);
                genSwapAssign(&umka->gen, ident->type->kind, typeSize(&umka->types, ident->type));  // Assign expression to variable
            }
        }
    }

    if (!constExpr)
        genPop(&umka->gen);                                                 // Remove expression list pointer

    if (!newVarFound)
        umka->error.handler(umka->error.context, "No new variables declared");
}


// declAssignmentStmt = singleDeclAssignmentStmt | listDeclAssignmentStmt.
void parseDeclAssignmentStmt(Umka *umka, IdentName *names, const bool *exported, int num, bool constExpr)
{
    if (num > 1)
        parseListDeclAssignmentStmt(umka, names, exported, num, constExpr);
    else
        parseSingleDeclAssignmentStmt(umka, names[0], exported[0], constExpr);
}


// incDecStmt = designator ("++" | "--").
static void parseIncDecStmt(Umka *umka, const Type *type, TokenKind op)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            umka->error.handler(umka->error.context, "Left side cannot be assigned to");
        type = type->base;
    }

    typeAssertCompatible(&umka->types, umka->intType, type);
    genUnary(&umka->gen, op, type);
    lexNext(&umka->lex);
}


// simpleStmt = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
// callStmt   = designator.
static void parseSimpleStmt(Umka *umka)
{
    if (doShortVarDeclLookahead(umka))
        parseShortVarDecl(umka);
    else
    {
        const Type *type = NULL;
        bool isVar, isCall, isCompLit;
        parseDesignatorList(umka, &type, NULL, &isVar, &isCall, &isCompLit);

        TokenKind op = umka->lex.tok.kind;

        if (typeExprListStruct(type) && !isCall && op != TOK_EQ)
            umka->error.handler(umka->error.context, "List assignment expected");

        if (op == TOK_EQ || lexShortAssignment(op) != TOK_NONE)
        {
            // Assignment
            if (!isVar || isCall || isCompLit)
                umka->error.handler(umka->error.context, "Left side cannot be assigned to");
            lexNext(&umka->lex);

            if (op == TOK_EQ)
                parseAssignmentStmt(umka, type, NULL);
            else
                parseShortAssignmentStmt(umka, type, op);
        }
        else if (op == TOK_PLUSPLUS || op == TOK_MINUSMINUS)
        {
            // Increment/decrement
            parseIncDecStmt(umka, type, op);
        }
        else
        {
            // Call
            if (!isCall)
                umka->error.handler(umka->error.context, "Assignment or function call expected");
            if (type->kind != TYPE_VOID)
                genPop(&umka->gen);  // Manually remove result
        }
    }
}


// ifStmt = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
static void parseIfStmt(Umka *umka)
{
    lexEat(&umka->lex, TOK_IF);

    // Additional scope embracing shortVarDecl and statement body
    blocksEnter(&umka->blocks);

    // [shortVarDecl ";"]
    if (doShortVarDeclLookahead(umka))
    {
        parseShortVarDecl(umka);
        lexEat(&umka->lex, TOK_SEMICOLON);
    }

    // expr
    const Type *type = umka->boolType;
    parseExpr(umka, &type, NULL);
    typeAssertCompatible(&umka->types, umka->boolType, type);

    genIfCondEpilog(&umka->gen);

    // block
    parseBlock(umka);

    // ["else" (ifStmt | block)]
    if (umka->lex.tok.kind == TOK_ELSE)
    {
        genElseProlog(&umka->gen);
        lexNext(&umka->lex);

        if (umka->lex.tok.kind == TOK_IF)
            parseIfStmt(umka);
        else
            parseBlock(umka);

        genIfElseEpilog(&umka->gen);
    }
    else
    {
        genIfEpilog(&umka->gen);
    }

    // Additional scope embracing shortVarDecl and statement body
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);
}


// exprCase = "case" expr {"," expr} ":" stmtList.
static void parseExprCase(Umka *umka, const Type *selectorType, ConstArray *existingConstants)
{
    lexEat(&umka->lex, TOK_CASE);

    // expr {"," expr}
    int numCaseConstants = 0;

    while (1)
    {
        Const constant;
        const Type *type = selectorType;
        parseExpr(umka, &type, &constant);
        doAssertImplicitTypeConv(umka, selectorType, &type, &constant);

        if (typeOverflow(selectorType->kind, constant))
            umka->error.handler(umka->error.context, "Overflow of %s", typeKindSpelling(selectorType->kind));

        if (constArrayFind(&umka->consts, existingConstants, constant) >= 0)
            umka->error.handler(umka->error.context, "Duplicate case constant");
        constArrayAppend(existingConstants, constant);

        genCaseConstantCheck(&umka->gen, type, &constant);
        numCaseConstants++;

        if (umka->lex.tok.kind != TOK_COMMA)
            break;
        lexNext(&umka->lex);
    }

    // ":" stmtList
    lexEat(&umka->lex, TOK_COLON);

    genCaseBlockProlog(&umka->gen, numCaseConstants);

    // Additional scope embracing stmtList
    blocksEnter(&umka->blocks);

    parseStmtList(umka);

    // Additional scope embracing stmtList
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);

    genCaseBlockEpilog(&umka->gen);
}


// typeCase = "case" type ":" stmtList.
static void parseTypeCase(Umka *umka, const char *concreteVarName, ConstArray *existingConcreteTypes)
{
    lexEat(&umka->lex, TOK_CASE);

    // type
    const Type *concreteType = parseType(umka, NULL);
    if (concreteType->kind == TYPE_INTERFACE)
        umka->error.handler(umka->error.context, "Non-interface type expected");

    Const concreteTypeAsConst = {.ptrVal = (Type *)concreteType};
    if (constArrayFindEquivalentType(&umka->consts, existingConcreteTypes, concreteTypeAsConst) >= 0)
        umka->error.handler(umka->error.context, "Duplicate case type");
    constArrayAppend(existingConcreteTypes, concreteTypeAsConst);

    const Type *concretePtrType = concreteType;
    if (concreteType->kind != TYPE_PTR)
        concretePtrType = typeAddPtrTo(&umka->types, &umka->blocks, concreteType);

    genDup(&umka->gen);                             // Duplicate interface expression
    genAssertType(&umka->gen, concretePtrType);

    genDup(&umka->gen);                             // Duplicate expression converted to the concrete type
    genPushGlobalPtr(&umka->gen, NULL);
    genBinary(&umka->gen, TOK_NOTEQ, concretePtrType);

    genIfCondEpilog(&umka->gen);

    // ":" stmtList
    lexEat(&umka->lex, TOK_COLON);

    // Additional scope embracing stmtList
    blocksEnter(&umka->blocks);

    // Allocate and initialize concrete-type variable
    const Ident *concreteIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, concreteVarName, concreteType, false);
    identSetUsed(concreteIdent);                     // Do not warn about unused concrete variable
    doZeroVar(umka, concreteIdent);

    if (concreteType->kind != TYPE_PTR)
        genDeref(&umka->gen, concreteType->kind);

    doPushVarPtr(umka, concreteIdent);
    genSwapChangeRefCntAssign(&umka->gen, concreteType);

    parseStmtList(umka);

    // Additional scope embracing stmtList
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);

    genElseProlog(&umka->gen);

    genPop(&umka->gen);                 // Remove duplicate interface expression
}


// default = "default" ":" stmtList.
static void parseDefault(Umka *umka)
{
    lexEat(&umka->lex, TOK_DEFAULT);
    lexEat(&umka->lex, TOK_COLON);

    // Additional scope embracing stmtList
    blocksEnter(&umka->blocks);

    parseStmtList(umka);

    // Additional scope embracing stmtList
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);
}


// exprSwitchStmt = "switch" [shortVarDecl ";"] expr "{" {exprCase} [default] "}".
static void parseExprSwitchStmt(Umka *umka)
{
    lexEat(&umka->lex, TOK_SWITCH);

    // Additional scope embracing shortVarDecl and statement body
    blocksEnter(&umka->blocks);

    // [shortVarDecl ";"]
    if (doShortVarDeclLookahead(umka))
    {
        parseShortVarDecl(umka);
        lexEat(&umka->lex, TOK_SEMICOLON);
    }

    // expr
    const Type *type = NULL;
    parseExpr(umka, &type, NULL);
    if (!typeOrdinal(type))
        umka->error.handler(umka->error.context, "Ordinal type expected");

    genSwitchCondEpilog(&umka->gen);

    // "{" {exprCase} "}"
    lexEat(&umka->lex, TOK_LBRACE);

    int numCases = 0;
    ConstArray existingConstants;
    constArrayAlloc(&existingConstants, &umka->storage, type);

    while (umka->lex.tok.kind == TOK_CASE)
    {
        parseExprCase(umka, type, &existingConstants);
        numCases++;
    }

    // [default]
    if (umka->lex.tok.kind == TOK_DEFAULT)
        parseDefault(umka);

    constArrayFree(&existingConstants);

    lexEat(&umka->lex, TOK_RBRACE);

    genSwitchEpilog(&umka->gen, numCases);

    // Additional scope embracing shortVarDecl and statement body
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);
}


// typeSwitchStmt = "switch" ident ":=" "type" "(" expr ")" "{" {typeCase} [default] "}".
static void parseTypeSwitchStmt(Umka *umka)
{
    lexEat(&umka->lex, TOK_SWITCH);

    // Additional scope embracing ident and statement body
    blocksEnter(&umka->blocks);

    // ident
    lexCheck(&umka->lex, TOK_IDENT);
    IdentName concreteVarName;
    strcpy(concreteVarName, umka->lex.tok.name);
    lexNext(&umka->lex);

    // ":=" "type" "("
    lexEat(&umka->lex, TOK_COLONEQ);
    lexEat(&umka->lex, TOK_TYPE);
    lexEat(&umka->lex, TOK_LPAR);

    // expr
    const Type *type = NULL;
    parseExpr(umka, &type, NULL);
    if (type->kind != TYPE_INTERFACE)
        umka->error.handler(umka->error.context, "Interface type expected");

    // ")"
    lexEat(&umka->lex, TOK_RPAR);

    // "{" {typeCase} "}"
    lexEat(&umka->lex, TOK_LBRACE);

    int numCases = 0;
    ConstArray existingConcreteTypes;
    constArrayAlloc(&existingConcreteTypes, &umka->storage, umka->ptrVoidType);

    while (umka->lex.tok.kind == TOK_CASE)
    {
        parseTypeCase(umka, concreteVarName, &existingConcreteTypes);
        numCases++;
    }

    // [default]
    if (umka->lex.tok.kind == TOK_DEFAULT)
        parseDefault(umka);

    constArrayFree(&existingConcreteTypes);

    lexEat(&umka->lex, TOK_RBRACE);

    genSwitchEpilog(&umka->gen, numCases);

    genPop(&umka->gen);     // Remove expr

    // Additional scope embracing ident and statement body
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);
}


// switchStmt = exprSwitchStmt | typeSwitchStmt.
static void parseSwitchStmt(Umka *umka)
{
    if (doTypeSwitchStmtLookahead(umka))
        parseTypeSwitchStmt(umka);
    else
        parseExprSwitchStmt(umka);
}


typedef struct
{
    const Ident *indexIdent;
    TokenKind op;
    bool isDeferred;
} ForPostStmt;


// forHeader = [shortVarDecl ";"] expr [";" simpleStmt].
static void parseForHeader(Umka *umka, ForPostStmt *postStmt)
{
    // [shortVarDecl ";"]
    if (doShortVarDeclLookahead(umka))
    {
        parseShortVarDecl(umka);
        lexEat(&umka->lex, TOK_SEMICOLON);
    }

    genForCondProlog(&umka->gen);

    // Additional scope embracing expr (needed for timely garbage collection in expr, since it is computed at each iteration)
    blocksEnter(&umka->blocks);

    // expr
    const Type *type = umka->boolType;
    parseExpr(umka, &type, NULL);
    typeAssertCompatible(&umka->types, umka->boolType, type);

    // Additional scope embracing expr
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);

    // [";" simpleStmt]
    if (umka->lex.tok.kind == TOK_SEMICOLON || umka->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
    {
        lexNext(&umka->lex);

        if (doForPostIncDecStmtLookahead(umka))
        {
            // Special case: simpleStmt = ident ("++" | "--").
            genWhileCondEpilog(&umka->gen);

            postStmt->indexIdent = identAssertFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, umka->lex.tok.name, NULL);

            if (postStmt->indexIdent->kind != IDENT_VAR)
                umka->error.handler(umka->error.context, "%s is not a variable", postStmt->indexIdent->name);           
            
            if (identIsOuterLocalVar(&umka->blocks, postStmt->indexIdent))
                umka->error.handler(umka->error.context, "%s is not specified as a captured variable", postStmt->indexIdent->name);

            typeAssertCompatible(&umka->types, umka->intType, postStmt->indexIdent->type);

            lexNext(&umka->lex);
            postStmt->op = umka->lex.tok.kind;

            lexNext(&umka->lex);
            postStmt->isDeferred = true;
        }
        else
        {
            // General case
            genForCondEpilog(&umka->gen);
            
            // Additional scope embracing simpleStmt (needed for timely garbage collection in simpleStmt, since it is executed at each iteration)
            blocksEnter(&umka->blocks);

            parseSimpleStmt(umka);

            // Additional scope embracing simpleStmt
            doGarbageCollection(umka);
            identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
            blocksLeave(&umka->blocks);

            genForPostStmtEpilog(&umka->gen);

            postStmt->indexIdent = NULL;
            postStmt->op = TOK_NONE;
            postStmt->isDeferred = false;            
        }
    }
    else
    {
        // Special case: simpleStmt omitted - treat it as deferred
        genWhileCondEpilog(&umka->gen);

        postStmt->indexIdent = NULL;
        postStmt->op = TOK_NONE;
        postStmt->isDeferred = true;        
    }
}


// forInHeader = ident ["," ident ["^"]] "in" expr.
static void parseForInHeader(Umka *umka, ForPostStmt *postStmt)
{
    IdentName indexOrKeyName = {0}, itemName = {0};
    bool iterateByPtr = false;

    // ident ["," ident ["^"]] "in"
    lexCheck(&umka->lex, TOK_IDENT);
    strcpy(indexOrKeyName, umka->lex.tok.name);
    lexNext(&umka->lex);

    if (umka->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&umka->lex);
        lexCheck(&umka->lex, TOK_IDENT);
        strcpy(itemName, umka->lex.tok.name);
        lexNext(&umka->lex);

        if (umka->lex.tok.kind == TOK_CARET)
        {
            iterateByPtr = true;
            lexNext(&umka->lex);
        }
    }

    lexEat(&umka->lex, TOK_IN);

    // expr
    const Type *collectionType = NULL;
    parseExpr(umka, &collectionType, NULL);

    // Implicit dereferencing: x in a^ == x in a
    if (collectionType->kind == TYPE_PTR || collectionType->kind == TYPE_WEAKPTR)
    {
        if (collectionType->kind == TYPE_WEAKPTR)
            genStrengthenPtr(&umka->gen);

        genDeref(&umka->gen, collectionType->base->kind);
        collectionType = collectionType->base;
    }

    // Check collection type
    if (collectionType->kind != TYPE_ARRAY && collectionType->kind != TYPE_DYNARRAY && collectionType->kind != TYPE_MAP && collectionType->kind != TYPE_STR)
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        umka->error.handler(umka->error.context, "Expression of type %s is not iterable", typeSpelling(collectionType, typeBuf));
    }

    if (collectionType->kind == TYPE_STR && iterateByPtr)
        umka->error.handler(umka->error.context, "String is not iterable by pointer");

    // Declare variable for the collection length and assign len(expr) to it
    if (collectionType->kind == TYPE_ARRAY)
        genPushIntConst(&umka->gen, collectionType->numItems);
    else
    {
        genDup(&umka->gen);
        genCallBuiltin(&umka->gen, collectionType->kind, BUILTIN_LEN);
    }

    const Ident *lenIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, "#len", umka->intType, false);
    doPushVarPtr(umka, lenIdent);
    genSwapAssign(&umka->gen, lenIdent->type->kind, typeSize(&umka->types, lenIdent->type));

    const Ident *collectionIdent = NULL;
    if (itemName[0] != '\0' || collectionType->kind == TYPE_MAP)
    {
        // Declare variable for the collection and assign expr to it
        const Type *collectionIdentType = collectionType;
        if (collectionType->kind == TYPE_ARRAY)
            collectionIdentType = typeAddPtrTo(&umka->types, &umka->blocks, collectionType);    // Avoid copying the whole static array - use a pointer instead

        collectionIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, "#collection", collectionIdentType, false);
        doZeroVar(umka, collectionIdent);
        doPushVarPtr(umka, collectionIdent);
        genSwapChangeRefCntAssign(&umka->gen, collectionIdent->type);
    }
    else
    {
        // Remove expr
        genPop(&umka->gen);
    }

    // Declare variable for the collection index (for maps, it will be used for indexing keys())
    const char *indexName = (collectionType->kind == TYPE_MAP) ? "#index" : indexOrKeyName;
    
    postStmt->indexIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, indexName, umka->intType, false);
    postStmt->op = TOK_PLUSPLUS;
    postStmt->isDeferred = true;

    identSetUsed(postStmt->indexIdent);                    // Do not warn about unused index
    doZeroVar(umka, postStmt->indexIdent);

    const Ident *keyIdent = NULL, *keysIdent = NULL;
    if (collectionType->kind == TYPE_MAP)
    {
        // Declare variable for the map key
        keyIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, indexOrKeyName, typeMapKey(collectionType), false);
        identSetUsed(keyIdent);                            // Do not warn about unused key
        doZeroVar(umka, keyIdent);

        // Declare variable for the map keys
        Type *keysType = typeAdd(&umka->types, &umka->blocks, TYPE_DYNARRAY);
        keysType->base = typeMapKey(collectionType);
        keysIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, "#keys", keysType, false);
        doZeroVar(umka, keysIdent);

        // Call keys()
        const int resultOffset = identAllocStack(&umka->idents, &umka->types, &umka->blocks, keysType);
        doPushVarPtr(umka, collectionIdent);        // Map
        genPushLocalPtr(&umka->gen, resultOffset);  // Pointer to result (hidden parameter)
        genCallTypedBuiltin(&umka->gen, keysType, BUILTIN_KEYS);

        // Assign map keys
        doPushVarPtr(umka, keysIdent);
        genSwapAssign(&umka->gen, keysType->kind, typeSize(&umka->types, keysType));
    }

    const Ident *itemIdent = NULL;
    if (itemName[0] != '\0')
    {
        // Declare variable for the collection item
        const Type *itemType = NULL;

        if (collectionType->kind == TYPE_MAP)
            itemType = typeMapItem(collectionType);
        else if (collectionType->kind == TYPE_STR)
            itemType = umka->charType;
        else
            itemType = collectionType->base;

        if (iterateByPtr)
            itemType = typeAddPtrTo(&umka->types, &umka->blocks, itemType);

        itemIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, itemName, itemType, false);
        doZeroVar(umka, itemIdent);
    }

    genWhileCondProlog(&umka->gen);

    // Implicit conditional expression: #index < #len
    doPushVarPtr(umka, postStmt->indexIdent);
    genDeref(&umka->gen, TYPE_INT);
    doPushVarPtr(umka, lenIdent);
    genDeref(&umka->gen, TYPE_INT);
    genBinary(&umka->gen, TOK_LESS, umka->intType);

    genWhileCondEpilog(&umka->gen);

    if (collectionType->kind == TYPE_MAP)
    {
        // Assign key = #keys[#index]
        doPushVarPtr(umka, keysIdent);
        doPushVarPtr(umka, postStmt->indexIdent);
        genDeref(&umka->gen, TYPE_INT);
        genGetDynArrayPtr(&umka->gen);
        genDeref(&umka->gen, keyIdent->type->kind);

        doPushVarPtr(umka, keyIdent);
        genSwapChangeRefCntAssign(&umka->gen, keyIdent->type);
    }

    // Assign collection item
    if (itemIdent)
    {
        doPushVarPtr(umka, collectionIdent);
        genDeref(&umka->gen, collectionIdent->type->kind);

        if (collectionType->kind == TYPE_MAP)
        {
            // Push item key
            doPushVarPtr(umka, keyIdent);
            genDeref(&umka->gen, keyIdent->type->kind);
        }
        else
        {
            // Push item index
            doPushVarPtr(umka, postStmt->indexIdent);
            genDeref(&umka->gen, TYPE_INT);
        }

        switch (collectionType->kind)
        {
            case TYPE_ARRAY:     genGetArrayPtr(&umka->gen, typeSize(&umka->types, collectionType->base), collectionType->numItems); break;
            case TYPE_DYNARRAY:  genGetDynArrayPtr(&umka->gen);                                                                      break;
            case TYPE_STR:       genGetArrayPtr(&umka->gen, typeSize(&umka->types, umka->charType), INT_MAX);                        break; // No range checking
            case TYPE_MAP:       genGetMapPtr(&umka->gen, collectionType);                                                           break;
            default:             break;
        }

        // Get collection item value
        if (!iterateByPtr)
            genDeref(&umka->gen, itemIdent->type->kind);

        // Assign collection item to iteration variable
        doPushVarPtr(umka, itemIdent);
        genSwapChangeRefCntAssign(&umka->gen, itemIdent->type);
    }
}


// forStmt = "for" (forHeader | forInHeader) block.
static void parseForStmt(Umka *umka)
{
    lexEat(&umka->lex, TOK_FOR);

    // Additional scope embracing shortVarDecl in forHeader/forEachHeader and statement body
    blocksEnter(&umka->blocks);

    // 'break'/'continue' prologs
    Gotos breaks, *outerBreaks = umka->gen.breaks;
    umka->gen.breaks = &breaks;
    genGotosProlog(&umka->gen, umka->gen.breaks, blocksCurrent(&umka->blocks));

    Gotos continues, *outerContinues = umka->gen.continues;
    umka->gen.continues = &continues;
    genGotosProlog(&umka->gen, umka->gen.continues, blocksCurrent(&umka->blocks));

    ForPostStmt deferredPostStmt = {0};

    Lexer lookaheadLex = umka->lex;
    lexNext(&lookaheadLex);

    if (!doShortVarDeclLookahead(umka) && (lookaheadLex.tok.kind == TOK_COMMA || lookaheadLex.tok.kind == TOK_IN))
        parseForInHeader(umka, &deferredPostStmt);
    else
        parseForHeader(umka, &deferredPostStmt);

    // block
    parseBlock(umka);

    // 'continue' epilog
    genGotosEpilog(&umka->gen, umka->gen.continues);
    umka->gen.continues = outerContinues;

    // simpleStmt, if deferred
    if (deferredPostStmt.isDeferred)
    {
        if (deferredPostStmt.indexIdent)
        {
            doPushVarPtr(umka, deferredPostStmt.indexIdent);
            genUnary(&umka->gen, deferredPostStmt.op, deferredPostStmt.indexIdent->type);            
        }
        genWhileEpilog(&umka->gen);
    }
    else
    {
        genForEpilog(&umka->gen);        
    }

    // 'break' epilog
    genGotosEpilog(&umka->gen, umka->gen.breaks);
    umka->gen.breaks = outerBreaks;

    // Additional scope embracing shortVarDecl in forHeader/forInHeader and statement body
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);
}


// breakStmt = "break".
static void parseBreakStmt(Umka *umka)
{
    lexEat(&umka->lex, TOK_BREAK);

    if (!umka->gen.breaks)
        umka->error.handler(umka->error.context, "No loop to break");

    doGarbageCollectionDownToBlock(umka, umka->gen.breaks->block);
    genGotosAddStub(&umka->gen, umka->gen.breaks);
}


// continueStmt = "continue".
static void parseContinueStmt(Umka *umka)
{
    lexEat(&umka->lex, TOK_CONTINUE);

    if (!umka->gen.continues)
        umka->error.handler(umka->error.context, "No loop to continue");

    doGarbageCollectionDownToBlock(umka, umka->gen.continues->block);
    genGotosAddStub(&umka->gen, umka->gen.continues);
}


// returnStmt = "return" [exprList].
static void parseReturnStmt(Umka *umka)
{
    lexEat(&umka->lex, TOK_RETURN);
    umka->blocks.item[umka->blocks.top].hasReturn = true;

    // Get function signature
    const Signature *sig = NULL;
    for (int i = umka->blocks.top; i >= 1; i--)
        if (umka->blocks.item[i].fn)
        {
            sig = &umka->blocks.item[i].fn->type->sig;
            break;
        }

    if (!sig)
        umka->error.handler(umka->error.context, "Function block not found");

    const Type *type = sig->resultType;
    if (umka->lex.tok.kind != TOK_SEMICOLON && umka->lex.tok.kind != TOK_IMPLICIT_SEMICOLON && umka->lex.tok.kind != TOK_RBRACE)
        parseExprList(umka, &type, NULL);
    else
        type = umka->voidType;

    doAssertImplicitTypeConv(umka, sig->resultType, &type, NULL);

    // Check non-64-bit ordinal and real types for overflow
    if (sig->resultType->kind != type->kind && typeNarrow(sig->resultType))
        genAssertRange(&umka->gen, sig->resultType->kind, type);

    // Copy structure to #result
    if (typeStructured(sig->resultType))
    {
        const Ident *result = identAssertFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, "#result", NULL);

        doPushVarPtr(umka, result);
        genDeref(&umka->gen, TYPE_PTR);

        // Assignment to an anonymous stack area (pointed to by #result) does not require updating reference counts
        genSwapAssign(&umka->gen, sig->resultType->kind, typeSize(&umka->types, sig->resultType));

        doPushVarPtr(umka, result);
        genDeref(&umka->gen, TYPE_PTR);
    }

    if (sig->resultType->kind != TYPE_VOID)
    {
        if (doTryRemoveCopyResultToTempVar(umka))
        {
            // Optimization: if the result expression is a function call, assume its reference count to be already increased before the inner return
            // The outer caller will hold this additional reference, so we can remove the temporary "reference holder" variable
        }
        else
        {
            // General case: increase result reference count
            genChangeRefCnt(&umka->gen, TOK_PLUSPLUS, sig->resultType);
        }
        genPopReg(&umka->gen, REG_RESULT);
    }

    doGarbageCollectionDownToBlock(umka, umka->gen.returns->block);
    genGotosAddStub(&umka->gen, umka->gen.returns);
}


// stmt = decl | block | simpleStmt | ifStmt | switchStmt | forStmt | breakStmt | continueStmt | returnStmt.
static void parseStmt(Umka *umka)
{
    switch (umka->lex.tok.kind)
    {
        case TOK_TYPE:
        case TOK_CONST:
        case TOK_VAR:       parseDecl(umka);            break;
        case TOK_LBRACE:    parseBlock(umka);           break;
        case TOK_IDENT:
        case TOK_CARET:
        case TOK_WEAK:
        case TOK_LBRACKET:
        case TOK_STR:
        case TOK_STRUCT:
        case TOK_INTERFACE:
        case TOK_MAP:
        case TOK_FN:        parseSimpleStmt(umka);      break;
        case TOK_IF:        parseIfStmt(umka);          break;
        case TOK_SWITCH:    parseSwitchStmt(umka);      break;
        case TOK_FOR:       parseForStmt(umka);         break;
        case TOK_BREAK:     parseBreakStmt(umka);       break;
        case TOK_CONTINUE:  parseContinueStmt(umka);    break;
        case TOK_RETURN:    parseReturnStmt(umka);      break;

        default: break;
    }
}


// stmtList = Stmt {";" Stmt}.
static void parseStmtList(Umka *umka)
{
    while (1)
    {
        parseStmt(umka);
        if (umka->lex.tok.kind != TOK_SEMICOLON && umka->lex.tok.kind != TOK_IMPLICIT_SEMICOLON)
            break;
        lexNext(&umka->lex);
    };
}


// block = "{" StmtList "}".
static void parseBlock(Umka *umka)
{
    lexEat(&umka->lex, TOK_LBRACE);
    blocksEnter(&umka->blocks);

    parseStmtList(umka);

    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    identFree(&umka->idents, blocksCurrent(&umka->blocks));

    blocksLeave(&umka->blocks);
    lexEat(&umka->lex, TOK_RBRACE);
}


// fnBlock = block.
void parseFnBlock(Umka *umka, Ident *fn, const Type *upvaluesStructType)
{
    lexEat(&umka->lex, TOK_LBRACE);
    blocksEnterFn(&umka->blocks, fn, upvaluesStructType != NULL);

    const char *prevDebugFnName = umka->lex.debug->fnName;

    if (fn->kind == IDENT_CONST && fn->type->kind == TYPE_FN && fn->block == 0)
    {
        if (fn->type->sig.isMethod)
            umka->lex.debug->fnName = identMethodNameWithRcv(&umka->idents, fn);
        else
            umka->lex.debug->fnName = fn->name;
    }
    else
        umka->lex.debug->fnName = "<unknown>";

    if (fn->prototypeOffset >= 0)
    {
        genEntryPoint(&umka->gen, fn->prototypeOffset);
        fn->prototypeOffset = -1;
    }

    genEnterFrameStub(&umka->gen);

    // Formal parameters
    for (int i = 0; i < fn->type->sig.numParams; i++)
        identAllocParam(&umka->idents, &umka->types, &umka->modules, &umka->blocks, &fn->type->sig, i);

    // Upvalues
    if (upvaluesStructType)
    {
        // Extract upvalues structure from the "any" interface
        const Ident *upvaluesParamIdent = identAssertFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, "#upvalues", NULL);
        const Type *upvaluesParamType = upvaluesParamIdent->type;

        doPushVarPtr(umka, upvaluesParamIdent);
        genDeref(&umka->gen, upvaluesParamIdent->type->kind);
        doExplicitTypeConv(umka, upvaluesStructType, &upvaluesParamType, NULL);

        // Copy upvalue structure fields to new local variables
        for (int i = 0; i < upvaluesStructType->numItems; i++)
        {
            const Field *upvalue = upvaluesStructType->field[i];

            genDup(&umka->gen);
            genGetFieldPtr(&umka->gen, upvalue->offset);
            genDeref(&umka->gen, upvalue->type->kind);

            const Ident *upvalueIdent = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, upvalue->name, upvalue->type, false);
            doZeroVar(umka, upvalueIdent);
            doPushVarPtr(umka, upvalueIdent);

            genSwapChangeRefCntAssign(&umka->gen, upvalue->type);
        }

        genPop(&umka->gen);
    }

    // 'break'/'continue'/'return' prologs
    Gotos *outerBreaks = umka->gen.breaks;
    umka->gen.breaks = NULL;

    Gotos *outerContinues = umka->gen.continues;
    umka->gen.continues = NULL;

    Gotos returns, *outerReturns = umka->gen.returns;
    umka->gen.returns = &returns;
    genGotosProlog(&umka->gen, umka->gen.returns, blocksCurrent(&umka->blocks));

    // Additional scope embracing StmtList
    blocksEnter(&umka->blocks);

    // StmtList
    parseStmtList(umka);

    const bool hasReturn = umka->blocks.item[umka->blocks.top].hasReturn;

    // Additional scope embracing StmtList
    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    blocksLeave(&umka->blocks);

    // 'return'/'continue'/'break' epilogs
    genGotosEpilog(&umka->gen, umka->gen.returns);
    umka->gen.returns = outerReturns;
    umka->gen.continues = outerContinues;
    umka->gen.breaks = outerBreaks;

    doGarbageCollection(umka);
    identWarnIfUnusedAll(&umka->idents, blocksCurrent(&umka->blocks));
    identFree(&umka->idents, blocksCurrent(&umka->blocks));

    const int localVarSlots = align(umka->blocks.item[umka->blocks.top].localVarSize, sizeof(Slot)) / sizeof(Slot);

    const ParamLayout *paramLayout = typeMakeParamLayout(&umka->types, &fn->type->sig);

    genLeaveFrameFixup(&umka->gen, typeMakeParamAndLocalVarLayout(&umka->types, paramLayout, localVarSlots));
    genReturn(&umka->gen, paramLayout->numParamSlots);

    umka->lex.debug->fnName = prevDebugFnName;

    blocksLeave(&umka->blocks);
    lexEat(&umka->lex, TOK_RBRACE);

    if (!hasReturn && fn->type->sig.resultType->kind != TYPE_VOID)
        umka->error.handler(umka->error.context, "Function must return a value");
}


// fnPrototype = .
void parseFnPrototype(Umka *umka, Ident *fn)
{
    fn->prototypeOffset = fn->offset;
    genNop(&umka->gen);
}

