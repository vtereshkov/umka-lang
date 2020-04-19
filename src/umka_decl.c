#include <string.h>

#include "umka_expr.h"
#include "umka_stmt.h"
#include "umka_decl.h"


// identList = ident {"," ident}.
void parseIdentList(Compiler *comp, IdentName *names, int capacity, int *num)
{
    *num = 0;
    while (1)
    {
        lexEat(&comp->lex, TOK_IDENT);

        if (*num >= capacity)
            comp->error("Too many identifiers");
        strcpy(names[(*num)++], comp->lex.tok.name);

        if (comp->lex.tok.kind != TOK_COMMA)
            break;
        lexNext(&comp->lex);
    }
}


// typedIdentList = identList ":" type.
void parseTypedIdentList(Compiler *comp, IdentName *names, int capacity, int *num, Type **type)
{
    parseIdentList(comp, names, capacity, num);
    lexEat(&comp->lex, TOK_COLON);
    *type = parseType(comp);
}


// signature = "(" [typedIdentList {"," typedIdentList}] ")" [":" type].
static void parseSignature(Compiler *comp, Signature *sig)
{
    // Formal parameter list
    sig->numParams = 0;
    lexEat(&comp->lex, TOK_LPAR);

    if (comp->lex.tok.kind == TOK_IDENT)
    {
        while (1)
        {
            IdentName paramNames[MAX_PARAMS];
            Type *paramType;
            int numParams = 0;
            parseTypedIdentList(comp, paramNames, MAX_PARAMS, &numParams, &paramType);

            for (int i = 0; i < numParams; i++)
                typeAddParam(&comp->types, sig, paramType, paramNames[i]);

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }
    lexEat(&comp->lex, TOK_RPAR);

    // Result type
    sig->numResults = 0;
    if (comp->lex.tok.kind == TOK_COLON)
    {
        lexNext(&comp->lex);
        sig->resultType[sig->numResults++] = parseType(comp);
    }
    else
        sig->resultType[sig->numResults++] = comp->voidType;
}


// ptrType = "^" type.
static Type *parsePtrType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_CARET);
    Type *type = parseType(comp);
    return typeAddPtrTo(&comp->types, &comp->blocks, type);
}


// arrayType = "[" expr "]" type.
static Type *parseArrayType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_LBRACKET);

    Const len;
    Type *indexType;
    parseExpr(comp, &indexType, &len);
    typeAssertCompatible(&comp->types, indexType, comp->intType);

    lexEat(&comp->lex, TOK_RBRACKET);

    Type *baseType = parseType(comp);

    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_ARRAY);
    type->base = baseType;
    type->numItems = len.intVal;
    return type;
}


// structType = "struct" "{" {typedIdentList ";"} "}"
static Type *parseStructType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_STRUCT);
    lexEat(&comp->lex, TOK_LBRACE);

    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
    type->numItems = 0;

    while (comp->lex.tok.kind == TOK_IDENT)
    {
        IdentName fieldNames[MAX_FIELDS];
        Type *fieldType;
        int numFields = 0;
        parseTypedIdentList(comp, fieldNames, MAX_FIELDS, &numFields, &fieldType);

        for (int i = 0; i < numFields; i++)
            typeAddField(&comp->types, type, fieldType, fieldNames[i]);

        lexEat(&comp->lex, TOK_SEMICOLON);
    }
    lexEat(&comp->lex, TOK_RBRACE);
    return type;
}


// fnType = "fn" signature.
static Type *parseFnType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_FN);
    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_FN);
    parseSignature(comp, &(type->sig));
    return type;
}


// type = ident | ptrType | arrayType | structType | fnType.
Type *parseType(Compiler *comp)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_IDENT:
        {
            Ident *ident = identAssertFind(&comp->idents, &comp->blocks, comp->lex.tok.name);
            if (ident->kind != IDENT_TYPE)
                comp->error("Type expected");
            lexNext(&comp->lex);
            return ident->type;
        }
        case TOK_CARET:     return parsePtrType(comp);
        case TOK_LBRACKET:  return parseArrayType(comp);
        case TOK_STRUCT:    return parseStructType(comp);
        case TOK_FN:        return parseFnType(comp);

        default:            comp->error("Type expected"); return NULL;
    }
}


// typeDeclItem = ident "=" type.
static void parseTypeDeclItem(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);
    lexNext(&comp->lex);

    lexEat(&comp->lex, TOK_EQ);
    Type *type = parseType(comp);

    identAddType(&comp->idents, &comp->blocks, name, type);
}


