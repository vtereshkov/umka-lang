#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_ident.h"


void identInit(Idents *idents, Error *error)
{
    idents->first = idents->last = NULL;
    idents->tempVarNameSuffix = 0;
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

        // Remove heap-allocated globals
        if (ident->inHeap)
            free(ident->ptr);

        free(ident);
        ident = next;
    }
}


Ident *identFind(Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, Type *rcvType)
{
    unsigned int nameHash = hash(name);
    int curFnBlockPos = -1;

    for (int i = blocks->top; i >= 0; i--)
    {
        // Forbid accessing locals of any function other than the current one
        if (curFnBlockPos == -1 && blocks->item[i].fn)
            curFnBlockPos = i;

        if (i > 0 && i < curFnBlockPos)
            continue;

        for (Ident *ident = idents->first; ident; ident = ident->next)
            if (ident->hash == nameHash && strcmp(ident->name, name) == 0 && ident->block == blocks->item[i].block &&

               // What we found has correct name and block scope, check module scope
               (ident->module == 0 ||                                                               // Universe module
               (ident->module == module && (blocks->module == module ||                             // Current module
               (ident->exported && modules->module[blocks->module]->imports[ident->module])))))     // Imported module
            {
                    bool method = ident->type->kind == TYPE_FN && ident->type->sig.method;

                    // We don't need a method and what we found is not a method
                    if (!rcvType && !method)
                        return ident;

                    // We need a method and what we found is a method
                    if (rcvType && method && (typeCompatible(ident->type->sig.param[0]->type, rcvType, false) ||
                                              typeCompatible(rcvType, ident->type->sig.param[0]->type, false)))
                        return ident;
            }
    }

    return NULL;
}


Ident *identAssertFind(Idents *idents, Modules *modules, Blocks *blocks, int module, const char *name, Type *rcvType)
{
    Ident *res = identFind(idents, modules, blocks, module, name, rcvType);
    if (!res)
        idents->error->handler(idents->error->context, "Unknown identifier %s", name);
    return res;
}


static Ident *identAdd(Idents *idents, Modules *modules, Blocks *blocks, IdentKind kind, const char *name, Type *type, bool exported)
{
    Type *rcvType = NULL;
    if (type->kind == TYPE_FN && type->sig.method)
        rcvType = type->sig.param[0]->type;

    Ident *ident = identFind(idents, modules, blocks, blocks->module, name, rcvType);

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
            typeCompatible(ident->type, type, false) &&
            ident->prototypeOffset >= 0)
        {
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

    strcpy(ident->name, name);
    ident->hash = hash(name);

    ident->type             = type;
    ident->module           = blocks->module;
    ident->block            = blocks->item[blocks->top].block;
    ident->exported         = exported;
    ident->inHeap           = false;
    ident->prototypeOffset  = -1;
    ident->next             = NULL;

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


int identAllocStack(Idents *idents, Blocks *blocks, int size)
{
    int *localVarSize = NULL;
    for (int i = blocks->top; i >= 1; i--)
        if (blocks->item[i].fn)
        {
            localVarSize = &blocks->item[i].localVarSize;
            break;
        }
    if (!localVarSize)
        idents->error->handler(idents->error->context, "No stack frame");

    (*localVarSize) += size;
    return -(*localVarSize);
}


Ident *identAllocVar(Idents *idents, Types *types, Modules *modules, Blocks *blocks, const char *name, Type *type, bool exported)
{
    Ident *ident;
    if (blocks->top == 0)       // Global
    {
        void *ptr = malloc(typeSize(types, type));
        ident = identAddGlobalVar(idents, modules, blocks, name, type, exported, ptr);
        ident->inHeap = true;
    }
    else                        // Local
    {
        int offset = identAllocStack(idents, blocks, typeSize(types, type));
        ident = identAddLocalVar(idents, modules, blocks, name, type, exported, offset);
    }
    return ident;
}


Ident *identAllocParam(Idents *idents, Types *types, Modules *modules, Blocks *blocks, Signature *sig, int index)
{
    int paramSizeUpToIndex = typeParamSizeUpTo(types, sig, index);
    int paramSizeTotal     = typeParamSizeTotal(types, sig);

    int offset = (paramSizeTotal - paramSizeUpToIndex) + 2 * sizeof(Slot);  // + 2 slots for old base pointer and return address
    return identAddLocalVar(idents, modules, blocks, sig->param[index]->name, sig->param[index]->type, false, offset);
}


char *identTempVarName(Idents *idents, char *buf)
{
    sprintf(buf, "__temp%d", idents->tempVarNameSuffix++);
    return buf;
}

