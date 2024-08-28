#define __USE_MINGW_ANSI_STDIO 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "umka_expr.h"
#include "umka_stmt.h"
#include "umka_decl.h"


static int parseModule(Compiler *comp);


// exportMark = ["*"].
static bool parseExportMark(Compiler *comp)
{
    if (comp->lex.tok.kind == TOK_MUL)
    {
        lexNextForcedSemicolon(&comp->lex);
        return true;
    }
    return false;
}


// identList = ident exportMark {"," ident exportMark}.
static void parseIdentList(Compiler *comp, IdentName *names, bool *exported, int capacity, int *num)
{
    *num = 0;
    while (1)
    {
        lexCheck(&comp->lex, TOK_IDENT);

        if (*num >= capacity)
            comp->error.handler(comp->error.context, "Too many identifiers");
        strcpy(names[*num], comp->lex.tok.name);

        lexNext(&comp->lex);
        exported[*num] = parseExportMark(comp);
        (*num)++;

        if (comp->lex.tok.kind != TOK_COMMA)
            break;
        lexNext(&comp->lex);
    }
}


// typedIdentList = identList ":" [".."] type.
static void parseTypedIdentList(Compiler *comp, IdentName *names, bool *exported, int capacity, int *num, Type **type, bool allowVariadicParamList)
{
    parseIdentList(comp, names, exported, capacity, num);
    lexEat(&comp->lex, TOK_COLON);

    if (allowVariadicParamList && comp->lex.tok.kind == TOK_ELLIPSIS)
    {
        if (*num != 1)
            comp->error.handler(comp->error.context, "Only one variadic parameter list is allowed");

        lexNext(&comp->lex);
        Type *itemType = parseType(comp, NULL);

        *type = typeAdd(&comp->types, &comp->blocks, TYPE_DYNARRAY);
        (*type)->base = itemType;
        (*type)->isVariadicParamList = true;
    }
    else
        *type = parseType(comp, NULL);
}


// rcvSignature = "(" ident ":" type ")".
static void parseRcvSignature(Compiler *comp, Signature *sig)
{
    lexEat(&comp->lex, TOK_LPAR);
    lexEat(&comp->lex, TOK_IDENT);

    IdentName rcvName;
    strcpy(rcvName, comp->lex.tok.name);

    lexEat(&comp->lex, TOK_COLON);
    Type *rcvType = parseType(comp, NULL);

    if (rcvType->kind != TYPE_PTR || !rcvType->base->typeIdent)
        comp->error.handler(comp->error.context, "Receiver should be a pointer to a defined type");

     if (rcvType->base->typeIdent->module != comp->blocks.module)
        comp->error.handler(comp->error.context, "Receiver base type cannot be defined in another module");

    if (rcvType->base->kind == TYPE_PTR || rcvType->base->kind == TYPE_INTERFACE)
    	comp->error.handler(comp->error.context, "Receiver base type cannot be a pointer or an interface");

    sig->isMethod = true;
    typeAddParam(&comp->types, sig, rcvType, rcvName);

    lexEat(&comp->lex, TOK_RPAR);
}


