#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_ident.h"


static void identTempName(Idents *idents, char *buf)
{
    snprintf(buf, DEFAULT_STR_LEN + 1, "#temp%d", idents->tempVarNameSuffix++);
}


void identInit(Idents *idents, Storage *storage, DebugInfo *debug, Error *error)
{
    idents->first = NULL;
    idents->lastTempVarForResult = NULL;
    idents->tempVarNameSuffix = 0;
    idents->storage = storage;
    idents->debug = debug;
    idents->error = error;
}


void identFree(Idents *idents, int block)
{
    while (idents->first && idents->first->block == block)
    {
        Ident *next = idents->first->next;

        if (idents->first->globallyAllocated)
            storageRemove(idents->storage, idents->first->ptr);

        storageRemove(idents->storage, idents->first);
        idents->first = next;
    }
}


static const Ident *identFindEx(const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name, const Type *rcvType, bool markAsUsed, bool isModule)
{
    const unsigned int nameHash = hash(name);

    for (int i = blocks->top; i >= 0; i--)
    {
        for (const Ident *ident = idents->first; ident; ident = ident->next)
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
                            identSetUsed(ident);
                        return ident;
                    }
                }
            }
    }

    return NULL;
}


const Ident *identFind(const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name, const Type *rcvType, bool markAsUsed)
{
    return identFindEx(idents, modules, blocks, module, name, rcvType, markAsUsed, false);
}


const Ident *identAssertFind(const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name, const Type *rcvType)
{
    const Ident *res = identFind(idents, modules, blocks, module, name, rcvType, true);
    if (!res)
        idents->error->handler(idents->error->context, "Unknown identifier %s", name);
    return res;
}


const Ident *identFindModule(const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name, bool markAsUsed)
{
    return identFindEx(idents, modules, blocks, module, name, NULL, markAsUsed, true);
}


const Ident *identAssertFindModule(const Idents *idents, const Modules *modules, const Blocks *blocks, int module, const char *name)
{
    const Ident *res = identFindModule(idents, modules, blocks, module, name, true);
    if (!res)
        idents->error->handler(idents->error->context, "Unknown module %s", name);
    return res;
}


bool identIsOuterLocalVar(const Blocks *blocks, const Ident *ident)
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


static Ident *identAdd(Idents *idents, const Modules *modules, const Blocks *blocks, IdentKind kind, const char *name, const Type *type, bool exported)
{
    const Type *rcvType = NULL;
    if (type->kind == TYPE_FN && type->sig.isMethod)
        rcvType = type->sig.param[0]->type;

    const Ident *existingIdent = identFindEx(idents, modules, blocks, blocks->module, name, rcvType, false, kind == IDENT_MODULE);

    if (existingIdent)
    {
        if (existingIdent->block == blocks->item[blocks->top].block)
        {
            // Forward type declaration resolution
            if (existingIdent->kind == IDENT_TYPE && existingIdent->type->kind == TYPE_FORWARD &&
                kind == IDENT_TYPE && type->kind != TYPE_FORWARD &&
                strcmp(existingIdent->type->typeIdent->name, name) == 0)
            {
                Ident *modifiableIdent = (Ident *)existingIdent;
                Type *modifiableType = (Type *)type;
                Type *modifiableIdentType = (Type *)existingIdent->type;

                modifiableType->typeIdent = existingIdent;
                typeDeepCopy(idents->storage, modifiableIdentType, type);
                modifiableIdent->exported = exported;

                return modifiableIdent;
            }

            // Function prototype resolution
            if (existingIdent->kind == IDENT_CONST && existingIdent->type->kind == TYPE_FN &&
                kind == IDENT_CONST && type->kind == TYPE_FN &&
                existingIdent->exported == exported &&
                strcmp(existingIdent->name, name) == 0 &&
                typeCompatible(existingIdent->type, type) &&
                existingIdent->prototypeOffset >= 0)
            {
                Ident *modifiableIdent = (Ident *)existingIdent;
                Type *modifiableIdentType = (Type *)existingIdent->type;

                typeDeepCopy(idents->storage, modifiableIdentType, type);

                return modifiableIdent;
            }

            idents->error->handler(idents->error->context, "Duplicate identifier %s", name);
        }
        else if (!identIsHidden(name) && !identIsPlaceholder(name) && existingIdent->block != 0 && !identIsOuterLocalVar(blocks, existingIdent))
        {
            idents->error->warningHandler(idents->error->context, idents->debug, "Shadowed identifier %s", name);
        }
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

    Ident *ident = storageAdd(idents->storage, sizeof(Ident));
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
    ident->used              = exported || ident->module == 0 || identIsHidden(ident->name) || identIsPlaceholder(ident->name) || identIsMain(ident);  // Exported, predefined, hidden, placeholder identifiers and main() are always treated as used
    ident->prototypeOffset   = -1;
    ident->debug             = *(idents->debug);

    ident->next   = idents->first;
    idents->first = ident;

    return ident;
}


Ident *identAddConst(Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported, Const constant)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_CONST, name, type, exported);
    ident->constant = constant;
    return ident;
}


