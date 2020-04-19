#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_ident.h"


void identInit(Idents *idents, ErrorFunc error)
{
    idents->first = idents->last = NULL;
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


Ident *identFind(Idents *idents, Blocks *blocks, char *name)
{
    int nameHash = hash(name);

    for (int i = blocks->top; i >= 0; i--)
        for (Ident *ident = idents->first; ident; ident = ident->next)
            if (ident->hash == nameHash && strcmp(ident->name, name) == 0 && ident->block == blocks->item[i].block)
                return ident;

    return NULL;
}


Ident *identAssertFind(Idents *idents, Blocks *blocks, char *name)
{
    Ident *res = identFind(idents, blocks, name);
    if (!res)
        idents->error("Unknown identifier %s", name);
    return res;
}


static void identAdd(Idents *idents, Blocks *blocks, IdentKind kind, char *name, Type *type)
{
    Ident *ident = identFind(idents, blocks, name);
    if (ident && ident->block == blocks->item[blocks->top].block)
        idents->error("Duplicate identifier %s", name);

    ident = malloc(sizeof(Ident));
    ident->kind = kind;

    strcpy(ident->name, name);
    ident->hash = hash(name);

    ident->type = type;
    ident->block = blocks->item[blocks->top].block;
    ident->forward = false;
    ident->inHeap = false;
    ident->next = NULL;

    // Add to list
    if (!idents->first)
        idents->first = idents->last = ident;
    else
    {
        idents->last->next = ident;
        idents->last = ident;
    }
}


void identAddConst(Idents *idents, Blocks *blocks, char *name, Type *type, Const constant)
{
    identAdd(idents, blocks, IDENT_CONST, name, type);
    idents->last->constant = constant;
}


void identAddGlobalVar(Idents *idents, Blocks *blocks, char *name, Type *type, void *ptr)
{
    identAdd(idents, blocks, IDENT_VAR, name, type);
    idents->last->ptr = ptr;
}


void identAddLocalVar(Idents *idents, Blocks *blocks, char *name, Type *type, int offset)
{
    identAdd(idents, blocks, IDENT_VAR, name, type);
    idents->last->offset = offset;
}


void identAddType(Idents *idents, Blocks *blocks, char *name, Type *type)
{
    identAdd(idents, blocks, IDENT_TYPE, name, type);
}


void identAddBuiltinFunc(Idents *idents, Blocks *blocks, char *name, Type *type, BuiltinFunc builtin)
{
    identAdd(idents, blocks, IDENT_BUILTIN_FN, name, type);
    idents->last->builtin = builtin;
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
        idents->error("No stack frame");

    (*localVarSize) += size;
    return -(*localVarSize);
}


void identAllocVar(Idents *idents, Types *types, Blocks *blocks, char *name, Type *type)
{
    if (blocks->top == 0)       // Global
    {
        void *ptr = malloc(typeSize(types, type));
        identAddGlobalVar(idents, blocks, name, type, ptr);
        idents->last->inHeap = true;
    }
    else                        // Local
    {
        int offset = identAllocStack(idents, blocks, typeSize(types, type));
        identAddLocalVar(idents, blocks, name, type, offset);
    }
}


void identAllocParam(Idents *idents, Blocks *blocks, Signature *sig, int index)
{
        // All parameters are slot-aligned, structured parameters are passed as pointers
        int offset = (sig->numParams + 1 - index) * sizeof(Slot);
        identAddLocalVar(idents, blocks, sig->param[index]->name, sig->param[index]->type, offset);
}

