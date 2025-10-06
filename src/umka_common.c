#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #ifdef UMKA_EXT_LIBS
        #include <dlfcn.h>
    #endif
    #include <unistd.h>
#endif

#include "umka_common.h"
#include "umka_types.h"


// Errors

void errorReportInit(UmkaError *report, Storage *storage, const char *fileName, const char *fnName, int line, int pos, int code, const char *format, va_list args)
{
    char *reportFileName = storageAdd(storage, strlen(fileName) + 1);
    strcpy(reportFileName, fileName);
    report->fileName = reportFileName;

    char *reportFnName = storageAdd(storage, strlen(fnName) + 1);
    strcpy(reportFnName, fnName);
    report->fnName = reportFnName;

    report->line = line;
    report->pos = pos;
    report->code = code;

    va_list argsCopy;
    va_copy(argsCopy, args);

    const int msgLen = vsnprintf(NULL, 0, format, args);
    char *reportMsg = storageAdd(storage, msgLen + 1);
    vsnprintf(reportMsg, msgLen + 1, format, argsCopy);
    report->msg = reportMsg;

    va_end(argsCopy);
}


// Storage

void storageInit(Storage *storage, Error *error)
{
    storage->first = NULL;
    storage->error = error;
}


void storageFree(Storage *storage)
{
    while (storage->first)
    {
        StorageChunk *next = storage->first->next;
        free(storage->first);
        storage->first = next;
    }
}


void *storageAdd(Storage *storage, int64_t size)
{
    StorageChunk *chunk = malloc(sizeof(StorageChunk) + size);
    if (!chunk)
        storage->error->handler(storage->error->context, "Out of memory");

    chunk->prev = NULL;
    chunk->next = storage->first;
    chunk->size = size;
    memset(chunk->data, 0, size);

    if (storage->first)
        storage->first->prev = chunk;
    storage->first = chunk;

    return chunk->data;
}


char *storageAddStr(Storage *storage, int64_t len)
{
    StrDimensions dims = {.len = len, .capacity = len + 1};

    char *dimsAndData = storageAdd(storage, sizeof(StrDimensions) + dims.capacity);
    *(StrDimensions *)dimsAndData = dims;

    char *data = dimsAndData + sizeof(StrDimensions);
    data[len] = 0;

    return data;
}


DynArray *storageAddDynArray(Storage *storage, const struct tagType *type, int64_t len)
{
    DynArray *array = storageAdd(storage, sizeof(DynArray));

    array->type     = type;
    array->itemSize = array->type->base->size;

    DynArrayDimensions dims = {.len = len, .capacity = 2 * (len + 1)};

    char *dimsAndData = storageAdd(storage, sizeof(DynArrayDimensions) + dims.capacity * array->itemSize);
    *(DynArrayDimensions *)dimsAndData = dims;

    array->data = dimsAndData + sizeof(DynArrayDimensions);
    return array;
}


void storageRemove(Storage *storage, void *data)
{
    StorageChunk *chunk = (StorageChunk *)((char *)data - sizeof(StorageChunk));

    if (chunk == storage->first)
        storage->first = chunk->next;

    if (chunk->prev)
        chunk->prev->next = chunk->next;

    if (chunk->next)
        chunk->next->prev = chunk->prev;

    free(chunk);
}


void *storageRealloc(Storage *storage, void *data, int64_t size)
{
    const StorageChunk *chunk = (const StorageChunk *)((char *)data - sizeof(StorageChunk));

    void *newData = storageAdd(storage, size);
    memcpy(newData, data, chunk->size);
    storageRemove(storage, data);

    return newData;
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
#else
    return "";
#endif
}


static void *moduleLoadImplLib(const char *path)
{
#ifdef UMKA_EXT_LIBS
    #ifdef _WIN32
        return LoadLibrary(path);
    #else
        return dlopen(path, RTLD_LOCAL | RTLD_LAZY);
    #endif
#else
    return NULL;
#endif
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
#else
    return NULL;
#endif
}


void moduleInit(Modules *modules, Storage *storage, bool implLibsEnabled, Error *error)
{
    for (int i = 0; i < MAX_MODULES; i++)
    {
        modules->module[i] = NULL;
        modules->moduleSource[i] = NULL;
    }
    modules->numModules = 0;
    modules->numModuleSources = 0;
    modules->implLibsEnabled = implLibsEnabled;
    modules->storage = storage;
    modules->error = error;

    if (!moduleCurFolder(modules->curFolder, DEFAULT_STR_LEN + 1))
        modules->error->handler(modules->error->context, "Cannot get current folder");
}


void moduleFree(Modules *modules)
{
    for (int i = 0; i < modules->numModules; i++)
        if (modules->module[i]->implLib)
            moduleFreeImplLib(modules->module[i]->implLib);
}


void moduleNameFromPath(const Modules *modules, const char *path, char *folder, char *name, int size)
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


int moduleFind(const Modules *modules, const char *path)
{
    const unsigned int pathHash = hash(path);
    for (int i = 0; i < modules->numModules; i++)
        if (modules->module[i]->pathHash == pathHash && strcmp(modules->module[i]->path, path) == 0)
            return i;
    return -1;
}


