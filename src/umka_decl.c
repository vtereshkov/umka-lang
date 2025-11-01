#define __USE_MINGW_ANSI_STDIO 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "umka_expr.h"
#include "umka_stmt.h"
#include "umka_decl.h"


static int parseModule(Umka *umka);


// exportMark = ["*"].
static bool parseExportMark(Umka *umka)
{
    if (umka->lex.tok.kind == TOK_MUL)
    {
        lexNextForcedSemicolon(&umka->lex);
        return true;
    }
    return false;
}


// identList = ident exportMark {"," ident exportMark}.
static void parseIdentList(Umka *umka, IdentName *names, bool *exported, int capacity, int *num)
{
    *num = 0;
    while (1)
    {
        lexCheck(&umka->lex, TOK_IDENT);

        if (*num >= capacity)
            umka->error.handler(umka->error.context, "Too many identifiers");
        strcpy(names[*num], umka->lex.tok.name);

        lexNext(&umka->lex);
        exported[*num] = parseExportMark(umka);
        (*num)++;

        if (umka->lex.tok.kind != TOK_COMMA)
            break;
        lexNext(&umka->lex);
    }
}


// typedIdentList = identList ":" [".."] type.
static void parseTypedIdentList(Umka *umka, IdentName *names, bool *exported, int capacity, int *num, const Type **type, bool allowVariadicParamList)
{
    parseIdentList(umka, names, exported, capacity, num);
    lexEat(&umka->lex, TOK_COLON);

    if (allowVariadicParamList && umka->lex.tok.kind == TOK_ELLIPSIS)
    {
        if (*num != 1)
            umka->error.handler(umka->error.context, "Only one variadic parameter list is allowed");

        lexNext(&umka->lex);
        const Type *itemType = parseType(umka, NULL);
        if (itemType->kind == TYPE_VOID)
            umka->error.handler(umka->error.context, "Variadic parameters cannot be void");

        Type *variadicListType = typeAdd(&umka->types, &umka->blocks, TYPE_DYNARRAY);
        variadicListType->base = itemType;
        variadicListType->isVariadicParamList = true;
        *type = variadicListType;
    }
    else
        *type = parseType(umka, NULL);
}


// rcvSignature = "(" ident ":" type ")".
static void parseRcvSignature(Umka *umka, Signature *sig)
{
    lexEat(&umka->lex, TOK_LPAR);
    lexEat(&umka->lex, TOK_IDENT);

    IdentName rcvName;
    strcpy(rcvName, umka->lex.tok.name);

    lexEat(&umka->lex, TOK_COLON);
    const Type *rcvType = parseType(umka, NULL);

    if (rcvType->kind != TYPE_PTR || !rcvType->base->typeIdent)
        umka->error.handler(umka->error.context, "Receiver should be a pointer to a defined type");

     if (rcvType->base->typeIdent->module != umka->blocks.module)
        umka->error.handler(umka->error.context, "Receiver base type cannot be defined in another module");

    if (rcvType->base->kind == TYPE_PTR || rcvType->base->kind == TYPE_INTERFACE)
    	umka->error.handler(umka->error.context, "Receiver base type cannot be a pointer or an interface");

    sig->isMethod = true;
    typeAddParam(&umka->types, sig, rcvType, rcvName, (Const){0});

    lexEat(&umka->lex, TOK_RPAR);
}