// signature = "(" [typedIdentList ["=" expr] {"," typedIdentList ["=" expr]}] ")" [":" (type | "(" type {"," type} ")")].
static void parseSignature(Compiler *comp, Signature *sig)
{
    // Dummy hidden parameter that allows any function to be converted to a closure
    if (!sig->isMethod)
        typeAddParam(&comp->types, sig, comp->anyType, "__upvalues");

    // Formal parameter list
    lexEat(&comp->lex, TOK_LPAR);
    int numDefaultParams = 0;
    bool variadicParamListFound = false;

    if (comp->lex.tok.kind == TOK_IDENT)
    {
        while (1)
        {
            if (variadicParamListFound)
                comp->error.handler(comp->error.context, "Variadic parameter list should be the last parameter");

            IdentName paramNames[MAX_PARAMS];
            bool paramExported[MAX_PARAMS];
            Type *paramType = NULL;
            int numParams = 0;
            parseTypedIdentList(comp, paramNames, paramExported, MAX_PARAMS, &numParams, &paramType, true);

            variadicParamListFound = paramType->isVariadicParamList;

            // ["=" expr]
            Const defaultConstant;
            if (comp->lex.tok.kind == TOK_EQ)
            {
                if (numParams != 1)
                    comp->error.handler(comp->error.context, "Parameter list cannot have common default value");

                if (paramType->isVariadicParamList)
                    comp->error.handler(comp->error.context, "Variadic parameter list cannot have default value");

                lexNext(&comp->lex);

                Type *defaultType = paramType;
                parseExpr(comp, &defaultType, &defaultConstant);
                doAssertImplicitTypeConv(comp, paramType, &defaultType, &defaultConstant);

                numDefaultParams++;
            }
            else
            {
                if (numDefaultParams != 0)
                    comp->error.handler(comp->error.context, "Parameters with default values should be the last ones");
            }

            for (int i = 0; i < numParams; i++)
            {
                if (paramExported[i])
                    comp->error.handler(comp->error.context, "Parameter %s cannot be exported", paramNames[i]);

                Param *param = typeAddParam(&comp->types, sig, paramType, paramNames[i]);
                if (numDefaultParams > 0)
                    param->defaultVal = defaultConstant;
            }

            if (comp->lex.tok.kind != TOK_COMMA)
                break;
            lexNext(&comp->lex);
        }
    }
    lexEat(&comp->lex, TOK_RPAR);
    sig->numDefaultParams = numDefaultParams;

    // Result type
    if (comp->lex.tok.kind == TOK_COLON)
    {
        lexNext(&comp->lex);
        if (comp->lex.tok.kind == TOK_LPAR)
        {
            // Result type list (syntactic sugar - actually a structure type)
            Type *listType = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
            listType->isExprList = true;

            lexNext(&comp->lex);

            while (1)
            {
                Type *fieldType = parseType(comp, NULL);
                typeAddField(&comp->types, listType, fieldType, NULL);

                if (comp->lex.tok.kind != TOK_COMMA)
                    break;
                lexNext(&comp->lex);
            }
            lexEat(&comp->lex, TOK_RPAR);

            if (listType->numItems == 1)
                sig->resultType = listType->field[0]->type;
            else
                sig->resultType = listType;
        }
        else
            // Single result type
            sig->resultType = parseType(comp, NULL);
    }
    else
        sig->resultType = comp->voidType;

    // Structured result parameter
    if (typeStructured(sig->resultType))
        typeAddParam(&comp->types, sig, typeAddPtrTo(&comp->types, &comp->blocks, sig->resultType), "__result");
}


static Type *parseTypeOrForwardType(Compiler *comp)
{
    Type *type = NULL;

    // Forward declaration?
    bool forward = false;
    if (comp->types.forwardTypesEnabled && comp->lex.tok.kind == TOK_IDENT)
    {
        Ident *ident = NULL;

        Lexer lookaheadLex = comp->lex;
        lexNext(&lookaheadLex);
        if (lookaheadLex.tok.kind == TOK_COLONCOLON)
            ident = identFindModule(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, comp->lex.tok.name, true);
        else
            ident = identFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, comp->lex.tok.name, NULL, true);

        if (!ident)
        {
            type = typeAdd(&comp->types, &comp->blocks, TYPE_FORWARD);
            type->typeIdent = identAddType(&comp->idents, &comp->modules, &comp->blocks, comp->lex.tok.name, type, false);
            type->typeIdent->used = true;

            lexNext(&comp->lex);

            forward = true;
        }
    }

    // Conventional declaration
    if (!forward)
        type = parseType(comp, NULL);

    return type;
}


// ptrType = ["weak"] "^" type.
static Type *parsePtrType(Compiler *comp)
{
    bool weak = false;
    if (comp->lex.tok.kind == TOK_WEAK)
    {
        weak = true;
        lexNext(&comp->lex);
    }

    lexEat(&comp->lex, TOK_CARET);

    Type *type = parseTypeOrForwardType(comp);
    type = typeAddPtrTo(&comp->types, &comp->blocks, type);
    if (weak)
        type->kind = TYPE_WEAKPTR;

    return type;
}


