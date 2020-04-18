#include <string.h>

#include "umka_stmt.h"
#include "umka_expr.h"
#include "umka_decl.h"


// assignmentStmt = designator "=" expr.
void parseAssignmentStmt(Compiler *comp, Type *type, bool constExpr)
{
    if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
        comp->error("Left side cannot be assigned to");

    Type *rightType;
    Const rightConstantBuf, *rightConstant = NULL;

    if (constExpr)
        rightConstant = &rightConstantBuf;

    parseExpr(comp, &rightType, rightConstant);

    if (constExpr)
        doPushConst(comp, rightType, rightConstant);

    doImplicitTypeConv(comp, type->base, &rightType, rightConstant, false);
    typeAssertCompatible(&comp->types, type->base, rightType);

    genAssign(&comp->gen, type->base->kind);
}


// shortAssignmentStmt = designator ("+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "~=") expr.
void parseShortAssignmentStmt(Compiler *comp, Type *type, TokenKind op)
{
    if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
        comp->error("Left side cannot be assigned to");

    // Duplicate designator and treat it as an expression
    Type *leftType = type->base;
    genDup(&comp->gen);
    genDeref(&comp->gen, leftType->kind);

    // No direct support for 32-bit reals
    if (leftType->kind == TYPE_REAL32)
        leftType = comp->realType;

    Type *rightType;
    parseExpr(comp, &rightType, NULL);

    doImplicitTypeConv(comp, leftType, &rightType, NULL, false);
    typeAssertCompatible(&comp->types, leftType, rightType);

    genAssign(&comp->gen, leftType->kind);
}


void parseDeclAssignment(Compiler *comp, IdentName name, bool constExpr)
{
    Type *rightType;
    Const rightConstantBuf, *rightConstant = NULL;

    if (constExpr)
        rightConstant = &rightConstantBuf;

    parseExpr(comp, &rightType, rightConstant);

    if (constExpr)
        doPushConst(comp, rightType, rightConstant);

    identAllocVar(&comp->idents, &comp->types, &comp->blocks, name, rightType);
    Ident *ident = comp->idents.last;

    doPushVarPtr(comp, ident);
    genSwap(&comp->gen);                        // Assignment requires that the left-hand side comes first
    genAssign(&comp->gen, rightType->kind);
}


// incDecStmt = designator ("++" | "--").
void parseIncDecStmt(Compiler *comp, Type *type, TokenKind op)
{
    if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
        comp->error("Left side cannot be assigned to");

    typeAssertCompatible(&comp->types, type->base, comp->intType);
    genUnary(&comp->gen, op, type->kind);
    lexNext(&comp->lex);
}


// simpleStmt     = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
// callStmt       = designator.
void parseSimpleStmt(Compiler *comp)
{
    Ident *ident = identFind(&comp->idents, &comp->blocks, comp->lex.tok.name);
    if (ident)
    {
        Type *type;
        bool isVar, isCall;
        parseDesignator(comp, &type, NULL, &isVar, &isCall);

        TokenKind op = comp->lex.tok.kind;
        if (op == TOK_EQ || lexShortAssignment(op) != TOK_NONE)
        {
            // Assignment
            if (!isVar)
                comp->error("Left side cannot be assigned to");
            lexNext(&comp->lex);

            if (op == TOK_EQ)
                parseAssignmentStmt(comp, type, false);
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
    else
        parseShortVarDecl(comp);
}


// ifStmt = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
void parseIfStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_IF);

    if (comp->lex.tok.kind == TOK_IDENT)
    {
        Ident *ident = identFind(&comp->idents, &comp->blocks, comp->lex.tok.name);
        if (!ident)
        {
            parseShortVarDecl(comp);
            lexEat(&comp->lex, TOK_SEMICOLON);
        }
    }

    Type *type;
    parseExpr(comp, &type, NULL);
    typeAssertCompatible(&comp->types, type, comp->boolType);

    genIfCondEpilog(&comp->gen);

    parseBlock(comp, NULL);

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
}


// forStmt = "for" [shortVarDecl ";"] expr [";" simpleStmt] block.
void parseForStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_FOR);

    if (comp->lex.tok.kind == TOK_IDENT)
    {
        Ident *ident = identFind(&comp->idents, &comp->blocks, comp->lex.tok.name);
        if (!ident)
        {
            parseShortVarDecl(comp);
            lexEat(&comp->lex, TOK_SEMICOLON);
        }
    }

    genForCondProlog(&comp->gen);

    Type *type;
    parseExpr(comp, &type, NULL);
    typeAssertCompatible(&comp->types, type, comp->boolType);

    genForCondEpilog(&comp->gen);

    if (comp->lex.tok.kind == TOK_SEMICOLON)
    {
        lexNext(&comp->lex);
        parseSimpleStmt(comp);
    }

    genForPostStmtEpilog(&comp->gen);

    parseBlock(comp, NULL);

    genForEpilog(&comp->gen);
}


// returnStmt = "return" [expr].
void parseReturnStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_RETURN);
}


// stmt = decl | assignmentStmt | callStmt | ifStmt | forStmt | returnStmt.
void parseStmt(Compiler *comp)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_TYPE:
        case TOK_CONST:
        case TOK_VAR:
        case TOK_FN:        parseDecl(comp);        break;
        case TOK_IDENT:     parseSimpleStmt(comp);  break;
        case TOK_IF:        parseIfStmt(comp);      break;
        case TOK_FOR:       parseForStmt(comp);     break;
        case TOK_RETURN:    parseReturnStmt(comp);  break;

        default: break;
    }
}


// stmtList = {Stmt ";"}.
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
    blocksEnter(&comp->blocks, fn != NULL);

    bool mainFn = false;
    if (fn)
    {
        if (strcmp(fn->name, "main") == 0)
        {
            genEntryPoint(&comp->gen);
            mainFn = true;
        }

        genEnterFrameStub(&comp->gen);
        for (int i = 0; i < fn->type->sig.numParams; i++)
            identAllocParam(&comp->idents, &comp->blocks, &fn->type->sig, i);
    }

    parseStmtList(comp);

    if (fn)
    {
        genLeaveFrameFixup(&comp->gen, comp->blocks.item[comp->blocks.top].localVarSize);
        if (mainFn)
            genHalt(&comp->gen);
        else
            genReturn(&comp->gen, fn->type->sig.numParams);
    }

    identFree(&comp->idents, comp->blocks.item[comp->blocks.top].block);

    blocksLeave(&comp->blocks);
    lexEat(&comp->lex, TOK_RBRACE);
}






