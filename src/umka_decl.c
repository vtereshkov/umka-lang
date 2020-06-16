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
        lexNext(&comp->lex);
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
            comp->error("Too many identifiers");
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

    if (rcvType->kind != TYPE_PTR)
        comp->error("Receiver should be a pointer");

    sig->method = true;
    typeAddParam(&comp->types, sig, rcvType, rcvName);

    lexEat(&comp->lex, TOK_RPAR);
}


// signature = "(" [typedIdentList ["=" expr] {"," typedIdentList ["=" expr]}] ")" [":" type].
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
                    comp->error("Parameter list cannot have common default value");

                lexNext(&comp->lex);

                Type *defaultType;
                parseExpr(comp, &defaultType, &defaultConstant);

                if (typeStructured(defaultType))
                    comp->error("Structured default values are not allowed");

                doImplicitTypeConv(comp, paramType, &defaultType, &defaultConstant, false);
                typeAssertCompatible(&comp->types, paramType, defaultType, false);
                numDefaultParams++;
            }
            else
            {
                if (numDefaultParams != 0)
                    comp->error("Parameters with default values should be the last ones");
            }

            for (int i = 0; i < numParams; i++)
            {
                if (paramExported[i])
                    comp->error("Parameter %s cannot be exported", paramNames[i]);

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
    sig->numResults = 0;
    if (comp->lex.tok.kind == TOK_COLON)
    {
        lexNext(&comp->lex);
        sig->resultType[sig->numResults++] = parseType(comp, NULL);
    }
    else
        sig->resultType[sig->numResults++] = comp->voidType;

    // Structured result parameter
    if (typeStructured(sig->resultType[0]))
        typeAddParam(&comp->types, sig, typeAddPtrTo(&comp->types, &comp->blocks, sig->resultType[0]), "__result");
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
    Type *type;

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
                type->forwardIdent = identAddType(&comp->idents, &comp->modules, &comp->blocks, name, type, exported);

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
            comp->error("Array length cannot be negative");
    }

    lexEat(&comp->lex, TOK_RBRACKET);

    Type *baseType = parseType(comp, NULL);

    Type *type = typeAdd(&comp->types, &comp->blocks, typeKind);
    type->base = baseType;
    type->numItems = len.intVal;
    return type;
}


// strType = "str" ["[" expr "]"].
static Type *parseStrType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_STR);

    Const len;
    Type *indexType;

    if (comp->lex.tok.kind == TOK_LBRACKET)
    {
        lexNext(&comp->lex);
        parseExpr(comp, &indexType, &len);
        typeAssertCompatible(&comp->types, comp->intType, indexType, false);
        if (len.intVal < 0)
            comp->error("String length cannot be negative");

        lexEat(&comp->lex, TOK_RBRACKET);
    }
    else    // Default string
    {
        len.intVal = DEFAULT_STR_LEN + 1;
        indexType = comp->intType;
    }

    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_STR);
    type->base = comp->charType;
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
        bool fieldExported[MAX_FIELDS];
        Type *fieldType;
        int numFields = 0;
        parseTypedIdentList(comp, fieldNames, fieldExported, MAX_FIELDS, &numFields, &fieldType);

        for (int i = 0; i < numFields; i++)
        {
            typeAddField(&comp->types, type, fieldType, fieldNames[i]);
            if (fieldExported[i])
                comp->error("Field %s cannot be exported", fieldNames[i]);
        }

        lexEat(&comp->lex, TOK_SEMICOLON);
    }
    lexEat(&comp->lex, TOK_RBRACE);
    return type;
}