// signature = "(" [typedIdentList ["=" expr] {"," typedIdentList ["=" expr]}] ")" [":" (type | "(" type {"," type} ")")].
static void parseSignature(Umka *umka, Signature *sig)
{
    // Dummy hidden parameter that allows any function to be converted to a closure
    if (!sig->isMethod)
        typeAddParam(&umka->types, sig, umka->anyType, "#upvalues", (Const){0});

    // Formal parameter list
    lexEat(&umka->lex, TOK_LPAR);
    int numDefaultParams = 0;

    if (umka->lex.tok.kind == TOK_IDENT)
    {
        bool variadicParamListFound = false;
        while (1)
        {
            if (variadicParamListFound)
                umka->error.handler(umka->error.context, "Variadic parameter list should be the last parameter");

            IdentName paramNames[MAX_PARAMS];
            bool paramExported[MAX_PARAMS];
            const Type *paramType = NULL;
            int numParams = 0;
            parseTypedIdentList(umka, paramNames, paramExported, MAX_PARAMS, &numParams, &paramType, true);

            variadicParamListFound = paramType->isVariadicParamList;

            // ["=" expr]
            Const defaultConstant = {0};
            if (umka->lex.tok.kind == TOK_EQ)
            {
                if (numParams != 1)
                    umka->error.handler(umka->error.context, "Parameter list cannot have common default value");

                if (paramType->isVariadicParamList)
                    umka->error.handler(umka->error.context, "Variadic parameter list cannot have default value");

                if (!typeComparable(paramType) && !typeEquivalent(paramType, umka->anyType))
                    umka->error.handler(umka->error.context, "Parameter must be of comparable or 'any' type to have default value");
                
                lexNext(&umka->lex);

                const Type *defaultType = paramType;
                parseExpr(umka, &defaultType, &defaultConstant);
                doAssertImplicitTypeConv(umka, paramType, &defaultType, &defaultConstant);

                numDefaultParams++;
            }
            else
            {
                if (numDefaultParams != 0)
                    umka->error.handler(umka->error.context, "Parameters with default values should be the last ones");
            }

            for (int i = 0; i < numParams; i++)
            {
                if (paramExported[i])
                    umka->error.handler(umka->error.context, "Parameter %s cannot be exported", paramNames[i]);

                typeAddParam(&umka->types, sig, paramType, paramNames[i], defaultConstant);
            }

            if (umka->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&umka->lex);
        }
    }
    lexEat(&umka->lex, TOK_RPAR);
    sig->numDefaultParams = numDefaultParams;

    // Result type
    if (umka->lex.tok.kind == TOK_COLON)
    {
        lexNext(&umka->lex);
        if (umka->lex.tok.kind == TOK_LPAR)
        {
            // Result type list (syntactic sugar - actually a structure type)
            Type *listType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
            listType->isExprList = true;

            lexNext(&umka->lex);

            while (1)
            {
                const Type *fieldType = parseType(umka, NULL);
                typeAddField(&umka->types, listType, fieldType, NULL);

                if (umka->lex.tok.kind != TOK_COMMA)
                    break;
                lexNext(&umka->lex);
            }
            lexEat(&umka->lex, TOK_RPAR);

            if (listType->numItems == 1)
                sig->resultType = listType->field[0]->type;
            else
                sig->resultType = listType;
        }
        else
            // Single result type
            sig->resultType = parseType(umka, NULL);
    }
    else
        sig->resultType = umka->voidType;

    // Structured result parameter
    if (typeStructured(sig->resultType))
        typeAddParam(&umka->types, sig, typeAddPtrTo(&umka->types, &umka->blocks, sig->resultType), "#result", (Const){0});
}


static const Type *parseTypeOrForwardType(Umka *umka)
{
    const Type *type = NULL;

    // Forward declaration?
    bool forward = false;
    if (umka->types.forwardTypesEnabled && umka->lex.tok.kind == TOK_IDENT)
    {
        const Ident *ident = NULL;

        Lexer lookaheadLex = umka->lex;
        lexNext(&lookaheadLex);
        if (lookaheadLex.tok.kind == TOK_COLONCOLON)
            ident = identFindModule(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, umka->lex.tok.name, true);
        else
            ident = identFind(&umka->idents, &umka->modules, &umka->blocks, umka->blocks.module, umka->lex.tok.name, NULL, true);

        if (!ident)
        {
            Type *forwardType = typeAdd(&umka->types, &umka->blocks, TYPE_FORWARD);
            forwardType->typeIdent = identAddType(&umka->idents, &umka->modules, &umka->blocks, umka->lex.tok.name, forwardType, false);
            identSetUsed(forwardType->typeIdent);

            lexNext(&umka->lex);

            type = forwardType;
            forward = true;
        }
    }

    // Conventional declaration
    if (!forward)
        type = parseType(umka, NULL);

    return type;
}


