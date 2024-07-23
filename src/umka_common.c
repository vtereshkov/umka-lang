#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <unistd.h>
#endif

#include "umka_common.h"
#include "umka_types.h"


// Errors

void errorReportInit(ErrorReport *report, const char *fileName, const char *fnName, int line, int pos, int code, const char *format, va_list args)
{
    errorReportFree(report);

    report->fileName = malloc(strlen(fileName) + 1);
    strcpy(report->fileName, fileName);

    report->fnName = malloc(strlen(fnName) + 1);
    strcpy(report->fnName, fnName);

    report->line = line;
    report->pos = pos;
    report->code = code;

    va_list argsCopy;
    va_copy(argsCopy, args);

    const int msgLen = vsnprintf(NULL, 0, format, args);
    report->msg = malloc(msgLen + 1);
    vsnprintf(report->msg, msgLen + 1, format, argsCopy);

    va_end(argsCopy);
}


void errorReportFree(ErrorReport *report)
{
    if (report->fileName)
    {
        free(report->fileName);
        report->fileName = NULL;
    }

    if (report->fnName)
    {
        free(report->fnName);
        report->fnName = NULL;
    }

    if (report->msg)
    {
        free(report->msg);
        report->msg = NULL;
    }
}


// Storage

void storageInit(Storage *storage)
{
    storage->first = storage->last = NULL;
}


void storageFree(Storage *storage)
{
    StorageChunk *chunk = storage->first;
    while (chunk)
    {
        StorageChunk *next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }
}


char *storageAdd(Storage *storage, int size)
{
    StorageChunk *chunk = malloc(sizeof(StorageChunk));

    chunk->data = malloc(size);
    memset(chunk->data, 0, size);
    chunk->next = NULL;

    // Add to list
    if (!storage->first)
        storage->first = storage->last = chunk;
    else
    {
        storage->last->next = chunk;
        storage->last = chunk;
    }
    return storage->last->data;
}


char *storageAddStr(Storage *storage, int len)
{
    StrDimensions dims = {.len = len, .capacity = len + 1};

    char *dimsAndData = storageAdd(storage, sizeof(StrDimensions) + dims.capacity);
    *(StrDimensions *)dimsAndData = dims;

    char *data = dimsAndData + sizeof(StrDimensions);
    data[len] = 0;

    return data;
}


DynArray *storageAddDynArray(Storage *storage, struct tagType *type, int len)
{
    DynArray *array = (DynArray *)storageAdd(storage, sizeof(DynArray));

    array->type     = type;
    array->itemSize = typeSizeNoCheck(array->type->base);

    DynArrayDimensions dims = {.len = len, .capacity = 2 * (len + 1)};

    char *dimsAndData = storageAdd(storage, sizeof(DynArrayDimensions) + dims.capacity * array->itemSize);
    *(DynArrayDimensions *)dimsAndData = dims;

    array->data = dimsAndData + sizeof(DynArrayDimensions);
    return array;
}


// Modules

static const char *moduleImplLibSuffix()
{
#ifdef UMKA_EXT_LIBS
    #ifdef _WIN32
        return "_windows";
    #elif defined __EMSCRIPTEN__
        return "_wasm";
    #else
        return "_linux";
    #endif
#endif
    return "";
}


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


void moduleInit(Modules *modules, bool implLibsEnabled, Error *error)
{
    for (int i = 0; i < MAX_MODULES; i++)
    {
        modules->module[i] = NULL;
        modules->moduleSource[i] = NULL;
    }
    modules->numModules = 0;
    modules->numModuleSources = 0;
    modules->implLibsEnabled = implLibsEnabled;
    modules->error = error;

    if (!moduleCurFolder(modules->curFolder, DEFAULT_STR_LEN + 1))
        modules->error->handler(modules->error->context, "Cannot get current folder");
}


