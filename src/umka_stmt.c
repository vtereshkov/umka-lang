#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <string.h>

#include "umka_stmt.h"
#include "umka_expr.h"
#include "umka_decl.h"


static void parseStmtList(Compiler *comp);
static void parseBlock(Compiler *comp);


void doGarbageCollection(Compiler *comp, int block)
{
    for (Ident *ident = comp->idents.first; ident; ident = ident->next)
        if (ident->kind == IDENT_VAR && typeGarbageCollected(ident->type) && ident->block == block && !(ident->temporary && !ident->used) && strcmp(ident->name, "__result") != 0)
        {
            doPushVarPtr(comp, ident);
            genDeref(&comp->gen, ident->type->kind);
            genChangeRefCnt(&comp->gen, TOK_MINUSMINUS, ident->type);
            genPop(&comp->gen);
        }
}


void doGarbageCollectionDownToBlock(Compiler *comp, int block)
{
    // Collect garbage over all scopes down to the specified block (not inclusive)
    for (int i = comp->blocks.top; i >= 1; i--)
    {
        if (comp->blocks.item[i].block == block)
            break;
        doGarbageCollection(comp, comp->blocks.item[i].block);
    }
}


void doZeroVar(Compiler *comp, Ident *ident)
{
    if (ident->block == 0)
        constZero(ident->ptr, typeSize(&comp->types, ident->type));
    else
    {
        doPushVarPtr(comp, ident);
        genZero(&comp->gen, typeSize(&comp->types, ident->type));
    }
}


void doResolveExtern(Compiler *comp)
{
    for (Ident *ident = comp->idents.first; ident; ident = ident->next)
        if (ident->module == comp->blocks.module)
        {
            if (ident->prototypeOffset >= 0)
            {
                External *external = externalFind(&comp->externals, ident->name);

                // Try to find the function in the external function list or in an external implementation library
                void *fn = NULL;
                if (external)
                {
                    if (external->resolved)
                        comp->error.handler(comp->error.context, "External %s is already resolved", ident->name);

                    fn = external->entry;
                    external->resolved = true;
                }
                else
                    fn = moduleGetImplLibFunc(comp->modules.module[comp->blocks.module], ident->name);

                if (!fn)
                    comp->error.handler(comp->error.context, "Unresolved prototype of %s", ident->name);

                // All parameters must be declared since they may require garbage collection
                blocksEnter(&comp->blocks, ident);
                genEntryPoint(&comp->gen, ident->prototypeOffset);
                genEnterFrameStub(&comp->gen);

                for (int i = 0; i < ident->type->sig.numParams; i++)
                    identAllocParam(&comp->idents, &comp->types, &comp->modules, &comp->blocks, &ident->type->sig, i);

                genCallExtern(&comp->gen, fn);

                doGarbageCollection(comp, blocksCurrent(&comp->blocks));
                identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
                identFree(&comp->idents, blocksCurrent(&comp->blocks));

                int paramSlots = align(typeParamSizeTotal(&comp->types, &ident->type->sig), sizeof(Slot)) / sizeof(Slot);

                genLeaveFrameFixup(&comp->gen, 0);
                genReturn(&comp->gen, paramSlots);

                blocksLeave(&comp->blocks);
            }

            identWarnIfUnused(&comp->idents, ident);
        }
}


static bool doShortVarDeclLookahead(Compiler *comp)
{
    // ident {"," ident} ":="
    Lexer lookaheadLex = comp->lex;
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


static bool doTypeSwitchStmtLookahead(Compiler *comp)
{
    // "switch" ident ":=" "type"
    Lexer lookaheadLex = comp->lex;
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


// singleAssignmentStmt = designator "=" exprOrLit.
static void parseSingleAssignmentStmt(Compiler *comp, Type *type, Const *varPtrConst)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error.handler(comp->error.context, "Left side cannot be assigned to");
        type = type->base;
    }

    Type *rightType;
    Const rightConstantBuf, *rightConstant = varPtrConst ? &rightConstantBuf : NULL;
    parseExprOrUntypedLiteral(comp, &rightType, type, rightConstant);

    doImplicitTypeConv(comp, type, &rightType, rightConstant, false);
    typeAssertCompatible(&comp->types, type, rightType, false);

    if (varPtrConst)                                // Initialize global variable
        constAssign(&comp->consts, varPtrConst->ptrVal, rightConstant, type->kind, typeSize(&comp->types, type));
    else                                            // Assign to variable
    {
        if (doTryRemoveCopyResultToTempVar(comp))
        {
            // Optimization: if the right-hand side is a function call, assume its reference count to be already increased before return
            // The left-hand side will hold this additional reference, so we can remove the temporary "reference holder" variable
            genChangeLeftRefCntAssign(&comp->gen, type);

        }
        else
        {
            // General case: update reference counts for both sides
            genChangeRefCntAssign(&comp->gen, type);
        }
    }
}


