#ifndef UMKA_COMMON_H_INCLUDED
#define UMKA_COMMON_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>


enum
{
    DEFAULT_STR_LEN     = 255,
    MAX_IDENT_LEN       = DEFAULT_STR_LEN,
    MAX_MODULES         = 1024,
    MAX_FIELDS          = 256,
    MAX_PARAMS          = 16,
    MAX_BLOCK_NESTING   = 100,
    MAX_GOTOS           = 100,
};


enum
{
    MAP_NODE_FIELD_LEN   = 0,
    MAP_NODE_FIELD_KEY   = 1,
    MAP_NODE_FIELD_DATA  = 2,
    MAP_NODE_FIELD_LEFT  = 3,
    MAP_NODE_FIELD_RIGHT = 4,
};


typedef struct
{
    int64_t len, capacity;
} DynArrayDimensions;


typedef struct
{
    // Must have 8 byte alignment
    struct tagType *type;
    int64_t itemSize;           // Duplicates information contained in type, but useful for better performance
    void *data;                 // Allocated chunk should start at (char *)data - sizeof(DynArrayDimensions)
} DynArray;


typedef struct
{
    // The C equivalent of the Umka interface type
    void *self;
    struct tagType *selfType;
    // Methods are omitted - do not use sizeof()
} Interface;


typedef struct tagMapNode
{
    // The C equivalent of the Umka map base type
    int64_t len;            // Non-zero for the root node only
    void *key, *data;
    struct tagMapNode *left, *right;
} MapNode;


typedef struct
{
    // Must have 8 byte alignment
    struct tagType *type;
    MapNode *root;
} Map;


typedef void (*WarningCallback)(void * /*UmkaError*/ warning);


typedef struct
{
    void (*handler)(void *context, const char *format, ...);
    void (*runtimeHandler)(void *context, const char *format, ...);
    void (*warningHandler)(void *context, const char *format, ...);
    WarningCallback warningCallback;
    void *context;
    jmp_buf jumper;

    // Error report
    char fileName[DEFAULT_STR_LEN + 1];
    char fnName[DEFAULT_STR_LEN + 1];
    int line, pos;
    char msg[DEFAULT_STR_LEN + 1];
} Error;


typedef struct tagStorageChunk
{
    char *data;
    struct tagStorageChunk *next;
} StorageChunk;


typedef struct
{
    StorageChunk *first, *last;
} Storage;


typedef struct
{
    char path[DEFAULT_STR_LEN + 1], folder[DEFAULT_STR_LEN + 1], name[DEFAULT_STR_LEN + 1];
    unsigned int pathHash;
    void *implLib;
    char *importAlias[MAX_MODULES];
    bool importUsed[MAX_MODULES];
} Module;


typedef struct
{
    char path[DEFAULT_STR_LEN + 1], folder[DEFAULT_STR_LEN + 1], name[DEFAULT_STR_LEN + 1];
    unsigned int pathHash;
    char *source;
} ModuleSource;


typedef struct
{
    Module *module[MAX_MODULES];
    ModuleSource *moduleSource[MAX_MODULES];
    int numModules, numModuleSources;
    char curFolder[DEFAULT_STR_LEN + 1];
    bool implLibsEnabled;
    Error *error;
} Modules;


typedef struct
{
    int block;
    struct tagIdent *fn;
    int localVarSize;       // For function blocks only
    bool hasReturn;
} BlockStackSlot;


typedef struct
{
    BlockStackSlot item[MAX_BLOCK_NESTING];
    int numBlocks, top;
    int module;
    Error *error;
} Blocks;


typedef struct tagExternal
{
    char name[DEFAULT_STR_LEN + 1];
    unsigned int hash;
    void *entry;
    bool resolved;
    struct tagExternal *next;
} External;


typedef struct
{
    External *first, *last;
} Externals;


typedef struct
{
    char *fileName;
    char *fnName;
    int line;
} DebugInfo;


void  storageInit(Storage *storage);
void  storageFree(Storage *storage);
char *storageAdd (Storage *storage, int size);

void  moduleInit                (Modules *modules, bool implLibsEnabled, Error *error);
void  moduleFree                (Modules *modules);
void  moduleNameFromPath        (Modules *modules, const char *path, char *folder, char *name, int size);
int   moduleFind                (Modules *modules, const char *path);
int   moduleFindImported        (Modules *modules, Blocks *blocks, const char *name, bool markAsUsed);
void  moduleWarnIfUnusedImports (Modules *modules, int module);
int   moduleAdd                 (Modules *modules, const char *path);
char *moduleFindSource          (Modules *modules, const char *path);
void  moduleAddSource           (Modules *modules, const char *path, const char *source);
void *moduleGetImplLibFunc      (Module  *module,  const char *name);
char *moduleCurFolder           (char *buf, int size);
bool  modulePathIsAbsolute      (const char *path);
bool  moduleRegularizePath      (const char *path, const char *curFolder, char *regularizedPath, int size);
void  moduleAssertRegularizePath(Modules *modules, const char *path, const char *curFolder, char *regularizedPath, int size);

void blocksInit   (Blocks *blocks, Error *error);
void blocksFree   (Blocks *blocks);
void blocksEnter  (Blocks *blocks, struct tagIdent *fn);
void blocksLeave  (Blocks *blocks);
int  blocksCurrent(Blocks *blocks);

void externalInit       (Externals *externals);
void externalFree       (Externals *externals);
External *externalFind  (Externals *externals, const char *name);
External *externalAdd   (Externals *externals, const char *name, void *entry);


static inline unsigned int hash(const char *str)
{
    // djb2 hash
    unsigned int hash = 5381;
    char ch;

    while ((ch = *str++))
        hash = ((hash << 5) + hash) + ch;

    return hash;
}


static inline int64_t nonneg(int64_t size)
{
    return (size > 0) ? size : 0;
}


static inline int64_t align(int64_t size, int64_t alignment)
{
    return ((size + (alignment - 1)) / alignment) * alignment;
}


static inline bool getBit(const char *buf, int64_t bitPos)
{
    return (buf[bitPos / 8] >> (bitPos % 8)) & 1;
}


static inline DynArrayDimensions *getDims(const DynArray *array)
{
    return (DynArrayDimensions *)((char *)array->data - sizeof(DynArrayDimensions));
}


#endif // UMKA_COMMON_H_INCLUDED
