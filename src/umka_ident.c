#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_ident.h"


static void identTempName(Idents *idents, char *buf)
{
    snprintf(buf, DEFAULT_STR_LEN + 1, "__temp%d", idents->tempVarNameSuffix++);
}


void identInit(Idents *idents, DebugInfo *debug, Error *error)
{
    idents->first = idents->last = NULL;
    idents->lastTempVarForResult = NULL;
    idents->tempVarNameSuffix = 0;
    idents->debug = debug;
    idents->error = error;
}


void identFree(Idents *idents, int startBlock)
{
    Ident *ident = idents->first;

    // If block is specified, fast forward to the first identifier in this block (assuming this is the last block in the list)
    if (startBlock >= 0)
    {
        while (ident && ident->next && ident->next->block != startBlock)
            ident = ident->next;

        Ident *next = ident->next;
        idents->last = ident;
        idents->last->next = NULL;
        ident = next;
    }

    while (ident)
    {
        Ident *next = ident->next;

        // Remove globals
        if (ident->globallyAllocated)
            free(ident->ptr);

        free(ident);
        ident = next;
    }
}


static Ident *identFindEx(Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, Type *rcvType, bool markAsUsed, bool isModule)
{
    const unsigned int nameHash = hash(name);

    for (int i = blocks->top; i >= 0; i--)
    {
        for (Ident *ident = idents->first; ident; ident = ident->next)
            if (ident->hash == nameHash && strcmp(ident->name, name) == 0 && ident->block == blocks->item[i].block && (ident->kind == IDENT_MODULE) == isModule)
            {
                // What we found has correct name and block scope, check module scope
                const bool identModuleValid = (ident->module == 0 && blocks->module == module) ||                                                // Universe module
                                              (ident->module == module && (blocks->module == module ||                                           // Current module
                                              (ident->exported && (rcvType || modules->module[blocks->module]->importAlias[ident->module]))));   // Imported module

                if (identModuleValid)
                {
                    // Method names need not be unique in the given scope - check the receiver type to see if we found the right name
                    const bool methodFound = ident->type->kind == TYPE_FN && ident->type->sig.isMethod;

                    const bool found = (!rcvType && !methodFound) || (rcvType && methodFound && typeCompatibleRcv(ident->type->sig.param[0]->type, rcvType));
                    if (found)
                    {
                        if (markAsUsed)
                            ident->used = true;
                        return ident;
                    }
                }
            }
    }

    return NULL;
}


Ident *identFind(Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, Type *rcvType, bool markAsUsed)
{
    return identFindEx(idents, modules, blocks, module, name, rcvType, markAsUsed, false);
}


Ident *identAssertFind(Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, Type *rcvType)
{
    Ident *res = identFind(idents, modules, blocks, module, name, rcvType, true);
    if (!res)
        idents->error->handler(idents->error->context, "Unknown identifier %s", name);
    return res;
}


Ident *identFindModule(Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, bool markAsUsed)
{
    return identFindEx(idents, modules, blocks, module, name, NULL, markAsUsed, true);
}


Ident *identAssertFindModule(Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name)
{
    Ident *res = identFindModule(idents, modules, blocks, module, name, true);
    if (!res)
        idents->error->handler(idents->error->context, "Unknown module %s", name);
    return res;
}


bool identIsOuterLocalVar(Blocks *blocks, Ident *ident)
{
    if (!ident || ident->kind != IDENT_VAR || ident->block == 0)
        return false;

    bool curFnBlockFound = false;
    for (int i = blocks->top; i >= 0; i--)
    {
        if (blocks->item[i].block == ident->block && curFnBlockFound)
            return true;

        if (blocks->item[i].fn)
            curFnBlockFound = true;
    }

    return false;
}


static Ident *identAdd(Idents *idents, Modules *modules, Blocks *blocks, IdentKind kind, const char *name, Type *type, bool exported)
{
    Type *rcvType = NULL;
    if (type->kind == TYPE_FN && type->sig.isMethod)
        rcvType = type->sig.param[0]->type;

    Ident *ident = identFind(idents, modules, blocks, blocks->module, name, rcvType, false);

    if (ident && ident->block == blocks->item[blocks->top].block)
    {
        // Forward type declaration resolution
        if (ident->kind == IDENT_TYPE && ident->type->kind == TYPE_FORWARD &&
            kind == IDENT_TYPE && type->kind != TYPE_FORWARD &&
            strcmp(ident->type->typeIdent->name, name) == 0)
        {
            type->typeIdent = ident;
            typeDeepCopy(ident->type, type);
            ident->exported = exported;
            return ident;
        }

        // Function prototype resolution
        if (ident->kind == IDENT_CONST && ident->type->kind == TYPE_FN &&
            kind == IDENT_CONST && type->kind == TYPE_FN &&
            ident->exported == exported &&
            strcmp(ident->name, name) == 0 &&
            typeCompatible(ident->type, type) &&
            ident->prototypeOffset >= 0)
        {
            typeDeepCopy(ident->type, type);
            return ident;
        }

        idents->error->handler(idents->error->context, "Duplicate identifier %s", name);
    }

    if (exported && blocks->top != 0)
        idents->error->handler(idents->error->context, "Local identifier %s cannot be exported", name);

    if (kind == IDENT_CONST || kind == IDENT_VAR)
    {
        if (type->kind == TYPE_FORWARD)
            idents->error->handler(idents->error->context, "Unresolved forward type declaration for %s", name);

        if (type->kind == TYPE_VOID)
            idents->error->handler(idents->error->context, "Void variable or constant %s is not allowed", name);
    }

    ident = malloc(sizeof(Ident));
    ident->kind = kind;

    strncpy(ident->name, name, MAX_IDENT_LEN);
    ident->name[MAX_IDENT_LEN] = 0;

    ident->hash = hash(name);

    ident->type              = type;
    ident->module            = blocks->module;
    ident->block             = blocks->item[blocks->top].block;
    ident->exported          = exported;
    ident->globallyAllocated = false;
    ident->temporary         = false;
    ident->used              = exported || ident->module == 0 || ident->name[0] == '_' || identIsMain(ident);  // Exported, predefined, temporary identifiers and main() are always treated as used
    ident->prototypeOffset   = -1;
    ident->debug             = *(idents->debug);
    ident->next              = NULL;

    // Add to list
    if (!idents->first)
        idents->first = idents->last = ident;
    else
    {
        idents->last->next = ident;
        idents->last = ident;
    }

    return idents->last;
}