// listAssignmentStmt = designatorList "=" exprOrLitList.
static void parseListAssignmentStmt(Compiler *comp, Type *type, Const *varPtrConstList)
{
    Type *rightListType;
    Const rightListConstantBuf, *rightListConstant = varPtrConstList ? &rightListConstantBuf : NULL;
    parseExprOrUntypedLiteralList(comp, &rightListType, type, rightListConstant);

    const int numExpr = typeExprListStruct(rightListType) ? rightListType->numItems : 1;
    if (numExpr != type->numItems)
        comp->error.handler(comp->error.context, "%d expressions expected but %d found", type->numItems, numExpr);

    for (int i = type->numItems - 1; i >= 0; i--)
    {
        Type *leftType = type->field[i]->type;
        if (!typeStructured(leftType))
        {
            if (leftType->kind != TYPE_PTR || leftType->base->kind == TYPE_VOID)
                comp->error.handler(comp->error.context, "Left side cannot be assigned to");
            leftType = leftType->base;
        }

        Type *rightType = rightListType->field[i]->type;

        if (varPtrConstList)                                // Initialize global variables
        {
            Const rightConstantBuf = {.ptrVal = (char *)rightListConstant->ptrVal + rightListType->field[i]->offset};
            constDeref(&comp->consts, &rightConstantBuf, rightType->kind);

            doImplicitTypeConv(comp, leftType, &rightType, &rightConstantBuf, false);
            typeAssertCompatible(&comp->types, leftType, rightType, false);

            constAssign(&comp->consts, varPtrConstList[i].ptrVal, &rightConstantBuf, rightType->kind, typeSize(&comp->types, rightType));
        }
        else                                                // Assign to variable
        {
            genDup(&comp->gen);                                             // Duplicate expression list pointer
            genPopReg(&comp->gen, VM_REG_COMMON_3);                         // Save expression list pointer
            genGetFieldPtr(&comp->gen, rightListType->field[i]->offset);    // Get expression pointer
            genDeref(&comp->gen, rightType->kind);                          // Get expression value

            doImplicitTypeConv(comp, leftType, &rightType, NULL, false);
            typeAssertCompatible(&comp->types, leftType, rightType, false);

            genChangeRefCntAssign(&comp->gen, leftType);                    // Assign expression to variable
            genPushReg(&comp->gen, VM_REG_COMMON_3);                        // Restore expression list pointer
        }
    }

    if (!varPtrConstList)
        genPop(&comp->gen);                                                 // Remove expression list pointer
}


// assignmentStmt = singleAssignmentStmt | listAssignmentStmt.
void parseAssignmentStmt(Compiler *comp, Type *type, Const *varPtrConstList)
{
    if (typeExprListStruct(type))
        parseListAssignmentStmt(comp, type, varPtrConstList);
    else
        parseSingleAssignmentStmt(comp, type, varPtrConstList);
}


// shortAssignmentStmt = designator ("+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "~=" | "<<=" | ">>=") expr.
static void parseShortAssignmentStmt(Compiler *comp, Type *type, TokenKind op)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error.handler(comp->error.context, "Left side cannot be assigned to");
        type = type->base;
    }

    // Duplicate designator and treat it as an expression
    genDup(&comp->gen);
    genDeref(&comp->gen, type->kind);

    Type *leftType = type;
    Type *rightType;
    parseExpr(comp, &rightType, NULL);

    // Keep "+=" for strings as is for better optimizations
    const TokenKind shortOp = (leftType->kind == TYPE_STR && op == TOK_PLUSEQ) ? op : lexShortAssignment(op);

    doApplyOperator(comp, &leftType, &rightType, NULL, NULL, shortOp, true, false);
    genChangeRefCntAssign(&comp->gen, type);
}


// singleDeclAssignmentStmt = ident ":=" expr.
static void parseSingleDeclAssignmentStmt(Compiler *comp, IdentName name, bool exported, bool constExpr)
{
    Type *rightType;
    Const rightConstantBuf, *rightConstant = constExpr ? &rightConstantBuf : NULL;
    parseExpr(comp, &rightType, rightConstant);

    if (typeExprListStruct(rightType))
        comp->error.handler(comp->error.context, "1 expression expected but %d found", rightType->numItems);

    Ident *ident = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, name, rightType, exported);

    if (constExpr)              // Initialize global variable
        constAssign(&comp->consts, ident->ptr, rightConstant, rightType->kind, typeSize(&comp->types, rightType));
    else                        // Assign to variable
    {
        if (doTryRemoveCopyResultToTempVar(comp))
        {
            // Optimization: if the right-hand side is a function call, assume its reference count to be already increased before return
            // The left-hand side will hold this additional reference, so we can remove the temporary "reference holder" variable
        }
        else
        {
            // General case: increase right-hand side reference count
            genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, rightType);
        }

        doPushVarPtr(comp, ident);
        genSwapAssign(&comp->gen, rightType->kind, typeSize(&comp->types, rightType));
    }
}


