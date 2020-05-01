#include <string.h>

#include "umka_stmt.h"
#include "umka_expr.h"
#include "umka_decl.h"


void parseStmtList(Compiler *comp);


void doResolveExtern(Compiler *comp)
{
    for (Ident *ident = comp->idents.first; ident; ident = ident->next)
        if (ident->prototypeOffset >= 0)
        {
            External *external = externalFind(&comp->externals, ident->name);
            if (external)
            {
                int paramSlots = typeParamSizeTotal(&comp->types, &ident->type->sig) / sizeof(Slot);

                genEntryPoint(&comp->gen, ident->prototypeOffset);
                genCallExtern(&comp->gen, external->entry);
                genReturn(&comp->gen, paramSlots);
            }
            else
                comp->error("Unresolved prototype of %s", ident->name);
        }
}


static void doGarbageCollection(Compiler *comp, bool collectGlobals)
{
    int block = collectGlobals ? 0 : comp->blocks.item[comp->blocks.top].block;

    for (Ident *ident = comp->idents.first; ident; ident = ident->next)
        if (ident->kind == IDENT_VAR && ident->block == block)
            doUpdateRefCnt(comp, ident->type, ident, true, false, 0);
}


// assignmentStmt = designator "=" expr.
void parseAssignmentStmt(Compiler *comp, Type *type, void *initializedVarPtr)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error("Left side cannot be assigned to");
        type = type->base;
    }

    // Decrease old left-hand side reference count
    doUpdateRefCnt(comp, type, NULL, true, false, 0);

    Type *rightType;
    Const rightConstantBuf, *rightConstant = NULL;

    if (initializedVarPtr)
        rightConstant = &rightConstantBuf;

    parseExpr(comp, &rightType, rightConstant);

    doImplicitTypeConv(comp, type, &rightType, rightConstant, false);
    typeAssertCompatible(&comp->types, type, rightType);

    if (initializedVarPtr)      // Initialize global variable
        constAssign(&comp->consts, initializedVarPtr, rightConstant, type->kind, typeSize(&comp->types, type));
    else                        // Assign to local variable
    {
        // Increase right-hand side reference count
        doUpdateRefCnt(comp, type, NULL, false, true, 0);
        genAssign(&comp->gen, type->kind, typeSize(&comp->types, type));
    }

}


// shortAssignmentStmt = designator ("+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "~=") expr.
void parseShortAssignmentStmt(Compiler *comp, Type *type, TokenKind op)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error("Left side cannot be assigned to");
        type = type->base;
    }

    // Duplicate designator and treat it as an expression
    genDup(&comp->gen);
    genDeref(&comp->gen, type->kind);

    // No direct support for 32-bit reals
    if (type->kind == TYPE_REAL32)
        type = comp->realType;

    // Decrease old left-hand side reference count
    doUpdateRefCnt(comp, type, NULL, true, false, 0);

    Type *rightType;
    parseExpr(comp, &rightType, NULL);

    doApplyOperator(comp, &type, &rightType, NULL, NULL, lexShortAssignment(op), true, false);

    // Increase right-hand side reference count
    doUpdateRefCnt(comp, type, NULL, false, true, 0);
    genAssign(&comp->gen, type->kind, typeSize(&comp->types, type));
}


// declAssignmentStmt = ident ":=" expr.
void parseDeclAssignmentStmt(Compiler *comp, IdentName name, bool constExpr, bool exported)
{
    Type *rightType;
    Const rightConstantBuf, *rightConstant = NULL;

    if (constExpr)
        rightConstant = &rightConstantBuf;

    parseExpr(comp, &rightType, rightConstant);

    identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, name, rightType, exported);
    Ident *ident = comp->idents.last;

    if (constExpr)                              // Initialize global variable
        constAssign(&comp->consts, ident->ptr, rightConstant, rightType->kind, typeSize(&comp->types, rightType));
    else                                        // Assign to local variable
    {
        doPushVarPtr(comp, ident);
        genSwap(&comp->gen);                    // Assignment requires that the left-hand side comes first

        // Increase right-hand side reference count
        doUpdateRefCnt(comp, rightType, NULL, false, true, 0);
        genAssign(&comp->gen, rightType->kind, typeSize(&comp->types, rightType));
    }
}


// incDecStmt = designator ("++" | "--").
void parseIncDecStmt(Compiler *comp, Type *type, TokenKind op)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error("Left side cannot be assigned to");
        type = type->base;
    }

    typeAssertCompatible(&comp->types, comp->intType, type);
    genUnary(&comp->gen, op, type->kind);
    lexNext(&comp->lex);
}