// ptrType = ["weak"] "^" type.
static const Type *parsePtrType(Umka *umka)
{
    bool weak = false;
    if (umka->lex.tok.kind == TOK_WEAK)
    {
        weak = true;
        lexNext(&umka->lex);
    }

    lexEat(&umka->lex, TOK_CARET);

    const Type *baseType = parseTypeOrForwardType(umka);

    if (weak)
        return typeAddWeakPtrTo(&umka->types, &umka->blocks, baseType);
    else
        return typeAddPtrTo(&umka->types, &umka->blocks, baseType);
}


// arrayType = "[" expr "]" type.
// dynArrayType = "[" "]" type.
static const Type *parseArrayType(Umka *umka)
{
    lexEat(&umka->lex, TOK_LBRACKET);

    TypeKind typeKind;
    Const len;

    if (umka->lex.tok.kind == TOK_RBRACKET)
    {
        // Dynamic array
        typeKind = TYPE_DYNARRAY;
        len.intVal = 0;
    }
    else
    {
        // Conventional array
        typeKind = TYPE_ARRAY;
        const Type *indexType = NULL;
        parseExpr(umka, &indexType, &len);
        typeAssertCompatible(&umka->types, umka->intType, indexType);
        if (len.intVal < 0 || len.intVal > INT_MAX)
            umka->error.handler(umka->error.context, "Illegal array length");
    }

    lexEat(&umka->lex, TOK_RBRACKET);

    const Type *baseType = (typeKind == TYPE_DYNARRAY) ? parseTypeOrForwardType(umka) : parseType(umka, NULL);
    if (baseType->kind == TYPE_VOID)
        umka->error.handler(umka->error.context, "Array items cannot be void");

    if (len.intVal > 0 && typeSize(&umka->types, baseType) > INT_MAX / len.intVal)
        umka->error.handler(umka->error.context, "Array is too large");

    Type *type = typeAdd(&umka->types, &umka->blocks, typeKind);
    type->base = baseType;
    typeResizeArray(type, len.intVal);
    return type;
}


// strType = "str".
static const Type *parseStrType(Umka *umka)
{
    lexEat(&umka->lex, TOK_STR);
    return umka->strType;
}


// enumItem = ident ["=" expr].
static void parseEnumItem(Umka *umka, Type *type, Const *constant)
{
    lexCheck(&umka->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, umka->lex.tok.name);

    lexNext(&umka->lex);

    if (umka->lex.tok.kind != TOK_EQ)
        constant->intVal++;
    else
    {
        lexEat(&umka->lex, TOK_EQ);
        const Type *rightType = NULL;
        parseExpr(umka, &rightType, constant);
        typeAssertCompatible(&umka->types, umka->intType, rightType);
    }

    if (typeOverflow(type->kind, *constant))
        umka->error.handler(umka->error.context, "Overflow of %s", typeKindSpelling(type->kind));

    typeAddEnumConst(&umka->types, type, name, *constant);
}


// enumType = "enum" ["(" type ")"] "{" {enumItem ";"} "}"
static const Type *parseEnumType(Umka *umka)
{
    lexEat(&umka->lex, TOK_ENUM);

    const Type *baseType = umka->intType;
    if (umka->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&umka->lex);
        baseType = parseType(umka, NULL);
        typeAssertCompatible(&umka->types, umka->intType, baseType);
        lexEat(&umka->lex, TOK_RPAR);
    }

    Type *type = typeAdd(&umka->types, &umka->blocks, baseType->kind);
    type->isEnum = true;

    Const constant = {.intVal = -1};

    lexEat(&umka->lex, TOK_LBRACE);
    while (umka->lex.tok.kind == TOK_IDENT)
    {
        parseEnumItem(umka, type, &constant);
        lexEat(&umka->lex, TOK_SEMICOLON);
    }
    lexEat(&umka->lex, TOK_RBRACE);

    const Const zero = {.intVal = 0};
    if (!typeFindEnumConstByVal(type, zero))
        typeAddEnumConst(&umka->types, type, "zero", zero);

    return type;
}


