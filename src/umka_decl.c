#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <string.h>

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


// typedIdentList = identList ":" type.
static void parseTypedIdentList(Compiler *comp, IdentName *names, bool *exported, int capacity, int *num, Type **type)
{
    parseIdentList(comp, names, exported, capacity, num);
    lexEat(&comp->lex, TOK_COLON);
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

    sig->method = true;
    typeAddParam(&comp->types, sig, rcvType, rcvName);

    lexEat(&comp->lex, TOK_RPAR);
}


// signature = "(" [typedIdentList ["=" expr] {"," typedIdentList ["=" expr]}] ")" [":" (type | "(" type {"," type} ")")].
static void parseSignature(Compiler *comp, Signature *sig)
{
    // Formal parameter list
    lexEat(&comp->lex, TOK_LPAR);
    int numDefaultParams = 0;

    if (comp->lex.tok.kind == TOK_IDENT)
    {
        while (1)
        {
            IdentName paramNames[MAX_PARAMS];
            bool paramExported[MAX_PARAMS];
            Type *paramType;
            int numParams = 0;
            parseTypedIdentList(comp, paramNames, paramExported, MAX_PARAMS, &numParams, &paramType);

            // ["=" expr]
            Const defaultConstant;
            if (comp->lex.tok.kind == TOK_EQ)
            {
                if (numParams != 1)
                    comp->error.handler(comp->error.context, "Parameter list cannot have common default value");

                lexNext(&comp->lex);

                Type *defaultType;
                parseExpr(comp, &defaultType, &defaultConstant);

                if (typeStructured(defaultType))
                    comp->error.handler(comp->error.context, "Structured default values are not allowed");

                doImplicitTypeConv(comp, paramType, &defaultType, &defaultConstant, false);
                typeAssertCompatible(&comp->types, paramType, defaultType, false);
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
            sig->resultType = typeAdd(&comp->types, &comp->blocks, TYPE_STRUCT);
            sig->resultType->isExprList = true;

            lexNext(&comp->lex);

            while (1)
            {
                Type *fieldType = parseType(comp, NULL);
                typeAddField(&comp->types, sig->resultType, fieldType, NULL);

                if (comp->lex.tok.kind != TOK_COMMA)
                    break;
                lexNext(&comp->lex);
            }
            lexEat(&comp->lex, TOK_RPAR);
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
    Type *type = NULL;

    // Forward declaration
    bool forward = false;
    if (comp->lex.tok.kind == TOK_IDENT)
    {
        int module = moduleFind(&comp->modules, comp->lex.tok.name);
        if (module < 0)
        {
            Ident *ident = identFind(&comp->idents, &comp->modules, &comp->blocks, comp->blocks.module, comp->lex.tok.name, NULL);
            if (!ident)
            {
                IdentName name;
                strcpy(name, comp->lex.tok.name);

                lexNext(&comp->lex);
                bool exported = parseExportMark(comp);

                type = typeAdd(&comp->types, &comp->blocks, TYPE_FORWARD);
                type->typeIdent = identAddType(&comp->idents, &comp->modules, &comp->blocks, name, type, exported);

                forward = true;
            }
        }
    }

    // Conventional declaration
    if (!forward)
        type = parseType(comp, NULL);

    type = typeAddPtrTo(&comp->types, &comp->blocks, type);
    type->weak = weak;
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
        Type *indexType;
        parseExpr(comp, &indexType, &len);
        typeAssertCompatible(&comp->types, comp->intType, indexType, false);
        if (len.intVal < 0)
            comp->error.handler(comp->error.context, "Array length cannot be negative");
    }

    lexEat(&comp->lex, TOK_RBRACKET);

    Type *baseType = parseType(comp, NULL);

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
        bool fieldExported[MAX_FIELDS];
        Type *fieldType;
        int numFields = 0;
        parseTypedIdentList(comp, fieldNames, fieldExported, MAX_FIELDS, &numFields, &fieldType);

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

    // __self, __selftype
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

            typeAddParam(&comp->types, &methodType->sig, comp->ptrVoidType, "__self");
            parseSignature(comp, &methodType->sig);

            Field *method = typeAddField(&comp->types, type, methodType, methodName);
            methodType->sig.method = true;
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
                methodType->sig.method = true;
                methodType->sig.offsetFromSelf = method->offset;
            }
        }

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


// type = qualIdent | ptrType | arrayType | dynArrayType | strType | structType | interfaceType | fnType.
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
        case TOK_STRUCT:    return parseStructType(comp);
        case TOK_INTERFACE: return parseInterfaceType(comp);
        case TOK_FN:        return parseFnType(comp);

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

    typeAssertForwardResolved(&comp->types);
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
        constant->intVal++;
    else
    {
        lexEat(&comp->lex, TOK_EQ);
        parseExpr(comp, type, constant);
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
    IdentName varNames[MAX_FIELDS];
    bool varExported[MAX_FIELDS];
    int numVars = 0;
    Type *varType;
    parseTypedIdentList(comp, varNames, varExported, MAX_FIELDS, &numVars, &varType);

    Ident *var[MAX_FIELDS];
    for (int i = 0; i < numVars; i++)
    {
        var[i] = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, varNames[i], varType, varExported[i]);
        doZeroVar(comp, var[i]);
    }

    // Initializer
    if (comp->lex.tok.kind == TOK_EQ)
    {
        Type *designatorType = typeAddPtrTo(&comp->types, &comp->blocks, var[0]->type);
        Type *designatorListType;

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

        Const varPtrConstList[MAX_FIELDS] = {0};

        for (int i = 0; i < numVars; i++)
        {
            if (comp->blocks.top == 0)          // Globals are initialized with constant expressions
                varPtrConstList[i].ptrVal = (int64_t)var[i]->ptr;
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
    IdentName varNames[MAX_FIELDS];
    bool varExported[MAX_FIELDS];
    int numVars = 0;
    parseIdentList(comp, varNames, varExported, MAX_FIELDS, &numVars);

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
    if (fnType->sig.method)
    {
        Type *rcvBaseType = fnType->sig.param[0]->type->base;

        if (rcvBaseType->kind == TYPE_STRUCT && typeFindField(rcvBaseType, name))
            comp->error.handler(comp->error.context, "Structure already has field %s", name);
    }

    lexNext(&comp->lex);
    bool exported = parseExportMark(comp);

    parseSignature(comp, &fnType->sig);

    Const constant = {.intVal = comp->gen.ip};
    Ident *fn = identAddConst(&comp->idents, &comp->modules, &comp->blocks, name, fnType, exported, constant);

    if (comp->lex.tok.kind == TOK_LBRACE)
        parseFnBlock(comp, fn);
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
        if (comp->lex.tok.kind != TOK_SEMICOLON)
            break;
        lexNext(&comp->lex);
    }
}


// importItem = stringLiteral.
static void parseImportItem(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_STRLITERAL);

    char path[DEFAULT_STR_LEN + 1];
    char *folder = comp->modules.module[comp->blocks.module]->folder;
    snprintf(path, DEFAULT_STR_LEN + 1, "%s%s", folder, comp->lex.tok.strVal);

    int importedModule = moduleFindByPath(&comp->modules, path);
    if (importedModule < 0)
    {
        // Module source strings, if any, have precedence over files
        char *sourceString = moduleFindSourceByPath(&comp->modules, path);

        // Save context
        int currentModule       = comp->blocks.module;
        DebugInfo currentDebug  = comp->debug;
        Lexer currentLex        = comp->lex;
        lexInit(&comp->lex, &comp->cfg, &comp->storage, &comp->debug, path, sourceString, &comp->error);

        lexNext(&comp->lex);
        importedModule = parseModule(comp);

        // Restore context
        lexFree(&comp->lex);
        comp->lex               = currentLex;
        comp->debug             = currentDebug;
        comp->blocks.module     = currentModule;
    }

    comp->modules.module[comp->blocks.module]->imports[importedModule] = true;
    lexNext(&comp->lex);
}


// import = "import" (importItem | "(" {importItem ";"} ")").
static void parseImport(Compiler *comp)
{
    lexEat(&comp->lex, TOK_IMPORT);

    if (comp->lex.tok.kind == TOK_LPAR)
    {
        lexNext(&comp->lex);
        while (comp->lex.tok.kind == TOK_STRLITERAL)
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

    Ident *mainFn = identAssertFind(&comp->idents, &comp->modules, &comp->blocks, mainModule, "main", NULL);

    if (mainFn->kind != IDENT_CONST || mainFn->type->kind != TYPE_FN || mainFn->type->sig.method ||
        mainFn->type->sig.numParams != 0 || mainFn->type->sig.resultType->kind != TYPE_VOID)
        comp->error.handler(comp->error.context, "Illegal main() function");

    genCall(&comp->gen, mainFn->offset);
    doGarbageCollection(comp, 0);
    genHalt(&comp->gen);
}