// listDeclAssignmentStmt = identList ":=" exprOrLitList.
static void parseListDeclAssignmentStmt(Compiler *comp, IdentName *names, bool *exported, int num, bool constExpr)
{
    Type *rightListType;
    Const rightListConstantBuf, *rightListConstant = constExpr ? &rightListConstantBuf : NULL;
    parseExprOrUntypedLiteralList(comp, &rightListType, NULL, rightListConstant);

    const int numExpr = typeExprListStruct(rightListType) ? rightListType->numItems : 1;
    if (numExpr != num)
        comp->error.handler(comp->error.context, "%d expressions expected but %d found", num, numExpr);

    for (int i = 0; i < num; i++)
    {
        Type *rightType = rightListType->field[i]->type;
        Ident *ident = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, names[i], rightType, exported[i]);

        if (constExpr)              // Initialize global variable
        {
            Const rightConstantBuf = {.ptrVal = (char *)rightListConstant->ptrVal + rightListType->field[i]->offset};
            constDeref(&comp->consts, &rightConstantBuf, rightType->kind);
            constAssign(&comp->consts, ident->ptr, &rightConstantBuf, rightType->kind, typeSize(&comp->types, rightType));
        }
        else                        // Assign to variable
        {
            genDup(&comp->gen);                                             // Duplicate expression list pointer
            genGetFieldPtr(&comp->gen, rightListType->field[i]->offset);    // Get expression pointer
            genDeref(&comp->gen, rightType->kind);                          // Get expression value

            genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, rightType);           // Increase right-hand side reference count

            doPushVarPtr(comp, ident);
            genSwapAssign(&comp->gen, rightType->kind, typeSize(&comp->types, rightType));
        }
    }

    if (!constExpr)
        genPop(&comp->gen);                                                 // Remove expression list pointer
}


// declAssignmentStmt = singleDeclAssignmentStmt | listDeclAssignmentStmt.
void parseDeclAssignmentStmt(Compiler *comp, IdentName *names, bool *exported, int num, bool constExpr)
{
    if (num > 1)
        parseListDeclAssignmentStmt(comp, names, exported, num, constExpr);
    else
        parseSingleDeclAssignmentStmt(comp, names[0], exported[0], constExpr);
}


// incDecStmt = designator ("++" | "--").
static void parseIncDecStmt(Compiler *comp, Type *type, TokenKind op)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error.handler(comp->error.context, "Left side cannot be assigned to");
        type = type->base;
    }

    typeAssertCompatible(&comp->types, comp->intType, type, false);
    genUnary(&comp->gen, op, type->kind);
    lexNext(&comp->lex);
}


// simpleStmt = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
// callStmt   = designator.
static void parseSimpleStmt(Compiler *comp)
{
    if (doShortVarDeclLookahead(comp))
        parseShortVarDecl(comp);
    else
    {
        Type *type;
        bool isVar, isCall;
        parseDesignatorList(comp, &type, NULL, &isVar, &isCall);

        TokenKind op = comp->lex.tok.kind;

        if (typeExprListStruct(type) && !isCall && op != TOK_EQ)
            comp->error.handler(comp->error.context, "List assignment expected");

        if (op == TOK_EQ || lexShortAssignment(op) != TOK_NONE)
        {
            // Assignment
            if (!isVar)
                comp->error.handler(comp->error.context, "Left side cannot be assigned to");
            lexNext(&comp->lex);

            if (op == TOK_EQ)
                parseAssignmentStmt(comp, type, NULL);
            else
                parseShortAssignmentStmt(comp, type, op);
        }
        else if (op == TOK_PLUSPLUS || op == TOK_MINUSMINUS)
        {
            // Increment/decrement
            parseIncDecStmt(comp, type, op);
        }
        else
        {
            // Call
            if (!isCall)
                comp->error.handler(comp->error.context, "Assignment or function call expected");
            if (type->kind != TYPE_VOID)
                genPop(&comp->gen);  // Manually remove result
        }
    }
}


// ifStmt = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
static void parseIfStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_IF);

    // Additional scope embracing shortVarDecl and statement body
    blocksEnter(&comp->blocks, NULL);

    // [shortVarDecl ";"]
    if (doShortVarDeclLookahead(comp))
    {
        parseShortVarDecl(comp);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }

    // expr
    Type *type;
    parseExpr(comp, &type, NULL);
    typeAssertCompatible(&comp->types, comp->boolType, type, false);

    genIfCondEpilog(&comp->gen);

    // block
    parseBlock(comp);

    // ["else" (ifStmt | block)]
    if (comp->lex.tok.kind == TOK_ELSE)
    {
        genElseProlog(&comp->gen);
        lexNext(&comp->lex);

        if (comp->lex.tok.kind == TOK_IF)
            parseIfStmt(comp);
        else
            parseBlock(comp);
    }

    genIfElseEpilog(&comp->gen);

    // Additional scope embracing shortVarDecl and statement body
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
}


