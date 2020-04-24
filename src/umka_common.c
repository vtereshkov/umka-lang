#include <stdlib.h>
#include <string.h>

#include "umka_common.h"


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


void moduleInit(Modules *modules, ErrorFunc error)
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


int moduleFind(Modules *modules, char *name)
{
    int nameHash = hash(name);
    for (int i = 0; i < modules->numModules; i++)
        if (modules->module[i]->hash == nameHash && strcmp(modules->module[i]->name, name) == 0)
            return i;
    return -1;
}


int moduleAssertFind(Modules *modules, char *name)
{
    int res = moduleFind(modules, name);
    if (res < 0)
        modules->error("Unknown module %s", name);
    return res;
}


int moduleFindByPath(Modules *modules, char *path)
{
    int pathHash = hash(path);
    for (int i = 0; i < modules->numModules; i++)
        if (modules->module[i]->pathHash == pathHash && strcmp(modules->module[i]->path, path) == 0)
            return i;
    return -1;
}


void moduleNameFromPath(Modules *modules, char *path, char *name)
{
    char *slash = strrchr(path, '/');
    char *start = slash ? (slash + 1) : path;

    char *dot = strrchr(path, '.');
    char *stop = dot ? dot : (path + strlen(path));

    if (stop <= start)
        modules->error("Illegal module path %s", path);

    strncpy(name, start, stop - start);
}


void moduleAdd(Modules *modules, char *path)
{
    char name[DEFAULT_STR_LEN + 1];
    moduleNameFromPath(modules, path, name);

    int res = moduleFind(modules, name);
    if (res >= 0)
        modules->error("Duplicate module %s", name);

    Module *module = malloc(sizeof(Module));

    strcpy(module->name, name);
    strcpy(module->path, path);

    module->hash = hash(name);
    module->pathHash = hash(path);

    for (int i = 0; i < MAX_MODULES; i++)
        module->imports[i] = false;

    modules->module[modules->numModules++] = module;
}


void blocksInit(Blocks *blocks, ErrorFunc error)
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
        blocks->error("Block nesting is too deep");

    blocks->top++;
    blocks->item[blocks->top].block = blocks->numBlocks++;
    blocks->item[blocks->top].fn = fn;
    blocks->item[blocks->top].localVarSize = 0;
}


void blocksLeave(Blocks *blocks)
{
    if (blocks->top <= 0)
        blocks->error("No block to leave");
    blocks->top--;
}


// djb2 hash
int hash(const char *str)
{
    int hash = 5381;
    char ch;

    while ((ch = *str++))
        hash = ((hash << 5) + hash) + ch;

    return hash;
}


int align(int size, int alignment)
{
    return ((size + (alignment - 1)) / alignment) * alignment;
}