int moduleFindImported(const Modules *modules, const Blocks *blocks, const char *alias)
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

    Module *module = storageAdd(modules->storage, sizeof(Module));

    strncpy(module->path, path, DEFAULT_STR_LEN);
    module->path[DEFAULT_STR_LEN] = 0;

    strncpy(module->folder, folder, DEFAULT_STR_LEN);
    module->folder[DEFAULT_STR_LEN] = 0;

    strncpy(module->name, name, DEFAULT_STR_LEN);
    module->name[DEFAULT_STR_LEN] = 0;

    module->pathHash = hash(path);

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

    // Self-import
    module->importAlias[modules->numModules] = storageAdd(modules->storage, DEFAULT_STR_LEN + 1);
    strcpy(module->importAlias[modules->numModules], name);

    module->isCompiled = false;

    modules->module[modules->numModules] = module;
    return modules->numModules++;
}


const ModuleSource *moduleFindSource(const Modules *modules, const char *path)
{
    const unsigned int pathHash = hash(path);
    for (int i = 0; i < modules->numModuleSources; i++)
        if (modules->moduleSource[i]->pathHash == pathHash && strcmp(modules->moduleSource[i]->path, path) == 0)
            return modules->moduleSource[i];
    return NULL;
}


void moduleAddSource(Modules *modules, const char *path, const char *source, bool trusted)
{
    if (modules->numModuleSources >= MAX_MODULES)
        modules->error->handler(modules->error->context, "Too many module sources");

    char folder[DEFAULT_STR_LEN + 1] = "";
    char name  [DEFAULT_STR_LEN + 1] = "";

    moduleNameFromPath(modules, path, folder, name, DEFAULT_STR_LEN + 1);

    ModuleSource *moduleSource = storageAdd(modules->storage, sizeof(ModuleSource));

    strncpy(moduleSource->path, path, DEFAULT_STR_LEN);
    moduleSource->path[DEFAULT_STR_LEN] = 0;

    strncpy(moduleSource->folder, folder, DEFAULT_STR_LEN);
    moduleSource->folder[DEFAULT_STR_LEN] = 0;

    strncpy(moduleSource->name, name, DEFAULT_STR_LEN);
    moduleSource->name[DEFAULT_STR_LEN] = 0;

    int sourceLen = strlen(source);
    moduleSource->source = storageAdd(modules->storage, sourceLen + 1);
    strcpy(moduleSource->source, source);

    moduleSource->pathHash = hash(path);
    moduleSource->trusted = trusted;

    modules->moduleSource[modules->numModuleSources++] = moduleSource;
}


void *moduleGetImplLibFunc(const Module *module, const char *name)
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


bool moduleRegularizePath(const Modules *modules, const char *path, const char *curFolder, char *regularizedPath, int size)
{
    char *absolutePath = storageAdd(modules->storage, size);
    snprintf(absolutePath, size, "%s%s", modulePathIsAbsolute(path) ? "" : curFolder, path);

    char **separators = storageAdd(modules->storage, size * sizeof(char *));
    int numSeparators = 0;

    const char *readCh = absolutePath;
    char *writeCh = regularizedPath;
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
                        return false;

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
        return false;

    *writeCh = 0;
    return true;
}


void moduleAssertRegularizePath(const Modules *modules, const char *path, const char *curFolder, char *regularizedPath, int size)
{
    if (!moduleRegularizePath(modules, path, curFolder, regularizedPath, size))
        modules->error->handler(modules->error->context, "Invalid module path %s", path);
}


// Blocks

void blocksInit(Blocks *blocks, Error *error)
{
    blocks->numBlocks = 0;
    blocks->top = -1;
    blocks->module = -1;
    blocks->error = error;

    blocksEnter(blocks);
}


void blocksEnterFn(Blocks *blocks, const struct tagIdent *fn, bool hasUpvalues)
{
    if (blocks->top >= MAX_BLOCK_NESTING)
        blocks->error->handler(blocks->error->context, "Block nesting is too deep");

    blocks->top++;
    blocks->item[blocks->top].block = blocks->numBlocks++;
    blocks->item[blocks->top].fn = fn;
    blocks->item[blocks->top].localVarSize = 0;
    blocks->item[blocks->top].hasReturn = false;
    blocks->item[blocks->top].hasUpvalues = hasUpvalues;
}


void blocksEnter(Blocks *blocks)
{
    blocksEnterFn(blocks, NULL, false);
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


int blocksCurrent(const Blocks *blocks)
{
    return blocks->item[blocks->top].block;
}


// Externals

void externalInit(Externals *externals, Storage *storage)
{
    externals->first = NULL;
    externals->storage = storage;
}


External *externalFind(const Externals *externals, const char *name)
{
    const unsigned int nameHash = hash(name);

    for (External *external = externals->first; external; external = external->next)
        if (external->hash == nameHash && strcmp(external->name, name) == 0)
            return external;

    return NULL;
}


External *externalAdd(Externals *externals, const char *name, void *entry, bool resolveInTrusted)
{
    External *external = storageAdd(externals->storage, sizeof(External));

    external->entry = entry;
    external->resolved = false;
    external->resolveInTrusted = resolveInTrusted;

    strncpy(external->name, name, DEFAULT_STR_LEN);
    external->name[DEFAULT_STR_LEN] = 0;

    external->hash = hash(name);

    external->next = externals->first;
    externals->first = external;

    return external;
}