// arrayType = "[" expr "]" type.
// dynArrayType = "[" "]" type.
static Type *parseArrayType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_LBRACKET);

    TypeKind typeKind;
    Const len;

    if (comp->lex.tok.kind == TOK_RBRACKET)
    {
        // Dynamic array
        typeKind = TYPE_DYNARRAY;
        len.intVal = 0;
    }
    else
    {
        // Conventional array
        typeKind = TYPE_ARRAY;
        Type *indexType = NULL;
        parseExpr(comp, &indexType, &len);
        typeAssertCompatible(&comp->types, comp->intType, indexType);
        if (len.intVal < 0 || len.intVal > INT_MAX)
            comp->error.handler(comp->error.context, "Illegal array length");
    }

    lexEat(&comp->lex, TOK_RBRACKET);

    Type *baseType = (typeKind == TYPE_DYNARRAY) ? parseTypeOrForwardType(comp) : parseType(comp, NULL);
    if (baseType->kind == TYPE_VOID)
        comp->error.handler(comp->error.context, "Array items cannot be void");

    if (len.intVal > 0 && typeSize(&comp->types, baseType) > INT_MAX / len.intVal)
        comp->error.handler(comp->error.context, "Array is too large");

    Type *type = typeAdd(&comp->types, &comp->blocks, typeKind);
    type->base = baseType;
    type->numItems = len.intVal;
    return type;
}


// strType = "str".
static Type *parseStrType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_STR);
    return comp->strType;
}


// enumItem = ident ["=" expr].
static void parseEnumItem(Compiler *comp, Type *type, Const *constant)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);

    lexNext(&comp->lex);

    if (comp->lex.tok.kind != TOK_EQ)
        constant->intVal++;
    else
    {
        lexEat(&comp->lex, TOK_EQ);
        Type *rightType = NULL;
        parseExpr(comp, &rightType, constant);
        typeAssertCompatible(&comp->types, comp->intType, rightType);
    }

    if (typeOverflow(type->kind, *constant))
        comp->error.handler(comp->error.context, "Overflow of %s", typeKindSpelling(type->kind));

    typeAddEnumConst(&comp->types, type, name, *constant);
}


// enumType = "enum" ["(" type ")"] "{" {enumItem ";"} "}"
static Type *parseEnumType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_ENUM);

    Type *baseType = comp->intType;
    if (comp->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&comp->lex);
        baseType = parseType(comp, NULL);
        typeAssertCompatible(&comp->types, comp->intType, baseType);
        lexEat(&comp->lex, TOK_RPAR);
    }

    Type *type = typeAdd(&comp->types, &comp->blocks, baseType->kind);
    type->isEnum = true;

    Const constant = {.intVal = -1};

    lexEat(&comp->lex, TOK_LBRACE);
    while (comp->lex.tok.kind == TOK_IDENT)
    {
        parseEnumItem(comp, type, &constant);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }
    lexEat(&comp->lex, TOK_RBRACE);
    return type;
}


// mapType = "map" "[" type "]" type.
static Type *parseMapType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_MAP);
    lexEat(&comp->lex, TOK_LBRACKET);

    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_MAP);

    Type *keyType = parseType(comp, NULL);
    if (!typeValidOperator(keyType, TOK_EQEQ))
        comp->error.handler(comp->error.context, "Map key type is not comparable");

    Type *ptrKeyType = typeAddPtrTo(&comp->types, &comp->blocks, keyType);

    lexEat(&comp->lex, TOK_RBRACKET);

    Type *itemType = parseTypeOrForwardType(comp);
    if (itemType->kind == TYPE_VOID)
        comp->error.handler(comp->error.context, "Map items cannot be void");

    Type *ptrItemType = typeAddPtrTo(&comp->types, &comp->blocks, itemType);

    // The map base type is the Umka equivalent of MapNode
    Type *nodeType = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
    Type *ptrNodeType = typeAddPtrTo(&comp->types, &comp->blocks, nodeType);

    typeAddField(&comp->types, nodeType, comp->intType, "__len");
    typeAddField(&comp->types, nodeType, ptrKeyType,    "__key");
    typeAddField(&comp->types, nodeType, ptrItemType,   "__data");
    typeAddField(&comp->types, nodeType, ptrNodeType,   "__left");
    typeAddField(&comp->types, nodeType, ptrNodeType,   "__right");

    type->base = nodeType;
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
        IdentName fieldNames[MAX_IDENTS_IN_LIST];
        bool fieldExported[MAX_IDENTS_IN_LIST];
        Type *fieldType = NULL;
        int numFields = 0;
        parseTypedIdentList(comp, fieldNames, fieldExported, MAX_IDENTS_IN_LIST, &numFields, &fieldType, false);

        for (int i = 0; i < numFields; i++)
        {
            typeAddField(&comp->types, type, fieldType, fieldNames[i]);
            if (fieldExported[i])
                comp->error.handler(comp->error.context, "Field %s cannot be exported", fieldNames[i]);
        }

        lexEat(&comp->lex, TOK_SEMICOLON);
    }
    lexEat(&comp->lex, TOK_RBRACE);
    return type;
}