// mapType = "map" "[" type "]" type.
static const Type *parseMapType(Umka *umka)
{
    lexEat(&umka->lex, TOK_MAP);
    lexEat(&umka->lex, TOK_LBRACKET);

    Type *type = typeAdd(&umka->types, &umka->blocks, TYPE_MAP);

    const Type *keyType = parseType(umka, NULL);
    if (!typeValidOperator(keyType, TOK_EQEQ))
        umka->error.handler(umka->error.context, "Map key type is not comparable");

    const Type *ptrKeyType = typeAddPtrTo(&umka->types, &umka->blocks, keyType);

    lexEat(&umka->lex, TOK_RBRACKET);

    const Type *itemType = parseTypeOrForwardType(umka);
    if (itemType->kind == TYPE_VOID)
        umka->error.handler(umka->error.context, "Map items cannot be void");

    const Type *ptrItemType = typeAddPtrTo(&umka->types, &umka->blocks, itemType);

    // The map base type is the Umka equivalent of MapNode
    Type *nodeType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
    const Type *ptrNodeType = typeAddPtrTo(&umka->types, &umka->blocks, nodeType);

    typeAddField(&umka->types, nodeType, umka->intType, "#len");
    typeAddField(&umka->types, nodeType, umka->intType, "#priority");
    typeAddField(&umka->types, nodeType, ptrKeyType,    "#key");
    typeAddField(&umka->types, nodeType, ptrItemType,   "#data");
    typeAddField(&umka->types, nodeType, ptrNodeType,   "#left");
    typeAddField(&umka->types, nodeType, ptrNodeType,   "#right");

    type->base = nodeType;
    return type;
}


// structType = "struct" "{" {typedIdentList ";"} "}"
static const Type *parseStructType(Umka *umka)
{
    lexEat(&umka->lex, TOK_STRUCT);
    lexEat(&umka->lex, TOK_LBRACE);

    Type *type = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);

    while (umka->lex.tok.kind == TOK_IDENT)
    {
        IdentName fieldNames[MAX_IDENTS_IN_LIST];
        bool fieldExported[MAX_IDENTS_IN_LIST];
        const Type *fieldType = NULL;
        int numFields = 0;
        parseTypedIdentList(umka, fieldNames, fieldExported, MAX_IDENTS_IN_LIST, &numFields, &fieldType, false);

        for (int i = 0; i < numFields; i++)
        {
            typeAddField(&umka->types, type, fieldType, fieldNames[i]);
            if (fieldExported[i])
                umka->error.handler(umka->error.context, "Field %s cannot be exported", fieldNames[i]);
        }

        lexEat(&umka->lex, TOK_SEMICOLON);
    }
    lexEat(&umka->lex, TOK_RBRACE);
    return type;
}


// interfaceType = "interface" "{" {(ident signature | qualIdent) ";"} "}"
static const Type *parseInterfaceType(Umka *umka)
{
    lexEat(&umka->lex, TOK_INTERFACE);
    lexEat(&umka->lex, TOK_LBRACE);

    Type *type = typeAdd(&umka->types, &umka->blocks, TYPE_INTERFACE);

    // The interface type is the Umka equivalent of Interface + methods
    typeAddField(&umka->types, type, umka->ptrVoidType, "#self");
    typeAddField(&umka->types, type, umka->ptrVoidType, "#selftype");

    // Method names and signatures, or embedded interfaces
    while (umka->lex.tok.kind == TOK_IDENT)
    {
        Lexer lookaheadLex = umka->lex;
        lexNext(&lookaheadLex);

        if (lookaheadLex.tok.kind == TOK_LPAR)
        {
            // Method name and signature
            IdentName methodName;
            strcpy(methodName, umka->lex.tok.name);
            lexNext(&umka->lex);

            Type *methodType = typeAdd(&umka->types, &umka->blocks, TYPE_FN);
            methodType->sig.isMethod = true;

            typeAddParam(&umka->types, &methodType->sig, umka->ptrVoidType, "#self", (Const){0});
            parseSignature(umka, &methodType->sig);

            const Field *method = typeAddField(&umka->types, type, methodType, methodName);
            methodType->sig.offsetFromSelf = method->offset;
        }
        else
        {
            // Embedded interface
            const Type *embeddedType = parseType(umka, NULL);

            if (embeddedType->kind != TYPE_INTERFACE)
                umka->error.handler(umka->error.context, "Interface type expected");

            for (int i = 2; i < embeddedType->numItems; i++)    // Skip #self and #selftype in embedded interface
            {
                Type *methodType = typeAdd(&umka->types, &umka->blocks, TYPE_FN);
                typeDeepCopy(&umka->storage, methodType, embeddedType->field[i]->type);

                const Field *method = typeAddField(&umka->types, type, methodType, embeddedType->field[i]->name);
                methodType->sig.isMethod = true;
                methodType->sig.offsetFromSelf = method->offset;
            }
        }

        lexEat(&umka->lex, TOK_SEMICOLON);
    }
    lexEat(&umka->lex, TOK_RBRACE);
    return type;
}


