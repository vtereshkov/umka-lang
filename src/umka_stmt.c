#include <string.h>

#include "umka_stmt.h"
#include "umka_expr.h"
#include "umka_decl.h"


static void parseStmtList(Compiler *comp);
static void parseBlock(Compiler *comp);


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


// assignmentStmt = designator "=" expr.
void parseAssignmentStmt(Compiler *comp, Type *type, void *initializedVarPtr)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error("Left side cannot be assigned to");
        type = type->base;
    }

    Type *rightType;
    Const rightConstantBuf, *rightConstant = NULL;

    if (initializedVarPtr)
        rightConstant = &rightConstantBuf;

    parseExpr(comp, &rightType, rightConstant);

    doImplicitTypeConv(comp, type, &rightType, rightConstant, false);
    typeAssertCompatible(&comp->types, type, rightType, false);

    if (initializedVarPtr)                          // Initialize global variable
        constAssign(&comp->consts, initializedVarPtr, rightConstant, type->kind, typeSize(&comp->types, type));
    else                                            // Assign to variable
        genChangeRefCntAssign(&comp->gen, type);
}


// shortAssignmentStmt = designator ("+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "~=") expr.
static void parseShortAssignmentStmt(Compiler *comp, Type *type, TokenKind op)
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

    // All temporary reals are 64-bit
    if (type->kind == TYPE_REAL32)
        type = comp->realType;

    Type *rightType;
    parseExpr(comp, &rightType, NULL);

    doApplyOperator(comp, &type, &rightType, NULL, NULL, lexShortAssignment(op), true, false);
    genChangeRefCntAssign(&comp->gen, type);
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


// incDecStmt = designator ("++" | "--").
static void parseIncDecStmt(Compiler *comp, Type *type, TokenKind op)
{
    if (!typeStructured(type))
    {
        if (type->kind != TYPE_PTR || type->base->kind == TYPE_VOID)
            comp->error("Left side cannot be assigned to");
        type = type->base;
    }

    typeAssertCompatible(&comp->types, comp->intType, type, false);
    genUnary(&comp->gen, op, type->kind);
    lexNext(&comp->lex);
}


// simpleStmt     = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
// callStmt       = designator.
static void parseSimpleStmt(Compiler *comp)
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
                genPop(&comp->gen);  // Manually remove result
        }
    }
}


// ifStmt = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
static void parseIfStmt(Compiler *comp)
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

    // Additional scope embracing shortVarDecl
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
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
}


// forStmt = "for" [shortVarDecl ";"] expr [";" simpleStmt] block.
static void parseForStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_FOR);

    // Additional scope embracing shortVarDecl
    blocksEnter(&comp->blocks, NULL);

    // 'break'/'continue' prologs
    Gotos breaks, *outerBreaks = comp->gen.breaks;
    comp->gen.breaks = &breaks;
    genGotosProlog(&comp->gen, comp->gen.breaks, blocksCurrent(&comp->blocks));

    Gotos continues, *outerContinues = comp->gen.continues;
    comp->gen.continues = &continues;
    genGotosProlog(&comp->gen, comp->gen.continues, blocksCurrent(&comp->blocks));

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
    typeAssertCompatible(&comp->types, comp->boolType, type, false);

    genForCondEpilog(&comp->gen);

    // [";" simpleStmt]
    if (comp->lex.tok.kind == TOK_SEMICOLON)
    {
        lexNext(&comp->lex);
        parseSimpleStmt(comp);
    }

    genForPostStmtEpilog(&comp->gen);

    // block
    parseBlock(comp);

    // 'continue' epilog
    genGotosEpilog(&comp->gen, comp->gen.continues);
    comp->gen.continues = outerContinues;

    genForEpilog(&comp->gen);

    // 'break' epilog
    genGotosEpilog(&comp->gen, comp->gen.breaks);
    comp->gen.breaks = outerBreaks;

    // Additional scope embracing shortVarDecl
    doGarbageCollection(comp, blocksCurrent(&comp->blocks));
    blocksLeave(&comp->blocks);
}


// breakStmt = "break".
static void parseBreakStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_BREAK);

    if (!comp->gen.breaks)
        comp->error("No loop to break");

    doGarbageCollectionDownToBlock(comp, comp->gen.breaks->block);
    genGotosAddStub(&comp->gen, comp->gen.breaks);
}


// continueStmt = "continue".
static void parseContinueStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_CONTINUE);

    if (!comp->gen.continues)
        comp->error("No loop to continue");

    doGarbageCollectionDownToBlock(comp, comp->gen.continues->block);
    genGotosAddStub(&comp->gen, comp->gen.continues);
}


// returnStmt = "return" [expr].
static void parseReturnStmt(Compiler *comp)
{
    lexEat(&comp->lex, TOK_RETURN);
    Type *type;

    if (comp->lex.tok.kind != TOK_SEMICOLON && comp->lex.tok.kind != TOK_RBRACE)
        parseExpr(comp, &type, NULL);
    else
        type = comp->voidType;

    // Get function signature
    Signature *sig = NULL;
    for (int i = comp->blocks.top; i >= 1; i--)
        if (comp->blocks.item[i].fn)
        {
            sig = &comp->blocks.item[i].fn->type->sig;
            break;
        }

    doImplicitTypeConv(comp, sig->resultType[0], &type, NULL, false);
    typeAssertCompatible(&comp->types, sig->resultType[0], type, false);

    // Copy structure to __result
    if (typeStructured(sig->resultType[0]))
    {
        Ident *__result = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, "__result", NULL);

        doPushVarPtr(comp, __result);
        genDeref(&comp->gen, TYPE_PTR);

        // Assignment to an anonymous stack area (pointed to by __result) does not require updating reference counts
        genSwapAssign(&comp->gen, sig->resultType[0]->kind, typeSize(&comp->types, sig->resultType[0]));

        doPushVarPtr(comp, __result);
        genDeref(&comp->gen, TYPE_PTR);
    }

    if (sig->resultType[0]->kind != TYPE_VOID)
    {
        // Increase result reference count
        genChangeRefCnt(&comp->gen, TOK_PLUSPLUS, sig->resultType[0]);
        genPopReg(&comp->gen, VM_RESULT_REG_0);
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
        case TOK_VAR:
        case TOK_FN:        parseDecl(comp);            break;
        case TOK_LBRACE:    parseBlock(comp);           break;
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
        if (fn->type->sig.method || fn->type->sig.numParams != 0 || fn->type->sig.resultType[0]->kind != TYPE_VOID)
            comp->error("Illegal main() signature");

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

    Gotos returns;
    comp->gen.returns = &returns;
    genGotosProlog(&comp->gen, comp->gen.returns, blocksCurrent(&comp->blocks));

    // StmtList
    parseStmtList(comp);

    genGotosEpilog(&comp->gen, comp->gen.returns);

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