// interfaceType = "interface" "{" {(ident signature | qualIdent) ";"} "}"
static Type *parseInterfaceType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_INTERFACE);
    lexEat(&comp->lex, TOK_LBRACE);

    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_INTERFACE);
    type->numItems = 0;

    // The interface type is the Umka equivalent of Interface + methods
    typeAddField(&comp->types, type, comp->ptrVoidType, "__self");
    typeAddField(&comp->types, type, comp->ptrVoidType, "__selftype");

    // Method names and signatures, or embedded interfaces
    while (comp->lex.tok.kind == TOK_IDENT)
    {
        Lexer lookaheadLex = comp->lex;
        lexNext(&lookaheadLex);

        if (lookaheadLex.tok.kind == TOK_LPAR)
        {
            // Method name and signature
            IdentName methodName;
            strcpy(methodName, comp->lex.tok.name);
            lexNext(&comp->lex);

            Type *methodType = typeAdd(&comp->types, &comp->blocks, TYPE_FN);
            methodType->sig.isMethod = true;

            typeAddParam(&comp->types, &methodType->sig, comp->ptrVoidType, "__self");
            parseSignature(comp, &methodType->sig);

            Field *method = typeAddField(&comp->types, type, methodType, methodName);
            methodType->sig.offsetFromSelf = method->offset;
        }
        else
        {
            // Embedded interface
            Type *embeddedType = parseType(comp, NULL);

            if (embeddedType->kind != TYPE_INTERFACE)
                comp->error.handler(comp->error.context, "Interface type expected");

            for (int i = 2; i < embeddedType->numItems; i++)    // Skip __self and __selftype in embedded interface
            {
                Type *methodType = typeAdd(&comp->types, &comp->blocks, TYPE_FN);
                typeDeepCopy(methodType, embeddedType->field[i]->type);

                Field *method = typeAddField(&comp->types, type, methodType, embeddedType->field[i]->name);
                methodType->sig.isMethod = true;
                methodType->sig.offsetFromSelf = method->offset;
            }
        }

        lexEat(&comp->lex, TOK_SEMICOLON);
    }
    lexEat(&comp->lex, TOK_RBRACE);
    return type;
}


// closureType = "fn" signature.
static Type *parseClosureType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_FN);

    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_CLOSURE);

    // Function field
    Type *fnType = typeAdd(&comp->types, &comp->blocks, TYPE_FN);
    parseSignature(comp, &fnType->sig);
    typeAddField(&comp->types, type, fnType, "__fn");

    // Upvalues field
    typeAddField(&comp->types, type, comp->anyType, "__upvalues");

    return type;
}


// type = qualIdent | ptrType | arrayType | dynArrayType | strType | enumType | mapType | structType | interfaceType | closureType.
Type *parseType(Compiler *comp, Ident *ident)
{
    if (ident)
    {
        if (ident->kind != IDENT_TYPE)
            comp->error.handler(comp->error.context, "Type expected");
        lexNext(&comp->lex);
        return ident->type;
    }

    switch (comp->lex.tok.kind)
    {
        case TOK_IDENT:     return parseType(comp, parseQualIdent(comp));
        case TOK_CARET:
        case TOK_WEAK:      return parsePtrType(comp);
        case TOK_LBRACKET:  return parseArrayType(comp);
        case TOK_STR:       return parseStrType(comp);
        case TOK_ENUM:      return parseEnumType(comp);
        case TOK_MAP:       return parseMapType(comp);
        case TOK_STRUCT:    return parseStructType(comp);
        case TOK_INTERFACE: return parseInterfaceType(comp);
        case TOK_FN:        return parseClosureType(comp);

        default:            comp->error.handler(comp->error.context, "Type expected"); return NULL;
    }
}