Ident *identAddConst(Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported, Const constant)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_CONST, name, type, exported);
    ident->constant = constant;
    return ident;
}


Ident *identAddTempConst(Idents *idents, Modules *modules, Blocks *blocks, Type *type, Const constant)
{
    IdentName tempName;
    identTempName(idents, tempName);

    Ident *ident = identAddConst(idents, modules, blocks, tempName, type, false, constant);
    ident->temporary = true;
    return ident;
}


Ident *identAddGlobalVar(Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported, void *ptr)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_VAR, name, type, exported);
    ident->ptr = ptr;
    return ident;
}


Ident *identAddLocalVar(Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported, int offset)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_VAR, name, type, exported);
    ident->offset = offset;
    return ident;
}


Ident *identAddType(Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported)
{
    return identAdd(idents, modules, blocks, IDENT_TYPE, name, type, exported);
}


Ident *identAddBuiltinFunc(Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, BuiltinFunc builtin)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_BUILTIN_FN, name, type, false);
    ident->builtin = builtin;
    return ident;
}


Ident *identAddModule(Idents *idents, Modules *modules, Blocks *blocks, const char *name, Type *type, int moduleVal)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_MODULE, name, type, false);
    ident->moduleVal = moduleVal;
    return ident;
}


int identAllocStack(Idents *idents, Types *types, Blocks *blocks, Type *type)
{
    int *localVarSize = NULL;
    for (int i = blocks->top; i >= 1; i--)
        if (blocks->item[i].fn)
        {
            localVarSize = &blocks->item[i].localVarSize;
            break;
        }
    if (!localVarSize)
        idents->error->handler(idents->error->context, "No heap frame");

    *localVarSize = align(*localVarSize + typeSize(types, type), typeAlignment(types, type));
    return -2 * sizeof(Slot) - (*localVarSize);  // 2 extra slots for the stack frame ref count and parameter layout table
}


Ident *identAllocVar(Idents *idents, Types *types, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported)
{
    Ident *ident;
    if (blocks->top == 0)       // Global
    {
        void *ptr = malloc(typeSize(types, type));
        ident = identAddGlobalVar(idents, modules, blocks, name, type, exported, ptr);
        ident->globallyAllocated = true;
    }
    else                        // Local
    {
        int offset = identAllocStack(idents, types, blocks, type);
        ident = identAddLocalVar(idents, modules, blocks, name, type, exported, offset);
    }
    return ident;
}


Ident *identAllocTempVar(Idents *idents, Types *types, Modules *modules, Blocks *blocks, Type *type, bool isFuncResult)
{
    IdentName tempName;
    identTempName(idents, tempName);

    Ident *ident = identAllocVar(idents, types, modules, blocks, tempName, type, false);
    ident->temporary = true;

    if (isFuncResult)
    {
        if (blocks->top == 0)
            idents->error->handler(idents->error->context, "Temporary variable must be local");
        idents->lastTempVarForResult = ident;
    }

    return ident;
}


Ident *identAllocParam(Idents *idents, Types *types, Modules *modules, Blocks *blocks, Signature *sig, int index)
{
    int offset =typeParamOffset(types, sig, index);
    Ident *ident = identAddLocalVar(idents, modules, blocks, sig->param[index]->name, sig->param[index]->type, false, offset);
    ident->used = true;                                                     // Do not warn about unused parameters
    return ident;
}


char *identMethodNameWithRcv(Ident *method, char *buf, int size)
{
    char typeBuf[DEFAULT_STR_LEN + 1];
    typeSpelling(method->type->sig.param[0]->type, typeBuf);
    snprintf(buf, size, "(%s)%s", typeBuf, method->name);
    return buf;
}


void identWarnIfUnused(Idents *idents, Ident *ident)
{
    if (!ident->temporary && !ident->used)
    {
        idents->error->warningHandler(idents->error->context, &ident->debug, "%s %s is not used", (ident->kind == IDENT_MODULE ? "Module" : "Identifier"), ident->name);
        ident->used = true;
    }
}


void identWarnIfUnusedAll(Idents *idents, int block)
{
    for (Ident *ident = idents->first; ident; ident = ident->next)
        if (ident->block == block)
            identWarnIfUnused(idents, ident);
}


bool identIsMain(Ident *ident)
{
    return strcmp(ident->name, "main") == 0 && ident->kind == IDENT_CONST &&
           ident->type->kind == TYPE_FN && !ident->type->sig.isMethod && ident->type->sig.numParams == 1 && ident->type->sig.resultType->kind == TYPE_VOID;  // A dummy "__upvalues" is the only parameter
}