// typeDecl = "type" (typeDeclItem | "(" {typeDeclItem ";"} ")").
void parseTypeDecl(Compiler *comp)
{
    lexEat(&comp->lex, TOK_TYPE);

    if (comp->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&comp->lex);
        while (comp->lex.tok.kind == TOK_IDENT)
        {
            parseTypeDeclItem(comp);
            lexEat(&comp->lex, TOK_SEMICOLON);
        }
        lexEat(&comp->lex, TOK_RPAR);
    }
    else
        parseTypeDeclItem(comp);
}


// constDeclItem = ident "=" expr.
static void parseConstDeclItem(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);
    lexNext(&comp->lex);

    lexEat(&comp->lex, TOK_EQ);
    Type *type;
    Const constant;
    parseExpr(comp, &type, &constant);

    identAddConst(&comp->idents, &comp->blocks, name, type, constant);
}


// constDecl = "const" (constDeclItem | "(" {constDeclItem ";"} ")").
void parseConstDecl(Compiler *comp)
{
    lexEat(&comp->lex, TOK_CONST);

    if (comp->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&comp->lex);
        while (comp->lex.tok.kind == TOK_IDENT)
        {
            parseConstDeclItem(comp);
            lexEat(&comp->lex, TOK_SEMICOLON);
        }
        lexEat(&comp->lex, TOK_RPAR);
    }
    else
        parseConstDeclItem(comp);
}


// varDeclItem = typedIdentList | ident ":" type "=" expr.
static void parseVarDeclItem(Compiler *comp)
{
    IdentName varNames[MAX_FIELDS];
    int numVars = 0;
    Type *varType;
    parseTypedIdentList(comp, varNames, MAX_FIELDS, &numVars, &varType);

    for (int i = 0; i < numVars; i++)
        identAllocVar(&comp->idents, &comp->types, &comp->blocks, varNames[i], varType);

    // Initializer
    if (comp->lex.tok.kind == TOK_EQ)
    {
        if (numVars != 1)
            comp->error("Unable to initialize multiple variables");

        Ident *ident = comp->idents.last;
        doPushVarPtr(comp, ident);

        lexNext(&comp->lex);

        Type *designatorType = typeAddPtrTo(&comp->types, &comp->blocks, ident->type);
        parseAssignmentStmt(comp, designatorType, comp->blocks.top == 0);
    }
}


// varDecl = "var" (varDeclItem | "(" {varDeclItem ";"} ")").
void parseVarDecl(Compiler *comp)
{
    lexEat(&comp->lex, TOK_VAR);

    if (comp->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&comp->lex);
        while (comp->lex.tok.kind == TOK_IDENT)
        {
            parseVarDeclItem(comp);
            lexEat(&comp->lex, TOK_SEMICOLON);
        }
        lexEat(&comp->lex, TOK_RPAR);
    }
    else
        parseVarDeclItem(comp);
}


// shortVarDecl = ident ":=" expr.
void parseShortVarDecl(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);
    lexNext(&comp->lex);
    lexEat(&comp->lex, TOK_COLONEQ);

    parseDeclAssignment(comp, name, comp->blocks.top == 0);
}


// fnDecl = "fn" ident signature [block].
void parseFnDecl(Compiler *comp)
{
    if (comp->blocks.top != 0)
        comp->error("Nested functions are not allowed");

    lexEat(&comp->lex, TOK_FN);

    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);
    lexNext(&comp->lex);

    typeAdd(&comp->types, &comp->blocks, TYPE_FN);
    Type *fnType = comp->types.last;

    parseSignature(comp, &fnType->sig);

    Const constant = {.intVal = comp->gen.ip};
    identAddConst(&comp->idents, &comp->blocks, name, fnType, constant);
    Ident *fn = comp->idents.last;

    if (comp->lex.tok.kind == TOK_LBRACE)
        parseBlock(comp, fn);
    else
        fn->forward = true;
}


// decl = typeDecl | constDecl | varDecl | fnDecl.
void parseDecl(Compiler *comp)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_TYPE:   parseTypeDecl(comp); break;
        case TOK_CONST:  parseConstDecl(comp); break;
        case TOK_VAR:    parseVarDecl(comp); break;
        case TOK_FN:     parseFnDecl(comp); break;

        case TOK_EOF:    if (comp->blocks.top == 0)
                             break;

        default: comp->error("Declaration expected but %s found", lexSpelling(comp->lex.tok.kind)); break;
    }
}


// decls = {decl ";"}.
void parseDecls(Compiler *comp)
{
    while (1)
    {
        parseDecl(comp);
        if (comp->lex.tok.kind != TOK_SEMICOLON)
            break;
        lexNext(&comp->lex);
    }
}


// module = decls.
void parseModule(Compiler *comp)
{
    genNop(&comp->gen);     // Entry point stub
    parseDecls(comp);
}
