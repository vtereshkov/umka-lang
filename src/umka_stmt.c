#include <string.h>

#include "umka_stmt.h"
#include "umka_expr.h"
#include "umka_decl.h"


static void parseStmtList(Compiler *comp);
static void parseBlock(Compiler *comp);


static void doGarbageCollection(Compiler *comp, int block)
{
    for (Ident *ident = comp->idents.first; ident; ident = ident->next)
        if (ident->kind == IDENT_VAR && ident->block == block && typeGarbageCollected(ident->type))
        {
            doPushVarPtr(comp, ident);
            genDeref(&comp->gen, ident->type->kind);
            genChangeRefCnt(&comp->gen, TOK_MINUSMINUS, ident->type);
            genPop(&comp->gen);
        }
}


static void doGarbageCollectionDownToBlock(Compiler *comp, int block)
{
    // Collect garbage over all scopes down to the specified block (not inclusive)
    for (int i = comp->blocks.top; i >= 1; i--)
    {
        if (comp->blocks.item[i].block == block)
            break;
        doGarbageCollection(comp, comp->blocks.item[i].block);
    }
}


void doResolveExtern(Compiler *comp)
{
    for (Ident *ident = comp->idents.first; ident; ident = ident->next)
        if (ident->prototypeOffset >= 0)
        {
            External *external = externalFind(&comp->externals, ident->name);
            if (!external)
                comp->error.handler(comp->error.context, "Unresolved prototype of %s", ident->name);

            // All parameters must be declared since they may require garbage collection
            blocksEnter(&comp->blocks, ident);
            genEntryPoint(&comp->gen, ident->prototypeOffset);
            genEnterFrameStub(&comp->gen);

            for (int i = 0; i < ident->type->sig.numParams; i++)
                identAllocParam(&comp->idents, &comp->types, &comp->modules, &comp->blocks, &ident->type->sig, i);

            genCallExtern(&comp->gen, external->entry);

            doGarbageCollection(comp, blocksCurrent(&comp->blocks));
            identFree(&comp->idents, blocksCurrent(&comp->blocks));
            genLeaveFrameFixup(&comp->gen, 0);

            int paramSlots = typeParamSizeTotal(&comp->types, &ident->type->sig) / sizeof(Slot);
            genReturn(&comp->gen, paramSlots);

            blocksLeave(&comp->blocks);
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


// singleAssignmentStmt = designator "=" expr.
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
    parseExpr(comp, &rightType, rightConstant);

    doImplicitTypeConv(comp, type, &rightType, rightConstant, false);
    typeAssertCompatible(&comp->types, type, rightType, false);

    if (varPtrConst)                                // Initialize global variable
        constAssign(&comp->consts, (void *)varPtrConst->ptrVal, rightConstant, type->kind, typeSize(&comp->types, type));
    else                                            // Assign to variable
        genChangeRefCntAssign(&comp->gen, type);
}


// listAssignmentStmt = designatorList "=" exprList.
static void parseListAssignmentStmt(Compiler *comp, Type *type, Const *varPtrConstList)
{
    Type *rightListType;
    Const rightListConstantBuf, *rightListConstant = varPtrConstList ? &rightListConstantBuf : NULL;
    parseExprList(comp, &rightListType, NULL, rightListConstant);

    const int numExpr = typeExprListStruct(rightListType) ? rightListType->numItems : 1;

    if (numExpr < type->numItems) comp->error.handler(comp->error.context, "Too few expressions");
    if (numExpr > type->numItems) comp->error.handler(comp->error.context, "Too many expressions");

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
            Const rightConstantBuf = {.ptrVal = rightListConstant->ptrVal + rightListType->field[i]->offset};
            constDeref(&comp->consts, &rightConstantBuf, rightType->kind);

            doImplicitTypeConv(comp, leftType, &rightType, &rightConstantBuf, false);
            typeAssertCompatible(&comp->types, leftType, rightType, false);

            constAssign(&comp->consts, (void *)varPtrConstList[i].ptrVal, &rightConstantBuf, rightType->kind, typeSize(&comp->types, rightType));
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


// shortAssignmentStmt = designator ("+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "~=") expr.
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

    // All temporary reals are 64-bit
    if (type->kind == TYPE_REAL32)
        type = comp->realType;

    Type *rightType;
    parseExpr(comp, &rightType, NULL);

    doApplyOperator(comp, &type, &rightType, NULL, NULL, lexShortAssignment(op), true, false);
    genChangeRefCntAssign(&comp->gen, type);
}


// singleDeclAssignmentStmt = ident ":=" expr.
static void parseSingleDeclAssignmentStmt(Compiler *comp, IdentName name, bool exported, bool constExpr)
{
    Type *rightType;
    Const rightConstantBuf, *rightConstant = constExpr ? &rightConstantBuf : NULL;
    parseExpr(comp, &rightType, rightConstant);

    Ident *ident = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, name, rightType, exported);

    if (constExpr)              // Initialize global variable
        constAssign(&comp->consts, ident->ptr, rightConstant, rightType->kind, typeSize(&comp->types, rightType));
    else                        // Assign to variable
    {
        // Increase right-hand side reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, rightType);

        doPushVarPtr(comp, ident);
        genSwapAssign(&comp->gen, rightType->kind, typeSize(&comp->types, rightType));
    }
}


