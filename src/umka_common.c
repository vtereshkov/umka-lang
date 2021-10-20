#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef UMKA_EXT_LIBS
    #ifdef _WIN32
        #include <windows.h>
    #else
        #include <dlfcn.h>
    #endif
#endif

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

static void *moduleLoadImplLib(const char *path)
{
#ifdef UMKA_EXT_LIBS
    #ifdef _WIN32
        return LoadLibrary(path);
    #else
        return dlopen(path, RTLD_LOCAL | RTLD_LAZY);
    #endif
#endif
    return NULL;
}


static void moduleFreeImplLib(void *lib)
{
#ifdef UMKA_EXT_LIBS
    #ifdef _WIN32
        FreeLibrary(lib);
    #else
        dlclose(lib);
    #endif
#endif
}


static void *moduleLoadImplLibFunc(void *lib, const char *name)
{
#ifdef UMKA_EXT_LIBS
    #ifdef _WIN32
        return GetProcAddress(lib, name);
    #else
        return dlsym(lib, name);
    #endif
#endif
    return NULL;
}


void moduleInit(Modules *modules, Error *error)
{
    short i;
    for (i = 0; i < MAX_MODULES; i++)
    {
        modules->module[i] = NULL;
        modules->moduleSource[i] = NULL;
    }
    modules->numModules = 0;
    modules->numModuleSources = 0;
    modules->error = error;
}


void moduleFree(Modules *modules)
{
    {
        short i;
        for (i = 0; i < MAX_MODULES; i++) {
            if (modules->module[i])
            {
                if (modules->module[i]->implLib)
                    moduleFreeImplLib(modules->module[i]->implLib);
                free(modules->module[i]);
            }

            if (modules->moduleSource[i])
            {
                free(modules->moduleSource[i]->source);
                free(modules->moduleSource[i]);
            }
        }
    }
}


static void moduleNameFromPath(Modules *modules, const char *path, char *folder, char *name, int size)
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

    strncpy(folder, path, (start - path < size - 1) ? (start - path) : (size - 1));
    strncpy(name, start,  (stop - start < size - 1) ? (stop - start) : (size - 1));

    folder[size - 1] = 0;
    name[size - 1] = 0;
}


int moduleFind(Modules *modules, const char *name)
{
    unsigned int nameHash = hash(name);
    int i;
    for (i = 0; i < modules->numModules; i++)
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
    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name, DEFAULT_STR_LEN + 1);

    return moduleFind(modules, name);
}


int moduleAdd(Modules *modules, const char *path)
{
    if (modules->numModules >= MAX_MODULES)
        modules->error->handler(modules->error->context, "Too many modules");

    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name, DEFAULT_STR_LEN + 1);

    int res = moduleFind(modules, name);
    if (res >= 0)
        modules->error->handler(modules->error->context, "Duplicate module %s", name);

    Module *module = malloc(sizeof(Module));

    strncpy(module->path, path, DEFAULT_STR_LEN);
    module->path[DEFAULT_STR_LEN] = 0;

    strncpy(module->folder, folder, DEFAULT_STR_LEN);
    module->folder[DEFAULT_STR_LEN] = 0;

    strncpy(module->name, name, DEFAULT_STR_LEN);
    module->name[DEFAULT_STR_LEN] = 0;

    module->hash = hash(name);
    module->pathHash = hash(path);

    char libPath[2 + 2 * DEFAULT_STR_LEN + 4 + 1];
    sprintf(libPath, "./%s%s.umi", module->folder, module->name);

    module->implLib = moduleLoadImplLib(libPath);

    {
        short i;
        for (i = 0; i < MAX_MODULES; i++)
            module->imports[i] = false;
    }

    modules->module[modules->numModules] = module;
    return modules->numModules++;
}


char *moduleFindSource(Modules *modules, const char *name)
{
    unsigned int nameHash = hash(name);
    int i;
    for (i = 0; i < modules->numModuleSources; i++)
        if (modules->moduleSource[i]->hash == nameHash && strcmp(modules->moduleSource[i]->name, name) == 0)
            return modules->moduleSource[i]->source;
    return NULL;
}


char *moduleFindSourceByPath(Modules *modules, const char *path)
{
    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name, DEFAULT_STR_LEN + 1);

    return moduleFindSource(modules, name);
}


void moduleAddSource(Modules *modules, const char *path, const char *source)
{
    if (modules->numModuleSources >= MAX_MODULES)
        modules->error->handler(modules->error->context, "Too many module sources");

    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name, DEFAULT_STR_LEN + 1);

    ModuleSource *moduleSource = malloc(sizeof(ModuleSource));

    strncpy(moduleSource->name, name, DEFAULT_STR_LEN);
    moduleSource->name[DEFAULT_STR_LEN] = 0;

    int sourceLen = strlen(source);
    moduleSource->source = malloc(sourceLen + 1);
    strcpy(moduleSource->source, source);
    moduleSource->source[sourceLen] = 0;

    moduleSource->hash = hash(name);

    modules->moduleSource[modules->numModuleSources++] = moduleSource;
}


void *moduleGetImplLibFunc(Module *module, const char *name)
{
    if (module->implLib)
        return moduleLoadImplLibFunc(module->implLib, name);
    return NULL;
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

    External *external;
    for (external = externals->first; external; external = external->next)
        if (external->hash == nameHash && strcmp(external->name, name) == 0)
            return external;

    return NULL;
}


External *externalAdd(Externals *externals, const char *name, void *entry)
{
    External *external = malloc(sizeof(External));

    external->entry = entry;

    strncpy(external->name, name, DEFAULT_STR_LEN);
    external->name[DEFAULT_STR_LEN] = 0;

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