// interfaceType = "interface" "{" {ident signature ";"} "}"
static Type *parseInterfaceType(Compiler *comp)
{
    lexEat(&comp->lex, TOK_INTERFACE);
    lexEat(&comp->lex, TOK_LBRACE);

    Type *type = typeAdd(&comp->types, &comp->blocks, TYPE_INTERFACE);
    type->numItems = 0;

    // __self, __selftype
    typeAddField(&comp->types, type, comp->ptrVoidType, "__self");
    typeAddField(&comp->types, type, comp->ptrVoidType, "__selftype");

    // Methods
    while (comp->lex.tok.kind == TOK_IDENT)
    {
        IdentName methodName;
        strcpy(methodName, comp->lex.tok.name);
        lexNext(&comp->lex);

        Type *methodType = typeAdd(&comp->types, &comp->blocks, TYPE_FN);

        typeAddParam(&comp->types, &methodType->sig, comp->ptrVoidType, "__self");
        parseSignature(comp, &methodType->sig);

        Field *method = typeAddField(&comp->types, type, methodType, methodName);
        methodType->sig.method = true;
        methodType->sig.offsetFromSelf = method->offset;

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


// type = qualIdent | ptrType | arrayType | dynArrayType | strType | structType | fnType.
Type *parseType(Compiler *comp, Ident *ident)
{
    if (ident)
    {
        if (ident->kind != IDENT_TYPE)
            comp->error("Type expected");
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

        default:            comp->error("Type expected"); return NULL;
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

    identAddType(&comp->idents, &comp->modules, &comp->blocks, name, type, exported);
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

    typeAssertForwardResolved(&comp->types);
}


// constDeclItem = ident exportMark "=" expr.
static void parseConstDeclItem(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);

    lexNext(&comp->lex);
    bool exported = parseExportMark(comp);

    lexEat(&comp->lex, TOK_EQ);
    Type *type;
    Const constant;
    parseExpr(comp, &type, &constant);

    identAddConst(&comp->idents, &comp->modules, &comp->blocks, name, type, exported, constant);
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
    bool varExported[MAX_FIELDS];
    int numVars = 0;
    Type *varType;
    parseTypedIdentList(comp, varNames, varExported, MAX_FIELDS, &numVars, &varType);

    Ident *var[MAX_FIELDS];
    for (int i = 0; i < numVars; i++)
        var[i] = identAllocVar(&comp->idents, &comp->types, &comp->modules, &comp->blocks, varNames[i], varType, varExported[i]);

    // Initializer
    if (comp->lex.tok.kind == TOK_EQ)
    {
        if (numVars != 1)
            comp->error("Unable to initialize multiple variables");

        Type *designatorType = typeAddPtrTo(&comp->types, &comp->blocks, var[0]->type);

        void *initializedVarPtr = NULL;

        if (comp->blocks.top == 0)          // Globals are initialized with constant expressions
            initializedVarPtr = var[0]->ptr;
        else                                // Locals are assigned to
            doPushVarPtr(comp, var[0]);

        lexNext(&comp->lex);
        parseAssignmentStmt(comp, designatorType, initializedVarPtr);
    }
    // Zeros (locals are zeroed automatically upon entering stack frame)
    else if (var[0]->block == 0)
        for (int i = 0; i < numVars; i++)
            constZero(var[i]->ptr, typeSize(&comp->types, var[i]->type));
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


// shortVarDecl = declAssignmentStmt.
void parseShortVarDecl(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);

    lexNext(&comp->lex);
    bool exported = parseExportMark(comp);

    lexEat(&comp->lex, TOK_COLONEQ);

    parseDeclAssignmentStmt(comp, name, comp->blocks.top == 0, exported);
}


// fnDecl = "fn" [rcvSignature] ident exportMark signature [block].
void parseFnDecl(Compiler *comp)
{
    if (comp->blocks.top != 0)
        comp->error("Nested functions are not allowed");

    lexEat(&comp->lex, TOK_FN);
    Type *fnType = typeAdd(&comp->types, &comp->blocks, TYPE_FN);

    if (comp->lex.tok.kind == TOK_LPAR)
        parseRcvSignature(comp, &fnType->sig);

    lexCheck(&comp->lex, TOK_IDENT);
    IdentName name;
    strcpy(name, comp->lex.tok.name);

    // Check for method/field name collision
    if (fnType->sig.method && fnType->sig.param[0]->type->kind == TYPE_STRUCT)
    {
        Field *field = typeFindField(fnType->sig.param[0]->type, name);
        if (field)
            comp->error("Structure already has field %s", name);
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
        case TOK_VAR:    parseVarDecl(comp);        break;
        case TOK_IDENT:  parseShortVarDecl(comp);   break;
        case TOK_FN:     parseFnDecl(comp);         break;

        case TOK_EOF:    if (comp->blocks.top == 0)
                             break;

        default: comp->error("Declaration expected but %s found", lexSpelling(comp->lex.tok.kind)); break;
    }
}


// decls = decl {";" decl}.
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


// importItem = stringLiteral.
static void parseImportItem(Compiler *comp)
{
    lexCheck(&comp->lex, TOK_STRLITERAL);

    char path[DEFAULT_STR_LEN + 1];
    char *folder = comp->modules.module[comp->blocks.module]->folder;
    sprintf(path, "%s%s", folder, comp->lex.tok.strVal);

    int importedModule = moduleFindByPath(&comp->modules, path);
    if (importedModule < 0)
    {
        // Save context
        int currentModule       = comp->blocks.module;
        DebugInfo currentDebug  = comp->debug;
        Lexer currentLex        = comp->lex;
        lexInit(&comp->lex, &comp->storage, &comp->debug, path, comp->error);

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
    return comp->blocks.module;
}


// program = module.
void parseProgram(Compiler *comp)
{
    // Entry point stub
    genNop(&comp->gen);

    lexNext(&comp->lex);
    parseModule(comp);
    doResolveExtern(comp);

    if (!comp->gen.mainDefined)
        comp->error("main() is not defined");
}