// exprCase = "case" expr {"," expr} ":" stmtList.
static void parseExprCase(Compiler *comp, Type *selectorType)
{
    lexEat(&comp->lex, TOK_CASE);

    // expr {"," expr}
    while (1)
    {
        Const constant;
        Type *type;
        parseExpr(comp, &type, &constant);
        typeAssertCompatible(&comp->types, selectorType, type, false);

        genCaseExprEpilog(&comp->gen, &constant);

        if (comp->lex.tok.kind != TOK_COMMA)
            break;
        lexNext(&comp->lex);
    }

    // ":" stmtList
    lexEat(&comp->lex, TOK_COLON);

    genCaseBlockProlog(&comp->gen);

    // Additional scope embracing stmtList
    blocksEnter(&comp->blocks, NULL);

    parseStmtList(comp);

    // Additional scope embracing stmtList
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);

    genCaseBlockEpilog(&comp->gen);
}


// typeCase = "case" type ":" stmtList.
static void parseTypeCase(Compiler *comp, Type *selectorType, const char *concreteVarName)
{
    lexEat(&comp->lex, TOK_CASE);

    // type
    Type *concreteType = parseType(comp, NULL);
    if (concreteType->kind == TYPE_INTERFACE)
        comp->error.handler(comp->error.context, "Non-interface type expected");

    Type *concretePtrType = concreteType;
    if (concreteType->kind != TYPE_PTR)
        concretePtrType = typeAddPtrTo(&comp->types, &comp->blocks, concreteType);

    genDup(&comp->gen);                             // Duplicate interface expression
    genAssertType(&comp->gen, concretePtrType);

    genDup(&comp->gen);                             // Duplicate expression converted to the concrete type
    genPushGlobalPtr(&comp->gen, NULL);
    genBinary(&comp->gen, TOK_NOTEQ, TYPE_PTR, 0);

    genIfCondEpilog(&comp->gen);

    // ":" stmtList
    lexEat(&comp->lex, TOK_COLON);

    // Additional scope embracing stmtList
    blocksEnter(&comp->blocks, NULL);

    // Allocate and initialize concrete-type variable
    Ident *concreteIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, concreteVarName, concreteType, false);
    concreteIdent->used = true;                     // Do not warn about unused concrete variable
    doZeroVar(comp, concreteIdent);

    if (concreteType->kind != TYPE_PTR)
        genDeref(&comp->gen, concreteType->kind);

    doPushVarPtr(comp, concreteIdent);
    genSwapChangeRefCntAssign(&comp->gen, concreteType);

    parseStmtList(comp);

    // Additional scope embracing stmtList
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);

    genElseProlog(&comp->gen);

    genPop(&comp->gen);                 // Remove duplicate interface expression
}


// default = "default" ":" stmtList.
static void parseDefault(Compiler *comp)
{
    lexEat(&comp->lex, TOK_DEFAULT);
    lexEat(&comp->lex, TOK_COLON);

    // Additional scope embracing stmtList
    blocksEnter(&comp->blocks, NULL);

    parseStmtList(comp);

    // Additional scope embracing stmtList
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
}


// exprSwitchStmt = "switch" [shortVarDecl ";"] expr "{" {exprCase} [default] "}".
static void parseExprSwitchStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_SWITCH);

    // Additional scope embracing shortVarDecl and statement body
    blocksEnter(&comp->blocks, NULL);

    // [shortVarDecl ";"]
    if (doShortVarDeclLookahead(comp))
    {
        parseShortVarDecl(comp);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }

    // expr
    Type *type;
    parseExpr(comp, &type, NULL);
    if (!typeOrdinal(type))
        comp->error.handler(comp->error.context, "Ordinal type expected");

    genSwitchCondEpilog(&comp->gen);

    // "{" {exprCase} "}"
    lexEat(&comp->lex, TOK_LBRACE);

    int numCases = 0;
    while (comp->lex.tok.kind == TOK_CASE)
    {
        parseExprCase(comp, type);
        numCases++;
    }

    // [default]
    if (comp->lex.tok.kind == TOK_DEFAULT)
        parseDefault(comp);

    lexEat(&comp->lex, TOK_RBRACE);

    genSwitchEpilog(&comp->gen, numCases);

    // Additional scope embracing shortVarDecl and statement body
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
}