void moduleFree(Modules *modules)
{
    for (int i = 0; i < modules->numModules; i++)
    {
        if (modules->module[i]->implLib)
            moduleFreeImplLib(modules->module[i]->implLib);

        for (int j = 0; j < modules->numModules; j++)
            if (modules->module[i]->importAlias[j])
                free(modules->module[i]->importAlias[j]);

        free(modules->module[i]);
    }

    for (int i = 0; i < modules->numModuleSources; i++)
    {
        free(modules->moduleSource[i]->source);
        free(modules->moduleSource[i]);
    }
}


void moduleNameFromPath(Modules *modules, const char *path, char *folder, char *name, int size)
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


int moduleFind(Modules *modules, const char *path)
{
    unsigned int pathHash = hash(path);
    for (int i = 0; i < modules->numModules; i++)
        if (modules->module[i]->pathHash == pathHash && strcmp(modules->module[i]->path, path) == 0)
            return i;
    return -1;
}


int moduleFindImported(Modules *modules, Blocks *blocks, const char *alias)
{
    for (int i = 0; i < modules->numModules; i++)
    {
        const char *importAlias = modules->module[blocks->module]->importAlias[i];
        if (importAlias && strcmp(importAlias, alias) == 0)
            return i;
    }
    return -1;
}


int moduleAdd(Modules *modules, const char *path)
{
    if (modules->numModules >= MAX_MODULES)
        modules->error->handler(modules->error->context, "Too many modules");

    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name, DEFAULT_STR_LEN + 1);

    for (int i = 0; name[i]; i++)
        if (name[i] == ' ' || name[i] == '\t')
            modules->error->handler(modules->error->context, "Module name cannot contain spaces or tabs");

    int res = moduleFind(modules, path);
    if (res >= 0)
        modules->error->handler(modules->error->context, "Duplicate module %s", path);

    Module *module = malloc(sizeof(Module));

    strncpy(module->path, path, DEFAULT_STR_LEN);
    module->path[DEFAULT_STR_LEN] = 0;

    strncpy(module->folder, folder, DEFAULT_STR_LEN);
    module->folder[DEFAULT_STR_LEN] = 0;

    strncpy(module->name, name, DEFAULT_STR_LEN);
    module->name[DEFAULT_STR_LEN] = 0;

    module->pathHash = hash(path);

    module->implLib = NULL;
    if (modules->implLibsEnabled)
    {
        char libPath[2 + 2 * DEFAULT_STR_LEN + 8 + 4 + 1];

        const char *pathPrefix = modulePathIsAbsolute(module->path) ? "" : "./";

        // First, search for an implementation library with an OS-specific suffix
        sprintf(libPath, "%s%s%s%s.umi", pathPrefix, module->folder, module->name, moduleImplLibSuffix());
        module->implLib = moduleLoadImplLib(libPath);

        // If not found, search for an implementation library without suffix
        if (!module->implLib)
        {
            sprintf(libPath, "%s%s%s.umi", pathPrefix, module->folder, module->name);
            module->implLib = moduleLoadImplLib(libPath);
        }
    }

    for (int i = 0; i < MAX_MODULES; i++)
        module->importAlias[i] = NULL;

    // Self-import
    module->importAlias[modules->numModules] = malloc(DEFAULT_STR_LEN + 1);
    strcpy(module->importAlias[modules->numModules], name);

    module->isCompiled = false;

    modules->module[modules->numModules] = module;
    return modules->numModules++;
}


char *moduleFindSource(Modules *modules, const char *path)
{
    unsigned int pathHash = hash(path);
    for (int i = 0; i < modules->numModuleSources; i++)
        if (modules->moduleSource[i]->pathHash == pathHash && strcmp(modules->moduleSource[i]->path, path) == 0)
            return modules->moduleSource[i]->source;
    return NULL;
}