// simpleStmt     = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
// callStmt       = designator.
void parseSimpleStmt(Compiler *comp)
{
    Lexer lookaheadLex = comp->lex;
    lexNext(&lookaheadLex);

    if (lookaheadLex.tok.kind == TOK_COLONEQ)
        parseShortVarDecl(comp);
    else
    {
        Ident *ident = parseQualIdent(comp);
        Type *type;
        bool isVar, isCall;
        parseDesignator(comp, ident, &type, NULL, &isVar, &isCall);

        TokenKind op = comp->lex.tok.kind;
        if (op == TOK_EQ || lexShortAssignment(op) != TOK_NONE)
        {
            // Assignment
            if (!isVar)
                comp->error("Left side cannot be assigned to");
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
                comp->error("Assignment or function call expected");
            if (type->kind != TYPE_VOID)
                genPop(&comp->gen);  // Manually remove parameter
        }
    }
}


// ifStmt = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
void parseIfStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_IF);

    // Additional scope embracing shortVarDecl
    blocksEnter(&comp->blocks, NULL);

    // [shortVarDecl ";"]
    Lexer lookaheadLex = comp->lex;
    lexNext(&lookaheadLex);

    if (lookaheadLex.tok.kind == TOK_COLONEQ)
    {
        parseShortVarDecl(comp);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }

    // expr
    Type *type;
    parseExpr(comp, &type, NULL);
    typeAssertCompatible(&comp->types, comp->boolType, type);

    genIfCondEpilog(&comp->gen);

    // block
    parseBlock(comp, NULL);

    // ["else" (ifStmt | block)]
    if (comp->lex.tok.kind == TOK_ELSE)
    {
        genElseProlog(&comp->gen);
        lexNext(&comp->lex);

        if (comp->lex.tok.kind == TOK_IF)
            parseIfStmt(comp);
        else
            parseBlock(comp, NULL);
    }

    genIfElseEpilog(&comp->gen);

    // Additional scope embracing shortVarDecl
    doGarbageCollection(comp, false);
    blocksLeave(&comp->blocks);
}


// case = "case" expr {"," expr} ":" stmtList.
void parseCase(Compiler *comp, Type *selectorType)
{
    lexEat(&comp->lex, TOK_CASE);

    // expr {"," expr}
    while (1)
    {
        Const constant;
        Type *type;
        parseExpr(comp, &type, &constant);
        typeAssertCompatible(&comp->types, selectorType, type);

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
void parseDefault(Compiler *comp)
{
    lexEat(&comp->lex, TOK_DEFAULT);
    lexEat(&comp->lex, TOK_COLON);
    parseStmtList(comp);
}


// switchStmt = "switch" [shortVarDecl ";"] expr "{" {case} [default] "}".
void parseSwitchStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_SWITCH);

    // Additional scope embracing shortVarDecl
    blocksEnter(&comp->blocks, NULL);

    // [shortVarDecl ";"]
    Lexer lookaheadLex = comp->lex;
    lexNext(&lookaheadLex);

    if (lookaheadLex.tok.kind == TOK_COLONEQ)
    {
        parseShortVarDecl(comp);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }

    // expr
    Type *type;
    parseExpr(comp, &type, NULL);
    if (!typeOrdinal(type))
        comp->error("Ordinal type expected");

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

    // Additional scope embracing shortVarDecl
    doGarbageCollection(comp, false);
    blocksLeave(&comp->blocks);
}


// forStmt = "for" [shortVarDecl ";"] expr [";" simpleStmt] block.
void parseForStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_FOR);

    // break/continue prologs
    Gotos breaks, *outerBreaks = comp->gen.breaks;
    comp->gen.breaks = &breaks;
    genGotosProlog(&comp->gen, comp->gen.breaks);

    Gotos continues, *outerContinues = comp->gen.continues;
    comp->gen.continues = &continues;
    genGotosProlog(&comp->gen, comp->gen.continues);

    // Additional scope embracing shortVarDecl
    blocksEnter(&comp->blocks, NULL);

    // [shortVarDecl ";"]
    Lexer lookaheadLex = comp->lex;
    lexNext(&lookaheadLex);

    if (lookaheadLex.tok.kind == TOK_COLONEQ)
    {
        parseShortVarDecl(comp);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }

    genForCondProlog(&comp->gen);

    // expr
    Type *type;
    parseExpr(comp, &type, NULL);
    typeAssertCompatible(&comp->types, comp->boolType, type);

    genForCondEpilog(&comp->gen);

    // [";" simpleStmt]
    if (comp->lex.tok.kind == TOK_SEMICOLON)
    {
        lexNext(&comp->lex);
        parseSimpleStmt(comp);
    }

    genForPostStmtEpilog(&comp->gen);

    // block
    parseBlock(comp, NULL);

    // continue epilog
    genGotosEpilog(&comp->gen, comp->gen.continues);
    comp->gen.continues = outerContinues;

    genForEpilog(&comp->gen);

    // break epilog
    genGotosEpilog(&comp->gen, comp->gen.breaks);
    comp->gen.breaks = outerBreaks;

    // Additional scope embracing shortVarDecl
    doGarbageCollection(comp, false);
    blocksLeave(&comp->blocks);
}