// closureType = "fn" signature.
static const Type *parseClosureType(Umka *umka)
{
    lexEat(&umka->lex, TOK_FN);

    Type *type = typeAdd(&umka->types, &umka->blocks, TYPE_CLOSURE);

    // Function field
    Type *fnType = typeAdd(&umka->types, &umka->blocks, TYPE_FN);
    parseSignature(umka, &fnType->sig);
    typeAddField(&umka->types, type, fnType, "#fn");

    // Upvalues field
    typeAddField(&umka->types, type, umka->anyType, "#upvalues");

    return type;
}


// type = qualIdent | ptrType | arrayType | dynArrayType | strType | enumType | mapType | structType | interfaceType | closureType.
const Type *parseType(Umka *umka, const Ident *ident)
{
    if (ident)
    {
        if (ident->kind != IDENT_TYPE)
            umka->error.handler(umka->error.context, "Type expected");
        lexNext(&umka->lex);
        return ident->type;
    }

    switch (umka->lex.tok.kind)
    {
        case TOK_IDENT:     return parseType(umka, parseQualIdent(umka));
        case TOK_CARET:
        case TOK_WEAK:      return parsePtrType(umka);
        case TOK_LBRACKET:  return parseArrayType(umka);
        case TOK_STR:       return parseStrType(umka);
        case TOK_ENUM:      return parseEnumType(umka);
        case TOK_MAP:       return parseMapType(umka);
        case TOK_STRUCT:    return parseStructType(umka);
        case TOK_INTERFACE: return parseInterfaceType(umka);
        case TOK_FN:        return parseClosureType(umka);

        default:            umka->error.handler(umka->error.context, "Type expected"); return NULL;
    }
}


// typeDeclItem = ident exportMark "=" type.
static void parseTypeDeclItem(Umka *umka)
{
    lexCheck(&umka->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, umka->lex.tok.name);

    lexNext(&umka->lex);
    bool exported = parseExportMark(umka);

    lexEat(&umka->lex, TOK_EQ);

    const Type *type = parseType(umka, NULL);
    Type *newType = typeAdd(&umka->types, &umka->blocks, type->kind);
    typeDeepCopy(&umka->storage, newType, type);
    newType->typeIdent = identAddType(&umka->idents, &umka->modules, &umka->blocks, name, newType, exported);
}


// typeDecl = "type" (typeDeclItem | "(" {typeDeclItem ";"} ")").
static void parseTypeDecl(Umka *umka)
{
    lexEat(&umka->lex, TOK_TYPE);

    typeEnableForward(&umka->types, true);

    if (umka->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&umka->lex);
        while (umka->lex.tok.kind == TOK_IDENT)
        {
            parseTypeDeclItem(umka);
            lexEat(&umka->lex, TOK_SEMICOLON);
        }
        lexEat(&umka->lex, TOK_RPAR);
    }
    else
        parseTypeDeclItem(umka);

    typeEnableForward(&umka->types, false);
}