// listDeclAssignmentStmt = identList ":=" exprList.
static void parseListDeclAssignmentStmt(Compiler *comp, IdentName *names, bool *exported, int num, bool constExpr)
{
    Type *rightListType;
    Const rightListConstantBuf, *rightListConstant = constExpr ? &rightListConstantBuf : NULL;
    parseExprList(comp, &rightListType, NULL, rightListConstant);

    if (rightListType->numItems < num) comp->error.handler(comp->error.context, "Too few expressions");
    if (rightListType->numItems > num) comp->error.handler(comp->error.context, "Too many expressions");

    for (int i = 0; i < num; i++)
    {
        Type *rightType = rightListType->field[i]->type;
        Ident *ident = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, names[i], rightType, exported[i]);

        if (constExpr)              // Initialize global variable
        {
            Const rightConstantBuf = {.ptrVal = rightListConstant->ptrVal + rightListType->field[i]->offset};
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

        if (typeExprListStruct(type) && op != TOK_EQ)
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
    blocksLeave(&comp->blocks);
}


// case = "case" expr {"," expr} ":" stmtList.
static void parseCase(Compiler *comp, Type *selectorType)
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
    parseStmtList(comp);
    genCaseBlockEpilog(&comp->gen);
}


// default = "default" ":" stmtList.
static void parseDefault(Compiler *comp)
{
    lexEat(&comp->lex, TOK_DEFAULT);
    lexEat(&comp->lex, TOK_COLON);
    parseStmtList(comp);
}


// switchStmt = "switch" [shortVarDecl ";"] expr "{" {case} [default] "}".
static void parseSwitchStmt(Compiler *comp)
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

    // "{" {case} "}"
    lexEat(&comp->lex, TOK_LBRACE);

    int numCases = 0;
    while (comp->lex.tok.kind == TOK_CASE)
    {
        parseCase(comp, type);
        numCases++;
    }

    // [default]
    if (comp->lex.tok.kind == TOK_DEFAULT)
        parseDefault(comp);

    lexEat(&comp->lex, TOK_RBRACE);

    genSwitchEpilog(&comp->gen, numCases);

    // Additional scope embracing shortVarDecl and statement body
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
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
    blocksLeave(&comp->blocks);

    genForCondEpilog(&comp->gen);

    // [";" simpleStmt]
    if (comp->lex.tok.kind == TOK_SEMICOLON)
    {
        // Additional scope embracing simpleStmt (needed for timely garbage collection in simpleStmt, since it is executed at each iteration)
        blocksEnter(&comp->blocks, NULL);

        lexNext(&comp->lex);
        parseSimpleStmt(comp);

        // Additional scope embracing simpleStmt
        doGarbageCollection(comp, blocksCurrent(&comp->blocks));
        blocksLeave(&comp->blocks);
    }

    genForPostStmtEpilog(&comp->gen);
}