// typeDeclItem = ident exportMark "=" type.
static void parseTypeDeclItem(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);

    lexNext(&comp->lex);
    bool exported = parseExportMark(comp);

    lexEat(&comp->lex, TOK_EQ);

    Type *type = parseType(comp, NULL);
    Type *newType = typeAdd(&comp->types, &comp->blocks, type->kind);
    typeDeepCopy(newType, type);
    newType->typeIdent = identAddType(&comp->idents, &comp->modules, &comp->blocks, name, newType, exported);
}


// typeDecl = "type" (typeDeclItem | "(" {typeDeclItem ";"} ")").
static void parseTypeDecl(Compiler *comp)
{
    lexEat(&comp->lex, TOK_TYPE);

    typeEnableForward(&comp->types, true);

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

    typeEnableForward(&comp->types, false);
}


// constDeclItem = ident exportMark ["=" expr].
static void parseConstDeclItem(Compiler *comp, Type **type, Const *constant)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);

    lexNext(&comp->lex);
    bool exported = parseExportMark(comp);

    if (*type && typeInteger(*type) && comp->lex.tok.kind != TOK_EQ)
    {
        constant->intVal++;

        if (typeOverflow((*type)->kind, *constant))
            comp->error.handler(comp->error.context, "Overflow of %s", typeKindSpelling((*type)->kind));
    }
    else
    {
        lexEat(&comp->lex, TOK_EQ);
        *type = NULL;
        parseExpr(comp, type, constant);

        if (!typeOrdinal(*type) && !typeReal(*type) && (*type)->kind != TYPE_STR && (*type)->kind != TYPE_CLOSURE)
            comp->error.handler(comp->error.context, "Constant must be ordinal, or real, or string, or closure");
    }

    identAddConst(&comp->idents, &comp->modules, &comp->blocks, name, *type, exported, *constant);
}


// constDecl = "const" (constDeclItem | "(" {constDeclItem ";"} ")").
static void parseConstDecl(Compiler *comp)
{
    lexEat(&comp->lex, TOK_CONST);

    Type *type = NULL;
    Const constant;

    if (comp->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&comp->lex);
        while (comp->lex.tok.kind == TOK_IDENT)
        {
            parseConstDeclItem(comp, &type, &constant);
            lexEat(&comp->lex, TOK_SEMICOLON);
        }
        lexEat(&comp->lex, TOK_RPAR);
    }
    else
        parseConstDeclItem(comp, &type, &constant);
}


// varDeclItem = typedIdentList "=" exprList.
static void parseVarDeclItem(Compiler *comp)
{
    IdentName varNames[MAX_IDENTS_IN_LIST];
    bool varExported[MAX_IDENTS_IN_LIST];
    int numVars = 0;
    Type *varType = NULL;
    parseTypedIdentList(comp, varNames, varExported, MAX_IDENTS_IN_LIST, &numVars, &varType, false);

    Ident *var[MAX_IDENTS_IN_LIST];
    for (int i = 0; i < numVars; i++)
    {
        var[i] = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, varNames[i], varType, varExported[i]);
        doZeroVar(comp, var[i]);
    }

    // Initializer
    if (comp->lex.tok.kind == TOK_EQ)
    {
        Type *designatorType = NULL;
        if (typeStructured(var[0]->type))
            designatorType = var[0]->type;
        else
            designatorType = typeAddPtrTo(&comp->types, &comp->blocks, var[0]->type);

        Type *designatorListType = NULL;
        if (numVars == 1)
            designatorListType = designatorType;
        else
        {
            // Designator list (types formally encoded as structure field types - not a real structure)
            designatorListType = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
            designatorListType->isExprList = true;

            for (int i = 0; i < numVars; i++)
                typeAddField(&comp->types, designatorListType, designatorType, NULL);
        }

        Const varPtrConstList[MAX_IDENTS_IN_LIST] = {0};

        for (int i = 0; i < numVars; i++)
        {
            if (comp->blocks.top == 0)          // Globals are initialized with constant expressions
                varPtrConstList[i].ptrVal = var[i]->ptr;
            else                                // Locals are assigned to
                doPushVarPtr(comp, var[i]);
        }

        lexNext(&comp->lex);
        parseAssignmentStmt(comp, designatorListType, (comp->blocks.top == 0) ? varPtrConstList : NULL);
    }
}