// constDeclItem = ident exportMark ["=" expr].
static void parseConstDeclItem(Umka *umka, const Type **type, Const *constant)
{
    lexCheck(&umka->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, umka->lex.tok.name);

    lexNext(&umka->lex);
    bool exported = parseExportMark(umka);

    if (*type && typeInteger(*type) && umka->lex.tok.kind != TOK_EQ)
    {
        constant->intVal++;

        if (typeOverflow((*type)->kind, *constant))
            umka->error.handler(umka->error.context, "Overflow of %s", typeKindSpelling((*type)->kind));
    }
    else
    {
        lexEat(&umka->lex, TOK_EQ);
        *type = NULL;
        parseExpr(umka, type, constant);

        if (!typeOrdinal(*type) && !typeReal(*type) && (*type)->kind != TYPE_STR && (*type)->kind != TYPE_CLOSURE)
            umka->error.handler(umka->error.context, "Constant must be ordinal, or real, or string, or closure");
    }

    identAddConst(&umka->idents, &umka->modules, &umka->blocks, name, *type, exported, *constant);
}


// constDecl = "const" (constDeclItem | "(" {constDeclItem ";"} ")").
static void parseConstDecl(Umka *umka)
{
    lexEat(&umka->lex, TOK_CONST);

    const Type *type = NULL;
    Const constant;

    if (umka->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&umka->lex);
        while (umka->lex.tok.kind == TOK_IDENT)
        {
            parseConstDeclItem(umka, &type, &constant);
            lexEat(&umka->lex, TOK_SEMICOLON);
        }
        lexEat(&umka->lex, TOK_RPAR);
    }
    else
        parseConstDeclItem(umka, &type, &constant);
}


// varDeclItem = typedIdentList "=" exprList.
static void parseVarDeclItem(Umka *umka)
{
    IdentName varNames[MAX_IDENTS_IN_LIST];
    bool varExported[MAX_IDENTS_IN_LIST];
    int numVars = 0;
    const Type *varType = NULL;
    parseTypedIdentList(umka, varNames, varExported, MAX_IDENTS_IN_LIST, &numVars, &varType, false);

    Ident *var[MAX_IDENTS_IN_LIST];
    for (int i = 0; i < numVars; i++)
    {
        var[i] = identAllocVar(&umka->idents, &umka->types, &umka->modules, &umka->blocks, varNames[i], varType, varExported[i]);
        doZeroVar(umka, var[i]);
    }

    // Initializer
    if (umka->lex.tok.kind == TOK_EQ)
    {
        const Type *designatorType = NULL;
        if (typeStructured(var[0]->type))
            designatorType = var[0]->type;
        else
            designatorType = typeAddPtrTo(&umka->types, &umka->blocks, var[0]->type);

        if (numVars != 1)
        {
            // Designator list (types formally encoded as structure field types - not a real structure)
            Type *designatorListType = typeAdd(&umka->types, &umka->blocks, TYPE_STRUCT);
            designatorListType->isExprList = true;

            for (int i = 0; i < numVars; i++)
                typeAddField(&umka->types, designatorListType, designatorType, NULL);

            designatorType = designatorListType;
        }

        Const varPtrConstList[MAX_IDENTS_IN_LIST] = {0};

        for (int i = 0; i < numVars; i++)
        {
            if (umka->blocks.top == 0)          // Globals are initialized with constant expressions
                varPtrConstList[i].ptrVal = var[i]->ptr;
            else                                // Locals are assigned to
                doPushVarPtr(umka, var[i]);
        }

        lexNext(&umka->lex);
        parseAssignmentStmt(umka, designatorType, (umka->blocks.top == 0) ? varPtrConstList : NULL);
    }
}


// fullVarDecl = "var" (varDeclItem | "(" {varDeclItem ";"} ")").
static void parseFullVarDecl(Umka *umka)
{
    lexEat(&umka->lex, TOK_VAR);

    if (umka->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&umka->lex);
        while (umka->lex.tok.kind == TOK_IDENT)
        {
            parseVarDeclItem(umka);
            lexEat(&umka->lex, TOK_SEMICOLON);
        }
        lexEat(&umka->lex, TOK_RPAR);
    }
    else
        parseVarDeclItem(umka);
}