Ident *identAddTempConst(Idents *idents, const Modules *modules, const Blocks *blocks, const Type *type, Const constant)
{
    IdentName tempName;
    identTempName(idents, tempName);

    Ident *ident = identAddConst(idents, modules, blocks, tempName, type, false, constant);
    ident->temporary = true;
    return ident;
}


Ident *identAddGlobalVar(Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported, void *ptr)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_VAR, name, type, exported);
    ident->ptr = ptr;
    ident->globallyAllocated = true;
    return ident;
}


Ident *identAddLocalVar(Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported, int offset)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_VAR, name, type, exported);
    ident->offset = offset;
    return ident;
}


Ident *identAddType(Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, bool exported)
{
    return identAdd(idents, modules, blocks, IDENT_TYPE, name, type, exported);
}


Ident *identAddBuiltinFunc(Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, BuiltinFunc builtin)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_BUILTIN_FN, name, type, false);
    ident->builtin = builtin;
    return ident;
}


Ident *identAddModule(Idents *idents, const Modules *modules, const Blocks *blocks, const char *name, const Type *type, int moduleVal)
{
    Ident *ident = identAdd(idents, modules, blocks, IDENT_MODULE, name, type, false);
    ident->moduleVal = moduleVal;
    return ident;
}


int identAllocStack(Idents *idents, const Types *types, Blocks *blocks, const Type *type)
{
    int *localVarSize = NULL;
    for (int i = blocks->top; i >= 1; i--)
        if (blocks->item[i].fn)
        {
            localVarSize = &blocks->item[i].localVarSize;
            break;
        }
    if (!localVarSize)
        idents->error->handler(idents->error->context, "Stack frame is not found");

    *localVarSize = align(*localVarSize + typeSize(types, type), typeAlignment(types, type));
    return -2 * sizeof(Slot) - (*localVarSize);  // 2 extra slots for the stack frame ref count and parameter layout table
}


Ident *identAllocVar(Idents *idents, const Types *types, const Modules *modules, Blocks *blocks, const char *name, const Type *type, bool exported)
{
    Ident *ident;
    if (blocks->top == 0)       // Global
    {
        void *ptr = storageAdd(idents->storage, typeSize(types, type));
        ident = identAddGlobalVar(idents, modules, blocks, name, type, exported, ptr);
    }
    else                        // Local
    {
        const int offset = identAllocStack(idents, types, blocks, type);
        ident = identAddLocalVar(idents, modules, blocks, name, type, exported, offset);
    }
    return ident;
}


Ident *identAllocTempVar(Idents *idents, const Types *types, const Modules *modules, Blocks *blocks, const Type *type, bool isFuncResult)
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


Ident *identAllocParam(Idents *idents, const Types *types, const Modules *modules, const Blocks *blocks, const Signature *sig, int index)
{
    const int offset = typeParamOffset(types, sig, index);
    Ident *ident = identAddLocalVar(idents, modules, blocks, sig->param[index]->name, sig->param[index]->type, false, offset);
    identSetUsed(ident);                                                     // Do not warn about unused parameters
    return ident;
}


const char *identMethodNameWithRcv(const Idents *idents, const Ident *method)
{
    char typeBuf[DEFAULT_STR_LEN + 1];
    typeSpelling(method->type->sig.param[0]->type, typeBuf);

    char *buf = storageAdd(idents->storage, 2 * DEFAULT_STR_LEN + 2 + 1);
    snprintf(buf, 2 * DEFAULT_STR_LEN + 2 + 1, "(%s)%s", typeBuf, method->name);

    return buf;
}


void identWarnIfUnused(const Idents *idents, const Ident *ident)
{
    if (!ident->temporary && !ident->used)
    {
        idents->error->warningHandler(idents->error->context, &ident->debug, "%s %s is not used", (ident->kind == IDENT_MODULE ? "Module" : "Identifier"), ident->name);
        identSetUsed(ident);
    }
}


void identWarnIfUnusedAll(const Idents *idents, int block)
{
    for (const Ident *ident = idents->first; ident; ident = ident->next)
        if (ident->block == block)
            identWarnIfUnused(idents, ident);
}


bool identIsMain(const Ident *ident)
{
    return strcmp(ident->name, "main") == 0 && ident->kind == IDENT_CONST &&
           ident->type->kind == TYPE_FN && !ident->type->sig.isMethod && ident->type->sig.numParams == 1 && ident->type->sig.resultType->kind == TYPE_VOID;  // A dummy #upvalues is the only parameter
}