// typeSwitchStmt = "switch" ident ":=" "type" "(" expr ")" "{" {typeCase} [default] "}".
static void parseTypeSwitchStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_SWITCH);

    // Additional scope embracing ident and statement body
    blocksEnter(&comp->blocks, NULL);

    // ident
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName concreteVarName;
    strcpy(concreteVarName, comp->lex.tok.name);
    lexNext(&comp->lex);

    // ":=" "type" "("
    lexEat(&comp->lex, TOK_COLONEQ);
    lexEat(&comp->lex, TOK_TYPE);
    lexEat(&comp->lex, TOK_LPAR);

    // expr
    Type *type;
    parseExpr(comp, &type, NULL);
    if (type->kind != TYPE_INTERFACE)
        comp->error.handler(comp->error.context, "Interface type expected");

    // ")"
    lexEat(&comp->lex, TOK_RPAR);

    // "{" {typeCase} "}"
    lexEat(&comp->lex, TOK_LBRACE);

    int numCases = 0;
    while (comp->lex.tok.kind == TOK_CASE)
    {
        parseTypeCase(comp, type, concreteVarName);
        numCases++;
    }

    // [default]
    if (comp->lex.tok.kind == TOK_DEFAULT)
        parseDefault(comp);

    lexEat(&comp->lex, TOK_RBRACE);

    genSwitchEpilog(&comp->gen, numCases);

    genPop(&comp->gen);     // Remove expr

    // Additional scope embracing ident and statement body
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
}


// switchStmt = exprSwitchStmt | typeSwitchStmt.
static void parseSwitchStmt(Compiler *comp)
{
    if (doTypeSwitchStmtLookahead(comp))
        parseTypeSwitchStmt(comp);
    else
        parseExprSwitchStmt(comp);
}


// forHeader = [shortVarDecl ";"] expr [";" simpleStmt].
static void parseForHeader(Compiler *comp)
{
    // [shortVarDecl ";"]
    if (doShortVarDeclLookahead(comp))
    {
        parseShortVarDecl(comp);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }

    genForCondProlog(&comp->gen);

    // Additional scope embracing expr (needed for timely garbage collection in expr, since it is computed at each iteration)
    blocksEnter(&comp->blocks, NULL);

    // expr
    Type *type;
    parseExpr(comp, &type, NULL);
    typeAssertCompatible(&comp->types, comp->boolType, type, false);

    // Additional scope embracing expr
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);

    genForCondEpilog(&comp->gen);

    // [";" simpleStmt]
    if (comp->lex.tok.kind == TOK_SEMICOLON || comp->lex.tok.kind == TOK_IMPLICIT_SEMICOLON)
    {
        // Additional scope embracing simpleStmt (needed for timely garbage collection in simpleStmt, since it is executed at each iteration)
        blocksEnter(&comp->blocks, NULL);

        lexNext(&comp->lex);
        parseSimpleStmt(comp);

        // Additional scope embracing simpleStmt
        doGarbageCollection(comp, blocksCurrent(&comp->blocks));
        identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
        blocksLeave(&comp->blocks);
    }

    genForPostStmtEpilog(&comp->gen);
}