// shortVarDecl = declAssignmentStmt.
void parseShortVarDecl(Umka *umka)
{
    IdentName varNames[MAX_IDENTS_IN_LIST];
    bool varExported[MAX_IDENTS_IN_LIST];
    int numVars = 0;
    parseIdentList(umka, varNames, varExported, MAX_IDENTS_IN_LIST, &numVars);

    lexEat(&umka->lex, TOK_COLONEQ);
    parseDeclAssignmentStmt(umka, varNames, varExported, numVars, umka->blocks.top == 0);
}


// fnDecl = "fn" [rcvSignature] ident exportMark signature [block].
static void parseFnDecl(Umka *umka)
{
    if (umka->blocks.top != 0)
        umka->error.handler(umka->error.context, "Nested functions should be declared as variables");

    lexEat(&umka->lex, TOK_FN);
    Type *fnType = typeAdd(&umka->types, &umka->blocks, TYPE_FN);

    if (umka->lex.tok.kind == TOK_LPAR)
        parseRcvSignature(umka, &fnType->sig);

    lexCheck(&umka->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, umka->lex.tok.name);

    // Check for method/field name collision
    if (fnType->sig.isMethod)
    {
        const Type *rcvBaseType = fnType->sig.param[0]->type->base;

        if (rcvBaseType->kind == TYPE_STRUCT && typeFindField(rcvBaseType, name, NULL))
            umka->error.handler(umka->error.context, "Structure already has field %s", name);
    }

    lexNext(&umka->lex);
    bool exported = parseExportMark(umka);

    parseSignature(umka, &fnType->sig);

    Const constant = {.intVal = umka->gen.ip};
    Ident *fn = identAddConst(&umka->idents, &umka->modules, &umka->blocks, name, fnType, exported, constant);

    if (umka->lex.tok.kind == TOK_LBRACE)
        parseFnBlock(umka, fn, NULL);
    else
        parseFnPrototype(umka, fn);
}


// decl = typeDecl | constDecl | varDecl | fnDecl.
void parseDecl(Umka *umka)
{
    switch (umka->lex.tok.kind)
    {
        case TOK_TYPE:   parseTypeDecl(umka);       break;
        case TOK_CONST:  parseConstDecl(umka);      break;
        case TOK_VAR:    parseFullVarDecl(umka);    break;
        case TOK_IDENT:  parseShortVarDecl(umka);   break;
        case TOK_FN:     parseFnDecl(umka);         break;

        case TOK_EOF:    if (umka->blocks.top == 0)
                             break;

        default: umka->error.handler(umka->error.context, "Declaration expected but %s found", lexSpelling(umka->lex.tok.kind)); break;
    }
}


// decls = decl {";" decl}.
static void parseDecls(Umka *umka)
{
    while (1)
    {
        parseDecl(umka);
        if (umka->lex.tok.kind == TOK_EOF)
            break;
        lexEat(&umka->lex, TOK_SEMICOLON);
    }
}