// fullVarDecl = "var" (varDeclItem | "(" {varDeclItem ";"} ")").
static void parseFullVarDecl(Compiler *comp)
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


// shortVarDecl = declAssignmentStmt.
void parseShortVarDecl(Compiler *comp)
{
    IdentName varNames[MAX_IDENTS_IN_LIST];
    bool varExported[MAX_IDENTS_IN_LIST];
    int numVars = 0;
    parseIdentList(comp, varNames, varExported, MAX_IDENTS_IN_LIST, &numVars);

    lexEat(&comp->lex, TOK_COLONEQ);
    parseDeclAssignmentStmt(comp, varNames, varExported, numVars, comp->blocks.top == 0);
}


// fnDecl = "fn" [rcvSignature] ident exportMark signature [block].
static void parseFnDecl(Compiler *comp)
{
    if (comp->blocks.top != 0)
        comp->error.handler(comp->error.context, "Nested functions should be declared as variables");

    lexEat(&comp->lex, TOK_FN);
    Type *fnType = typeAdd(&comp->types, &comp->blocks, TYPE_FN);

    if (comp->lex.tok.kind == TOK_LPAR)
        parseRcvSignature(comp, &fnType->sig);

    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);

    // Check for method/field name collision
    if (fnType->sig.isMethod)
    {
        Type *rcvBaseType = fnType->sig.param[0]->type->base;

        if (rcvBaseType->kind == TYPE_STRUCT && typeFindField(rcvBaseType, name, NULL))
            comp->error.handler(comp->error.context, "Structure already has field %s", name);
    }

    lexNext(&comp->lex);
    bool exported = parseExportMark(comp);

    parseSignature(comp, &fnType->sig);

    Const constant = {.intVal = comp->gen.ip};
    Ident *fn = identAddConst(&comp->idents, &comp->modules, &comp->blocks, name, fnType, exported, constant);

    if (comp->lex.tok.kind == TOK_LBRACE)
        parseFnBlock(comp, fn, NULL);
    else
        parseFnPrototype(comp, fn);
}


// decl = typeDecl | constDecl | varDecl | fnDecl.
void parseDecl(Compiler *comp)
{
    switch (comp->lex.tok.kind)
    {
        case TOK_TYPE:   parseTypeDecl(comp);       break;
        case TOK_CONST:  parseConstDecl(comp);      break;
        case TOK_VAR:    parseFullVarDecl(comp);    break;
        case TOK_IDENT:  parseShortVarDecl(comp);   break;
        case TOK_FN:     parseFnDecl(comp);         break;

        case TOK_EOF:    if (comp->blocks.top == 0)
                             break;

        default: comp->error.handler(comp->error.context, "Declaration expected but %s found", lexSpelling(comp->lex.tok.kind)); break;
    }
}


// decls = decl {";" decl}.
static void parseDecls(Compiler *comp)
{
    while (1)
    {
        parseDecl(comp);
        if (comp->lex.tok.kind == TOK_EOF)
            break;
        lexEat(&comp->lex, TOK_SEMICOLON);
    }
}