// forInHeader = ident ["," ident] "in" expr.
static void parseForInHeader(Compiler *comp)
{
    IdentName indexOrKeyName = {0}, itemName = {0};

    // ident ["," ident] "in"
    lexCheck(&comp->lex, TOK_IDENT);
    strcpy(indexOrKeyName, comp->lex.tok.name);
    lexNext(&comp->lex);

    if (comp->lex.tok.kind == TOK_COMMA)
    {
        lexNext(&comp->lex);
        lexCheck(&comp->lex, TOK_IDENT);
        strcpy(itemName, comp->lex.tok.name);
        lexNext(&comp->lex);
    }

    lexEat(&comp->lex, TOK_IN);

    // expr
    Type *collectionType;
    parseExpr(comp, &collectionType, NULL);

    // Implicit dereferencing: x in a^ == x in a
    if (collectionType->kind == TYPE_PTR)
    {
        genDeref(&comp->gen, collectionType->base->kind);
        collectionType = collectionType->base;
    }

    // Check collection type
    if (collectionType->kind != TYPE_ARRAY && collectionType->kind != TYPE_DYNARRAY && collectionType->kind != TYPE_MAP && collectionType->kind != TYPE_STR)
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        comp->error.handler(comp->error.context, "Expression of type %s is not iterable", typeSpelling(collectionType, typeBuf));
    }

    // Declare variable for the collection length and assign len(expr) to it
    if (collectionType->kind == TYPE_ARRAY)
        genPushIntConst(&comp->gen, collectionType->numItems);
    else
    {
        genDup(&comp->gen);
        genCallBuiltin(&comp->gen, collectionType->kind, BUILTIN_LEN);
    }

    Ident *lenIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, "__len", comp->intType, false);
    doPushVarPtr(comp, lenIdent);
    genSwapAssign(&comp->gen, lenIdent->type->kind, typeSize(&comp->types, lenIdent->type));

    Ident *collectionIdent = NULL;
    if (itemName[0] != '\0' || collectionType->kind == TYPE_MAP)
    {
        // Declare variable for the collection and assign expr to it
        collectionIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, "__collection", collectionType, false);
        doZeroVar(comp, collectionIdent);
        doPushVarPtr(comp, collectionIdent);
        genSwapChangeRefCntAssign(&comp->gen, collectionType);
    }
    else
    {
        // Remove expr
        genPop(&comp->gen);
    }

    // Declare variable for the collection index (for maps, it will be used for indexing keys())
    const char *indexName = (collectionType->kind == TYPE_MAP) ? "__index" : indexOrKeyName;
    Ident *indexIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, indexName, comp->intType, false);
    indexIdent->used = true;                            // Do not warn about unused index
    doZeroVar(comp, indexIdent);

    Ident *keyIdent = NULL, *keysIdent = NULL;
    if (collectionType->kind == TYPE_MAP)
    {
        // Declare variable for the map key
        keyIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, indexOrKeyName, typeMapKey(collectionType), false);
        keyIdent->used = true;                            // Do not warn about unused key
        doZeroVar(comp, keyIdent);

        // Declare variable for the map keys
        Type *keysType = typeAdd(&comp->types, &comp->blocks, TYPE_DYNARRAY);
        keysType->base = typeMapKey(collectionType);
        keysIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, "__keys", keysType, false);
        doZeroVar(comp, keysIdent);

        // Call keys()
        int resultOffset = identAllocStack(&comp->idents, &comp->types, &comp->blocks, keysType);
        doPushVarPtr(comp, collectionIdent);        // Map
        genPushGlobalPtr(&comp->gen, keysType);     // Result type (hidden parameter)
        genPushLocalPtr(&comp->gen, resultOffset);  // Pointer to result (hidden parameter)

        genCallBuiltin(&comp->gen, collectionType->kind, BUILTIN_KEYS);
        doCopyResultToTempVar(comp, keysType);

        // Assign map keys
        doPushVarPtr(comp, keysIdent);
        genSwapChangeRefCntAssign(&comp->gen, keysType);
    }

    Ident *itemIdent = NULL;
    if (itemName[0] != '\0')
    {
        // Declare variable for the collection item
        Type *itemType = NULL;

        if (collectionType->kind == TYPE_MAP)
            itemType = typeMapItem(collectionType);
        else if (collectionType->kind == TYPE_STR)
            itemType = comp->charType;
        else
            itemType = collectionType->base;

        itemIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, itemName, itemType, false);
        doZeroVar(comp, itemIdent);
    }

    genForCondProlog(&comp->gen);

    // Implicit conditional expression: __index < __len
    doPushVarPtr(comp, indexIdent);
    genDeref(&comp->gen, TYPE_INT);
    doPushVarPtr(comp, lenIdent);
    genDeref(&comp->gen, TYPE_INT);
    genBinary(&comp->gen, TOK_LESS, TYPE_INT, 0);

    genForCondEpilog(&comp->gen);

    // Implicit simpleStmt: index++
    doPushVarPtr(comp, indexIdent);
    genUnary(&comp->gen, TOK_PLUSPLUS, TYPE_INT);

    genForPostStmtEpilog(&comp->gen);

    if (collectionType->kind == TYPE_MAP)
    {
        // Assign key = __keys[__index]
        doPushVarPtr(comp, keysIdent);
        doPushVarPtr(comp, indexIdent);
        genDeref(&comp->gen, TYPE_INT);
        genGetDynArrayPtr(&comp->gen);
        genDeref(&comp->gen, keyIdent->type->kind);

        doPushVarPtr(comp, keyIdent);
        genSwapChangeRefCntAssign(&comp->gen, keyIdent->type);
    }

    // Assign collection item
    if (itemIdent)
    {
        doPushVarPtr(comp, collectionIdent);
        genDeref(&comp->gen, collectionType->kind);

        if (collectionType->kind == TYPE_MAP)
        {
            // Push item key
            doPushVarPtr(comp, keyIdent);
            genDeref(&comp->gen, keyIdent->type->kind);
        }
        else
        {
            // Push item index
            doPushVarPtr(comp, indexIdent);
            genDeref(&comp->gen, TYPE_INT);
        }

        switch (collectionType->kind)
        {
            case TYPE_ARRAY:     genGetArrayPtr(&comp->gen, typeSize(&comp->types, collectionType->base), collectionType->numItems); break;
            case TYPE_DYNARRAY:  genGetDynArrayPtr(&comp->gen);                                                                      break;
            case TYPE_STR:       genGetArrayPtr(&comp->gen, typeSize(&comp->types, comp->charType), INT_MAX);                        break; // No range checking
            case TYPE_MAP:       genGetMapPtr(&comp->gen, collectionType);                                                           break;
            default:             break;
        }

        // Get collection item value
        genDeref(&comp->gen, itemIdent->type->kind);

        // Assign collection item to iteration variable
        doPushVarPtr(comp, itemIdent);
        genSwapChangeRefCntAssign(&comp->gen, itemIdent->type);
    }
}