// forInHeader = [ident ","] ident "in" expr.
static void parseForInHeader(Compiler *comp, TokenKind lookaheadTokKind)
{
    Ident *indexIdent = NULL, *itemIdent = NULL;
    Type *collectionType;
    IdentName itemName;

    // [ident ","] ident "in"
    lexCheck(&comp->lex, TOK_IDENT);

    if (lookaheadTokKind == TOK_COMMA)
    {
        indexIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, comp->lex.tok.name, comp->intType, false);

        lexEat(&comp->lex, TOK_IDENT);
        lexEat(&comp->lex, TOK_COMMA);
        lexCheck(&comp->lex, TOK_IDENT);
    }
    else if (lookaheadTokKind == TOK_IN)
        indexIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, "__index", comp->intType, false);

    // Zero index
    doPushVarPtr(comp, indexIdent);
    genPushIntConst(&comp->gen, 0);
    genAssign(&comp->gen, TYPE_INT, 0);

    strcpy(itemName, comp->lex.tok.name);

    lexNext(&comp->lex);
    lexEat(&comp->lex, TOK_IN);

    genForCondProlog(&comp->gen);

    // Additional scope embracing expr (needed for timely garbage collection in expr, since it is computed at each iteration)
    blocksEnter(&comp->blocks, NULL);

    // Implicit conditional expr: len(collection) >= index
    parseExpr(comp, &collectionType, NULL);

    // Implicit dereferencing: x in a^ == x in a
    if (collectionType->kind == TYPE_PTR)
    {
        if (!typeStructured(collectionType->base))
            genDeref(&comp->gen, collectionType->base->kind);
        collectionType = collectionType->base;
    }

    // Save collection for future use
    genDup(&comp->gen);
    genPopReg(&comp->gen, VM_REG_COMMON_2);

    if (collectionType->kind == TYPE_ARRAY)
    {
        genPop(&comp->gen);
        genPushIntConst(&comp->gen, collectionType->numItems);
    }
    else if (collectionType->kind == TYPE_DYNARRAY || collectionType->kind == TYPE_STR)
    {
        genCallBuiltin(&comp->gen, collectionType->kind, BUILTIN_LEN);
    }
    else
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        comp->error.handler(comp->error.context, "Expression of type %s is not iterable", typeSpelling(collectionType, typeBuf));
    }

    doPushVarPtr(comp, indexIdent);
    genDeref(&comp->gen, TYPE_INT);
    genBinary(&comp->gen, TOK_GREATER, TYPE_INT, 0);

    // Additional scope embracing expr
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);

    genForCondEpilog(&comp->gen);

    // Declare variable for collection item
    Type *itemType = (collectionType->kind == TYPE_STR) ? comp->charType : collectionType->base;
    itemIdent = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, itemName, itemType, false);

    // Additional scope embracing simpleStmt (needed for timely garbage collection in simpleStmt, since it is executed at each iteration)
    blocksEnter(&comp->blocks, NULL);

    // Implicit simpleStmt: index++
    doPushVarPtr(comp, indexIdent);
    genUnary(&comp->gen, TOK_PLUSPLUS, TYPE_INT);

    // Additional scope embracing simpleStmt
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);

    genForPostStmtEpilog(&comp->gen);

    // Get collection item pointer
    genPushReg(&comp->gen, VM_REG_COMMON_2);
    doPushVarPtr(comp, indexIdent);
    genDeref(&comp->gen, TYPE_INT);

    if (collectionType->kind == TYPE_DYNARRAY)
    {
        genGetDynArrayPtr(&comp->gen);
    }
    else if (collectionType->kind == TYPE_STR)
    {
        genPushIntConst(&comp->gen, -1);                            // Use actual length for range checking
        genGetArrayPtr(&comp->gen, typeSize(&comp->types, itemType));
    }
    else // TYPE_ARRAY
    {
        genPushIntConst(&comp->gen, collectionType->numItems);      // Use nominal length for range checking
        genGetArrayPtr(&comp->gen, typeSize(&comp->types, itemType));
    }

    // Get collection item value
    if (!typeStructured(itemType))
        genDeref(&comp->gen, itemType->kind);

    // Assign collection item to iteration variable
    doPushVarPtr(comp, itemIdent);
    genSwapChangeRefCntAssign(&comp->gen, itemType);
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
        parseForInHeader(comp, lookaheadLex.tok.kind);
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


