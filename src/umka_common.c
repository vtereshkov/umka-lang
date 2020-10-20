#include <stdlib.h>
#include <string.h>

#include "umka_common.h"


// Storage

void storageInit(Storage *storage, int capacity)
{
    storage->data = malloc(capacity);
    storage->capacity = capacity;
    storage->len = 0;
}


void storageFree(Storage *storage)
{
    free(storage->data);
}


// Modules

void moduleInit(Modules *modules, Error *error)
{
    for (int i = 0; i < MAX_MODULES; i++)
        modules->module[i] = NULL;
    modules->numModules = 0;
    modules->error = error;
}


void moduleFree(Modules *modules)
{
    for (int i = 0; i < MAX_MODULES; i++)
        if (modules->module[i])
            free(modules->module[i]);
}


int moduleFind(Modules *modules, const char *name)
{
    unsigned int nameHash = hash(name);
    for (int i = 0; i < modules->numModules; i++)
        if (modules->module[i]->hash == nameHash && strcmp(modules->module[i]->name, name) == 0)
            return i;
    return -1;
}


int moduleAssertFind(Modules *modules, const char *name)
{
    int res = moduleFind(modules, name);
    if (res < 0)
        modules->error->handler(modules->error->context, "Unknown module %s", name);
    return res;
}


int moduleFindByPath(Modules *modules, const char *path)
{
    unsigned int pathHash = hash(path);
    for (int i = 0; i < modules->numModules; i++)
        if (modules->module[i]->pathHash == pathHash && strcmp(modules->module[i]->path, path) == 0)
            return i;
    return -1;
}


static void moduleNameFromPath(Modules *modules, const char *path, char *folder, char *name)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');

    if (backslash && (!slash || backslash > slash))
        slash = backslash;

    const char *start = slash ? (slash + 1) : path;

    const char *dot = strrchr(path, '.');
    const char *stop = dot ? dot : (path + strlen(path));

    if (stop <= start)
        modules->error->handler(modules->error->context, "Illegal module path %s", path);

    strncpy(folder, path, start - path);
    strncpy(name, start, stop - start);
}


int moduleAdd(Modules *modules, const char *path)
{
    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name);

    int res = moduleFind(modules, name);
    if (res >= 0)
        modules->error->handler(modules->error->context, "Duplicate module %s", name);

    Module *module = malloc(sizeof(Module));

    strcpy(module->path, path);
    strcpy(module->folder, folder);
    strcpy(module->name, name);

    module->hash = hash(name);
    module->pathHash = hash(path);

    for (int i = 0; i < MAX_MODULES; i++)
        module->imports[i] = false;

    modules->module[modules->numModules] = module;
    return modules->numModules++;
}


// Blocks

void blocksInit(Blocks *blocks, Error *error)
{
    blocks->numBlocks = 0;
    blocks->top = -1;
    blocks->module = -1;
    blocks->error = error;

    blocksEnter(blocks, false);
}


void blocksFree(Blocks *blocks)
{
}


void blocksEnter(Blocks *blocks, struct tagIdent *fn)
{
    if (blocks->top >= MAX_BLOCK_NESTING)
        blocks->error->handler(blocks->error->context, "Block nesting is too deep");

    blocks->top++;
    blocks->item[blocks->top].block = blocks->numBlocks++;
    blocks->item[blocks->top].fn = fn;
    blocks->item[blocks->top].localVarSize = 0;
    blocks->item[blocks->top].hasReturn = false;
}


void blocksLeave(Blocks *blocks)
{
    if (blocks->top <= 0)
        blocks->error->handler(blocks->error->context, "No block to leave");
    blocks->top--;
}


int blocksCurrent(Blocks *blocks)
{
    return blocks->item[blocks->top].block;
}


// Externals

void externalInit(Externals *externals)
{
    externals->first = externals->last = NULL;
}


void externalFree(Externals *externals)
{
    External *external = externals->first;
    while (external)
    {
        External *next = external->next;
        free(external);
        external = next;
    }
}


External *externalFind(Externals *externals, const char *name)
{
    unsigned int nameHash = hash(name);

    for (External *external = externals->first; external; external = external->next)
        if (external->hash == nameHash && strcmp(external->name, name) == 0)
            return external;

    return NULL;
}


External *externalAdd(Externals *externals, const char *name, void *entry)
{
    External *external = malloc(sizeof(External));

    external->entry = entry;

    strcpy(external->name, name);
    external->hash = hash(name);

    external->next = NULL;

    // Add to list
    if (!externals->first)
        externals->first = externals->last = external;
    else
    {
        externals->last->next = external;
        externals->last = external;
    }
    return externals->last;
}