// forStmt = "for" (forHeader | forInHeader) block.
static void parseForStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_FOR);

    // Additional scope embracing shortVarDecl in forHeader/forEachHeader and statement body
    blocksEnter(&comp->blocks, NULL);

    // 'break'/'continue' prologs
    Gotos breaks, *outerBreaks = comp->gen.breaks;
    comp->gen.breaks = &breaks;
    genGotosProlog(&comp->gen, comp->gen.breaks, blocksCurrent(&comp->blocks));

    Gotos continues, *outerContinues = comp->gen.continues;
    comp->gen.continues = &continues;
    genGotosProlog(&comp->gen, comp->gen.continues, blocksCurrent(&comp->blocks));

    Lexer lookaheadLex = comp->lex;
    lexNext(&lookaheadLex);

    if (!doShortVarDeclLookahead(comp) && (lookaheadLex.tok.kind == TOK_COMMA || lookaheadLex.tok.kind == TOK_IN))
        parseForInHeader(comp);
    else
        parseForHeader(comp);

    // block
    parseBlock(comp);

    // 'continue' epilog
    genGotosEpilog(&comp->gen, comp->gen.continues);
    comp->gen.continues = outerContinues;

    genForEpilog(&comp->gen);

    // 'break' epilog
    genGotosEpilog(&comp->gen, comp->gen.breaks);
    comp->gen.breaks = outerBreaks;

    // Additional scope embracing shortVarDecl in forHeader/forEachHeader and statement body
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
}


// breakStmt = "break".
static void parseBreakStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_BREAK);

    if (!comp->gen.breaks)
        comp->error.handler(comp->error.context, "No loop to break");

    doGarbageCollectionDownToBlock(comp, comp->gen.breaks->block);
    genGotosAddStub(&comp->gen, comp->gen.breaks);
}


// continueStmt = "continue".
static void parseContinueStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_CONTINUE);

    if (!comp->gen.continues)
        comp->error.handler(comp->error.context, "No loop to continue");

    doGarbageCollectionDownToBlock(comp, comp->gen.continues->block);
    genGotosAddStub(&comp->gen, comp->gen.continues);
}


// returnStmt = "return" [exprOrLitList].
static void parseReturnStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_RETURN);
    comp->blocks.item[comp->blocks.top].hasReturn = true;

    // Get function signature
    Signature *sig = NULL;
    for (int i = comp->blocks.top; i >= 1; i--)
        if (comp->blocks.item[i].fn)
        {
            sig = &comp->blocks.item[i].fn->type->sig;
            break;
        }

    Type *type;
    if (comp->lex.tok.kind != TOK_SEMICOLON && comp->lex.tok.kind != TOK_IMPLICIT_SEMICOLON && comp->lex.tok.kind != TOK_RBRACE)
        parseExprOrUntypedLiteralList(comp, &type, sig->resultType, NULL);
    else
        type = comp->voidType;

    doImplicitTypeConv(comp, sig->resultType, &type, NULL, false);
    typeAssertCompatible(&comp->types, sig->resultType, type, false);

    // Copy structure to __result
    if (typeStructured(sig->resultType))
    {
        Ident *result = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, "__result", NULL);

        doPushVarPtr(comp, result);
        genDeref(&comp->gen, TYPE_PTR);

        // Assignment to an anonymous stack area (pointed to by __result) does not require updating reference counts
        genSwapAssign(&comp->gen, sig->resultType->kind, typeSize(&comp->types, sig->resultType));

        doPushVarPtr(comp, result);
        genDeref(&comp->gen, TYPE_PTR);
    }

    if (sig->resultType->kind != TYPE_VOID)
    {
        if (doTryRemoveCopyResultToTempVar(comp))
        {
            // Optimization: if the result expression is a function call, assume its reference count to be already increased before the inner return
            // The outer caller will hold this additional reference, so we can remove the temporary "reference holder" variable
        }
        else
        {
            // General case: increase result reference count
            genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, sig->resultType);
        }
        genPopReg(&comp->gen, VM_REG_RESULT);
    }

    doGarbageCollectionDownToBlock(comp, comp->gen.returns->block);
    genGotosAddStub(&comp->gen, comp->gen.returns);
}


// stmt = decl | block | simpleStmt | ifStmt | switchStmt | forStmt | breakStmt | continueStmt | returnStmt.
static void parseStmt(Compiler *comp)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_TYPE:
        case TOK_CONST:
        case TOK_VAR:       parseDecl(comp);            break;
        case TOK_LBRACE:    parseBlock(comp);           break;
        case TOK_IDENT:
        case TOK_CARET:
        case TOK_WEAK:
        case TOK_LBRACKET:
        case TOK_STR:
        case TOK_STRUCT:
        case TOK_INTERFACE:
        case TOK_FN:        parseSimpleStmt(comp);      break;
        case TOK_IF:        parseIfStmt(comp);          break;
        case TOK_SWITCH:    parseSwitchStmt(comp);      break;
        case TOK_FOR:       parseForStmt(comp);         break;
        case TOK_BREAK:     parseBreakStmt(comp);       break;
        case TOK_CONTINUE:  parseContinueStmt(comp);    break;
        case TOK_RETURN:    parseReturnStmt(comp);      break;

        default: break;
    }
}