// returnStmt = "return" [exprList].
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
    if (comp->lex.tok.kind != TOK_SEMICOLON && comp->lex.tok.kind != TOK_RBRACE)
        parseExprList(comp, &type, sig->resultType, NULL);
    else
        type = comp->voidType;

    doImplicitTypeConv(comp, sig->resultType, &type, NULL, false);
    typeAssertCompatible(&comp->types, sig->resultType, type, false);

    // Copy structure to __result
    if (typeStructured(sig->resultType))
    {
        Ident *__result = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, "__result", NULL);

        doPushVarPtr(comp, __result);
        genDeref(&comp->gen, TYPE_PTR);

        // Assignment to an anonymous stack area (pointed to by __result) does not require updating reference counts
        genSwapAssign(&comp->gen, sig->resultType->kind, typeSize(&comp->types, sig->resultType));

        doPushVarPtr(comp, __result);
        genDeref(&comp->gen, TYPE_PTR);
    }

    if (sig->resultType->kind != TYPE_VOID)
    {
        // Increase result reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, sig->resultType);
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
        if (comp->lex.tok.kind != TOK_SEMICOLON)
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
    identFree(&comp->idents, blocksCurrent(&comp->blocks));

    blocksLeave(&comp->blocks);
    lexEat(&comp->lex, TOK_RBRACE);
}


// fnBlock = block.
void parseFnBlock(Compiler *comp, Ident *fn)
{
    lexEat(&comp->lex, TOK_LBRACE);
    blocksEnter(&comp->blocks, fn);

    bool mainFn = false;
    if (strcmp(fn->name, "main") == 0)
    {
        if (fn->type->sig.method || fn->type->sig.numParams != 0 || fn->type->sig.resultType->kind != TYPE_VOID)
            comp->error.handler(comp->error.context, "Illegal main() signature");

        genEntryPoint(&comp->gen, 0);
        mainFn = true;
    }
    else if (fn->prototypeOffset >= 0)
    {
        genEntryPoint(&comp->gen, fn->prototypeOffset);
        fn->prototypeOffset = -1;
    }

    genEnterFrameStub(&comp->gen);
    for (int i = 0; i < fn->type->sig.numParams; i++)
        identAllocParam(&comp->idents, &comp->types, &comp->modules, &comp->blocks, &fn->type->sig, i);

    // 'return' prolog
    Gotos returns, *outerReturns = comp->gen.returns;
    comp->gen.returns = &returns;
    genGotosProlog(&comp->gen, comp->gen.returns, blocksCurrent(&comp->blocks));

    // StmtList
    parseStmtList(comp);

    if (!comp->blocks.item[comp->blocks.top].hasReturn && fn->type->sig.resultType->kind != TYPE_VOID)
        comp->error.handler(comp->error.context, "Non-void function block must have return statement");

    // 'return' epilog
    genGotosEpilog(&comp->gen, comp->gen.returns);
    comp->gen.returns = outerReturns;

    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    identFree(&comp->idents, blocksCurrent(&comp->blocks));

    genLeaveFrameFixup(&comp->gen, comp->blocks.item[comp->blocks.top].localVarSize);

    if (mainFn)
    {
        doGarbageCollection(comp, 0);
        genHalt(&comp->gen);
    }
    else
    {
        int paramSlots = typeParamSizeTotal(&comp->types, &fn->type->sig) / sizeof(Slot);
        genReturn(&comp->gen, paramSlots);
    }

    blocksLeave(&comp->blocks);
    lexEat(&comp->lex, TOK_RBRACE);
}


// fnPrototype = .
void parseFnPrototype(Compiler *comp, Ident *fn)
{
    fn->prototypeOffset = fn->offset;
    genNop(&comp->gen);
}