// importItem = [ident "="] stringLiteral.
static void parseImportItem(Umka *umka)
{
    char *alias = NULL;
    if (umka->lex.tok.kind == TOK_IDENT)
    {
        alias = storageAdd(&umka->storage, DEFAULT_STR_LEN + 1);
        strcpy(alias, umka->lex.tok.name);
        lexNext(&umka->lex);
        lexEat(&umka->lex, TOK_EQ);
    }

    lexCheck(&umka->lex, TOK_STRLITERAL);

    char path[DEFAULT_STR_LEN + 1] = "";

    // Module source strings, if any, have precedence over files
    const char *sourceString = NULL;
    bool sourceTrusted = false;

    if (moduleRegularizePath(&umka->modules, umka->lex.tok.strVal, umka->modules.curFolder, path, DEFAULT_STR_LEN + 1))
    {
        const ModuleSource *sourceDesc = moduleFindSource(&umka->modules, path);
        if (sourceDesc)
        {
            sourceString = sourceDesc->source;
            sourceTrusted = sourceDesc->trusted;
        }
    }

    if (!sourceString)
        moduleAssertRegularizePath(&umka->modules, umka->lex.tok.strVal, umka->modules.module[umka->blocks.module]->folder, path, DEFAULT_STR_LEN + 1);

    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(&umka->modules, path, folder, name, DEFAULT_STR_LEN + 1);

    if (!alias)
    {
        alias = storageAdd(&umka->storage, DEFAULT_STR_LEN + 1);
        strcpy(alias, name);
    }

    if (moduleFindImported(&umka->modules, &umka->blocks, alias) >= 0)
        umka->modules.error->handler(umka->modules.error->context, "Duplicate imported module %s", alias);

    int importedModule = moduleFind(&umka->modules, path);
    if (importedModule < 0)
    {
        // Save context
        int currentModule       = umka->blocks.module;
        DebugInfo currentDebug  = umka->debug;
        Lexer currentLex        = umka->lex;
        lexInit(&umka->lex, &umka->storage, &umka->debug, path, sourceString, sourceTrusted, &umka->error);

        lexNext(&umka->lex);
        importedModule = parseModule(umka);

        // Restore context
        lexFree(&umka->lex);
        umka->lex               = currentLex;
        umka->debug             = currentDebug;
        umka->blocks.module     = currentModule;
    }

    // Imported module is registered but its body has not been compiled yet - this is only possible if it's imported in a cycle
    if (!umka->modules.module[importedModule]->isCompiled)
        umka->modules.error->handler(umka->modules.error->context, "Cyclic import of module %s", alias);

    // Module is imported iff it has an import alias (which may coincide with the module name if not specified explicitly)
    char **importAlias = &umka->modules.module[umka->blocks.module]->importAlias[importedModule];
    if (*importAlias)
         umka->modules.error->handler(umka->modules.error->context, "Duplicate imported module %s", path);
    *importAlias = alias;

    identAddModule(&umka->idents, &umka->modules, &umka->blocks, alias, umka->voidType, importedModule);

    lexNext(&umka->lex);
}


// import = "import" (importItem | "(" {importItem ";"} ")").
static void parseImport(Umka *umka)
{
    lexEat(&umka->lex, TOK_IMPORT);

    if (umka->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&umka->lex);
        while (umka->lex.tok.kind == TOK_STRLITERAL || umka->lex.tok.kind == TOK_IDENT)
        {
            parseImportItem(umka);
            lexEat(&umka->lex, TOK_SEMICOLON);
        }
        lexEat(&umka->lex, TOK_RPAR);
    }
    else
        parseImportItem(umka);
}


// module = [import ";"] decls.
static int parseModule(Umka *umka)
{
    umka->blocks.module = moduleAdd(&umka->modules, umka->lex.fileName);

    if (umka->lex.tok.kind == TOK_IMPORT)
    {
        parseImport(umka);
        lexEat(&umka->lex, TOK_SEMICOLON);
    }
    parseDecls(umka);
    doResolveExtern(umka);

    umka->modules.module[umka->blocks.module]->isCompiled = true;
    return umka->blocks.module;
}


// program = module.
void parseProgram(Umka *umka)
{
    // Entry point stub
    genNop(&umka->gen);

    lexNext(&umka->lex);
    int mainModule = parseModule(umka);

    // Entry point
    genEntryPoint(&umka->gen, 0);

    const Ident *mainIdent = identFind(&umka->idents, &umka->modules, &umka->blocks, mainModule, "main", NULL, false);
    if (mainIdent)
    {
        if (!identIsMain(mainIdent))
            umka->error.handler(umka->error.context, "Identifier main must be fn main()");

        genPushZero(&umka->gen, sizeof(Interface) / sizeof(Slot));  // Dummy upvalue
        genCall(&umka->gen, mainIdent->offset);
    }

    doGarbageCollection(umka);
    genHalt(&umka->gen);
}