// stmtList = Stmt {";" Stmt}.
static void parseStmtList(Compiler *comp)
{
    while (1)
    {
        parseStmt(comp);
        if (comp->lex.tok.kind != TOK_SEMICOLON && comp->lex.tok.kind != TOK_IMPLICIT_SEMICOLON)
            break;
        lexNext(&comp->lex);
    };
}


// block = "{" StmtList "}".
static void parseBlock(Compiler *comp)
{
    lexEat(&comp->lex, TOK_LBRACE);
    blocksEnter(&comp->blocks, NULL);

    parseStmtList(comp);

    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    identFree(&comp->idents, blocksCurrent(&comp->blocks));

    blocksLeave(&comp->blocks);
    lexEat(&comp->lex, TOK_RBRACE);
}


// fnBlock = block.
void parseFnBlock(Compiler *comp, Ident *fn, Type *upvaluesStructType)
{
    lexEat(&comp->lex, TOK_LBRACE);
    blocksEnter(&comp->blocks, fn);

    char *prevDebugFnName = comp->lex.debug->fnName;

    if (fn && fn->kind == IDENT_CONST && fn->type->kind == TYPE_FN && fn->block == 0)
    {
        if (fn->type->sig.isMethod)
        {
            comp->lex.debug->fnName = storageAdd(&comp->storage, DEFAULT_STR_LEN + 1);
            identMethodNameWithRcv(fn, comp->lex.debug->fnName, DEFAULT_STR_LEN + 1);
        }
        else
            comp->lex.debug->fnName = fn->name;
    }
    else
        comp->lex.debug->fnName = "<unknown>";

    if (fn->prototypeOffset >= 0)
    {
        genEntryPoint(&comp->gen, fn->prototypeOffset);
        fn->prototypeOffset = -1;
    }

    genEnterFrameStub(&comp->gen);

    // Formal parameters
    for (int i = 0; i < fn->type->sig.numParams; i++)
        identAllocParam(&comp->idents, &comp->types, &comp->modules, &comp->blocks, &fn->type->sig, i);

    // Upvalues
    if (upvaluesStructType)
    {
        // Extract upvalues structure from the "any" interface
        Ident *upvaluesParamIdent = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, "__upvalues", NULL);
        Type *upvaluesParamType = upvaluesParamIdent->type;

        doPushVarPtr(comp, upvaluesParamIdent);
        genDeref(&comp->gen, upvaluesParamIdent->type->kind);
        doExplicitTypeConv(comp, upvaluesStructType, &upvaluesParamType, NULL, false);

        // Copy upvalue structure fields to new local variables
        for (int i = 0; i < upvaluesStructType->numItems; i++)
        {
            Field *upvalue = upvaluesStructType->field[i];

            genDup(&comp->gen);
            genGetFieldPtr(&comp->gen, upvalue->offset);
            genDeref(&comp->gen, upvalue->type->kind);

            Ident *upvalueIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, upvalue->name, upvalue->type, false);
            doZeroVar(comp, upvalueIdent);
            doPushVarPtr(comp, upvalueIdent);

            genSwapChangeRefCntAssign(&comp->gen, upvalue->type);
        }

        genPop(&comp->gen);
    }

    // 'return' prolog
    Gotos returns, *outerReturns = comp->gen.returns;
    comp->gen.returns = &returns;
    genGotosProlog(&comp->gen, comp->gen.returns, blocksCurrent(&comp->blocks));

    // StmtList
    parseStmtList(comp);

    const bool hasReturn = comp->blocks.item[comp->blocks.top].hasReturn;

    // 'return' epilog
    genGotosEpilog(&comp->gen, comp->gen.returns);
    comp->gen.returns = outerReturns;

    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identWarnIfUnusedAll(&comp->idents, blocksCurrent(&comp->blocks));
    identFree(&comp->idents, blocksCurrent(&comp->blocks));

    const int localVarSlots = align(comp->blocks.item[comp->blocks.top].localVarSize, sizeof(Slot)) / sizeof(Slot);
    const int paramSlots    = align(typeParamSizeTotal(&comp->types, &fn->type->sig), sizeof(Slot)) / sizeof(Slot);

    genLeaveFrameFixup(&comp->gen, localVarSlots);
    genReturn(&comp->gen, paramSlots);

    comp->lex.debug->fnName = prevDebugFnName;

    blocksLeave(&comp->blocks);
    lexEat(&comp->lex, TOK_RBRACE);

    if (!hasReturn && fn->type->sig.resultType->kind != TYPE_VOID)
        comp->error.handler(comp->error.context, "Non-void function must have a return statement");
}


// fnPrototype = .
void parseFnPrototype(Compiler *comp, Ident *fn)
{
    fn->prototypeOffset = fn->offset;
    genNop(&comp->gen);
}