// importItem = [ident "="] stringLiteral.
static void parseImportItem(Compiler *comp)
{
    char *alias = NULL;
    if (comp->lex.tok.kind == TOK_IDENT)
    {
        alias = (char *)malloc(DEFAULT_STR_LEN + 1);
        strcpy(alias, comp->lex.tok.name);
        lexNext(&comp->lex);
        lexEat(&comp->lex, TOK_EQ);
    }

    lexCheck(&comp->lex, TOK_STRLITERAL);

    char path[DEFAULT_STR_LEN + 1] = "";

    // Module source strings, if any, have precedence over files
    char *sourceString = NULL;
    if (moduleRegularizePath(comp->lex.tok.strVal, comp->modules.curFolder, path, DEFAULT_STR_LEN + 1))
        sourceString = moduleFindSource(&comp->modules, path);

    if (!sourceString)
        moduleAssertRegularizePath(&comp->modules, comp->lex.tok.strVal, comp->modules.module[comp->blocks.module]->folder, path, DEFAULT_STR_LEN + 1);

    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(&comp->modules, path, folder, name, DEFAULT_STR_LEN + 1);

    if (!alias)
    {
        alias = (char *)malloc(DEFAULT_STR_LEN + 1);
        strcpy(alias, name);
    }

    if (moduleFindImported(&comp->modules, &comp->blocks, alias) >= 0)
        comp->modules.error->handler(comp->modules.error->context, "Duplicate imported module %s", alias);

    int importedModule = moduleFind(&comp->modules, path);
    if (importedModule < 0)
    {
        // Save context
        int currentModule       = comp->blocks.module;
        DebugInfo currentDebug  = comp->debug;
        Lexer currentLex        = comp->lex;
        lexInit(&comp->lex, &comp->storage, &comp->debug, path, sourceString, &comp->error);

        lexNext(&comp->lex);
        importedModule = parseModule(comp);

        // Restore context
        lexFree(&comp->lex);
        comp->lex               = currentLex;
        comp->debug             = currentDebug;
        comp->blocks.module     = currentModule;
    }

    // Imported module is registered but its body has not been compiled yet - this is only possible if it's imported in a cycle
    if (!comp->modules.module[importedModule]->isCompiled)
        comp->modules.error->handler(comp->modules.error->context, "Cyclic import of module %s", alias);

    // Module is imported iff it has an import alias (which may coincide with the module name if not specified explicitly)
    char **importAlias = &comp->modules.module[comp->blocks.module]->importAlias[importedModule];
    if (*importAlias)
         comp->modules.error->handler(comp->modules.error->context, "Duplicate imported module %s", path);
    *importAlias = alias;

    identAddModule(&comp->idents, &comp->modules, &comp->blocks, alias, comp->voidType, importedModule);

    lexNext(&comp->lex);
}


// import = "import" (importItem | "(" {importItem ";"} ")").
static void parseImport(Compiler *comp)
{
    lexEat(&comp->lex, TOK_IMPORT);

    if (comp->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&comp->lex);
        while (comp->lex.tok.kind == TOK_STRLITERAL || comp->lex.tok.kind == TOK_IDENT)
        {
            parseImportItem(comp);
            lexEat(&comp->lex, TOK_SEMICOLON);
        }
        lexEat(&comp->lex, TOK_RPAR);
    }
    else
        parseImportItem(comp);
}


// module = [import ";"] decls.
static int parseModule(Compiler *comp)
{
    comp->blocks.module = moduleAdd(&comp->modules, comp->lex.fileName);

    if (comp->lex.tok.kind == TOK_IMPORT)
    {
        parseImport(comp);
        lexEat(&comp->lex, TOK_SEMICOLON);
    }
    parseDecls(comp);
    doResolveExtern(comp);

    comp->modules.module[comp->blocks.module]->isCompiled = true;
    return comp->blocks.module;
}


// program = module.
void parseProgram(Compiler *comp)
{
    // Entry point stub
    genNop(&comp->gen);

    lexNext(&comp->lex);
    int mainModule = parseModule(comp);

    // Entry point
    genEntryPoint(&comp->gen, 0);

    Ident *mainFn = identFind(&comp->idents, &comp->modules, &comp->blocks, mainModule, "main", NULL, false);
    if (mainFn && identIsMain(mainFn))
    {
        genPushZero(&comp->gen, sizeof(Interface) / sizeof(Slot));  // Dummy upvalue
        genCall(&comp->gen, mainFn->offset);
    }

    doGarbageCollection(comp, 0);
    genHalt(&comp->gen);
}