void moduleAddSource(Modules *modules, const char *path, const char *source)
{
    if (modules->numModuleSources >= MAX_MODULES)
        modules->error->handler(modules->error->context, "Too many module sources");

    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name, DEFAULT_STR_LEN + 1);

    ModuleSource *moduleSource = malloc(sizeof(ModuleSource));

    strncpy(moduleSource->path, path, DEFAULT_STR_LEN);
    moduleSource->path[DEFAULT_STR_LEN] = 0;

    strncpy(moduleSource->folder, folder, DEFAULT_STR_LEN);
    moduleSource->folder[DEFAULT_STR_LEN] = 0;

    strncpy(moduleSource->name, name, DEFAULT_STR_LEN);
    moduleSource->name[DEFAULT_STR_LEN] = 0;

    int sourceLen = strlen(source);
    moduleSource->source = malloc(sourceLen + 1);
    strcpy(moduleSource->source, source);
    moduleSource->source[sourceLen] = 0;

    moduleSource->pathHash = hash(path);

    modules->moduleSource[modules->numModuleSources++] = moduleSource;
}


void *moduleGetImplLibFunc(Module *module, const char *name)
{
    if (module->implLib)
        return moduleLoadImplLibFunc(module->implLib, name);
    return NULL;
}


char *moduleCurFolder(char *buf, int size)
{
#ifdef _WIN32
    if (GetCurrentDirectory(size, buf) == 0)
        return NULL;
#else
    if (!getcwd(buf, size))
        return NULL;
#endif

    int len = strlen(buf);

    if (len > 0 && (buf[len - 1] == '/' || buf[len - 1] == '\\'))
        return buf;

    if (len > size - 2)
        return NULL;

    buf[len] = '/';
    buf[len + 1] = 0;
    return buf;
}


bool modulePathIsAbsolute(const char *path)
{
    while (path && (*path == ' ' || *path == '\t'))
        path++;

    if (!path)
        return false;

#ifdef _WIN32
    return isalpha(path[0]) && path[1] == ':';
#else
    return path[0] == '/';
#endif
}


bool moduleRegularizePath(const char *path, const char *curFolder, char *regularizedPath, int size)
{
    char *absolutePath = malloc(size);
    snprintf(absolutePath, size, "%s%s", modulePathIsAbsolute(path) ? "" : curFolder, path);

    char **separators = malloc(size * sizeof(char *));
    int numSeparators = 0;

    char *readCh = absolutePath, *writeCh = regularizedPath;
    int numDots = 0;

    while (*readCh)
    {
        switch (*readCh)
        {
            case '/':
            case '\\':
            {
                if (numDots == 1)   // "./" or ".\"
                {
                    numDots = 0;
                    break;
                }

                if (numDots == 2)   // "../" or "..\"
                {
                    if (numSeparators < 2)
                    {
                        free(separators);
                        free(absolutePath);
                        return false;
                    }

                    numSeparators--;
                    writeCh = separators[numSeparators - 1] + 1;

                    numDots = 0;
                    break;
                }

                separators[numSeparators++] = writeCh;
                *(writeCh++) = '/';

                numDots = 0;
                break;
            }

            case '.':
            {
                numDots++;
                break;
            }

            case ' ':
            case '\t':
            {
                numDots = 0;
                // fallthrough
            }

            default:
            {
                while (numDots > 0)
                {
                    *(writeCh++) = '.';
                    numDots--;
                }

                *(writeCh++) = *readCh;
                break;
            }
        }

        readCh++;
    }

    if (numDots > 0)
    {
        free(separators);
        free(absolutePath);
        return false;
    }

    *writeCh = 0;

    free(separators);
    free(absolutePath);
    return true;
}


void moduleAssertRegularizePath(Modules *modules, const char *path, const char *curFolder, char *regularizedPath, int size)
{
    if (!moduleRegularizePath(path, curFolder, regularizedPath, size))
        modules->error->handler(modules->error->context, "Invalid module path %s", path);
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


void blocksReenter(Blocks *blocks)
{
    blocks->top++;
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
    external->resolved = false;

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