// breakStmt = "break".
void parseBreakStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_BREAK);

    if (!comp->gen.breaks)
        comp->error("No loop to break");

    // Leave block scope
    doGarbageCollection(comp, false);
    genGotosAddStub(&comp->gen, comp->gen.breaks);
}


// continueStmt = "continue".
void parseContinueStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_CONTINUE);

    if (!comp->gen.continues)
        comp->error("No loop to continue");

    // Leave block scope
    doGarbageCollection(comp, false);
    genGotosAddStub(&comp->gen, comp->gen.continues);
}


// returnStmt = "return" [expr].
void parseReturnStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_RETURN);
    Type *type;

    if (comp->lex.tok.kind != TOK_SEMICOLON && comp->lex.tok.kind != TOK_RBRACE)
        parseExpr(comp, &type, NULL);
    else
    {
        type = comp->voidType;

        if (comp->lex.tok.kind == TOK_SEMICOLON)
            lexNext(&comp->lex);
    }

    // Get function signature
    Signature *sig = NULL;
    for (int i = comp->blocks.top; i >= 1; i--)
        if (comp->blocks.item[i].fn)
        {
            sig = &comp->blocks.item[i].fn->type->sig;
            break;
        }

    doImplicitTypeConv(comp, sig->resultType[0], &type, NULL, false);
    typeAssertCompatible(&comp->types, sig->resultType[0], type);

    // Copy structure to __result
    if (typeStructured(sig->resultType[0]))
    {
        Ident *__result = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, "__result", NULL);

        doPushVarPtr(comp, __result);
        genDeref(&comp->gen, TYPE_PTR);
        genSwap(&comp->gen);
        genAssign(&comp->gen, sig->resultType[0]->kind, typeSize(&comp->types, sig->resultType[0]));

        doPushVarPtr(comp, __result);
        genDeref(&comp->gen, TYPE_PTR);
    }

    if (sig->resultType[0]->kind != TYPE_VOID)
        genPopReg(&comp->gen, VM_RESULT_REG_0);

    // Leave block scope
    doGarbageCollection(comp, false);
    genGotosAddStub(&comp->gen, comp->gen.returns);
}


// stmt = decl | block | simpleStmt | ifStmt | switchStmt | forStmt | breakStmt | continueStmt | returnStmt.
void parseStmt(Compiler *comp)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_TYPE:
        case TOK_CONST:
        case TOK_VAR:
        case TOK_FN:        parseDecl(comp);            break;
        case TOK_LBRACE:    parseBlock(comp, NULL);     break;
        case TOK_IDENT:     parseSimpleStmt(comp);      break;
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
void parseStmtList(Compiler *comp)
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
void parseBlock(Compiler *comp, Ident *fn)
{
    lexEat(&comp->lex, TOK_LBRACE);
    blocksEnter(&comp->blocks, fn);

    Gotos returns;

    bool mainFn = false;
    if (fn)
    {
        if (strcmp(fn->name, "main") == 0)
        {
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

        comp->gen.returns = &returns;
        genGotosProlog(&comp->gen, comp->gen.returns);
    }

    parseStmtList(comp);
    doGarbageCollection(comp, false);

    if (fn)
    {
        genGotosEpilog(&comp->gen, comp->gen.returns);
        genLeaveFrameFixup(&comp->gen, comp->blocks.item[comp->blocks.top].localVarSize);

        if (mainFn)
        {
            doGarbageCollection(comp, true);
            genHalt(&comp->gen);
        }
        else
        {
            int paramSlots = typeParamSizeTotal(&comp->types, &fn->type->sig) / sizeof(Slot);
            genReturn(&comp->gen, paramSlots);
        }
    }

    identFree(&comp->idents, comp->blocks.item[comp->blocks.top].block);

    blocksLeave(&comp->blocks);
    lexEat(&comp->lex, TOK_RBRACE);
}


// prototype = .
void parsePrototype(Compiler *comp, Ident *fn)
{
    fn->prototypeOffset = fn->offset;
    genNop(&comp->gen);
}



