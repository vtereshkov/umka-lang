#ifndef UMKA_COMMON_H_INCLUDED
#define UMKA_COMMON_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>


enum
{
    DEFAULT_STR_LEN     = 255,
    MAX_IDENT_LEN       = DEFAULT_STR_LEN,
    MAX_IDENTS_IN_LIST  = 256,
    MAX_MODULES         = 1024,
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
} StrDimensions;


typedef StrDimensions DynArrayDimensions;


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
    // Methods are omitted - do not use sizeof() for non-empty interfaces
} Interface;


typedef struct
{
    // The C equivalent of the Umka closure type
    int64_t entryOffset;
    Interface upvalue;      // No methods - equivalent to "any"
} Closure;


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


typedef struct
{
    char *fileName;
    char *fnName;
    int line;
} DebugInfo;


typedef void (*WarningCallback)(void * /*UmkaError*/ warning);


typedef struct      // Must be identical to UmkaError
{
    char *fileName;
    char *fnName;
    int line, pos, code;
    char *msg;
} ErrorReport;


typedef struct
{
    void (*handler)(void *context, const char *format, ...);
    void (*runtimeHandler)(void *context, int code, const char *format, ...);
    void (*warningHandler)(void *context, DebugInfo *debug, const char *format, ...);
    WarningCallback warningCallback;
    void *context;
    jmp_buf jumper;
    int jumperNesting;
    ErrorReport report;
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
    bool isCompiled;
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
    int localVarSize;           // For function blocks only
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
    int64_t numParams;
    int64_t numResultParams;
    int64_t numParamSlots;
    int64_t firstSlotIndex[];
} ExternalCallParamLayout;


void errorReportInit(ErrorReport *report, const char *fileName, const char *fnName, int line, int pos, int code, const char *format, va_list args);
void errorReportFree(ErrorReport *report);

void  storageInit               (Storage *storage);
void  storageFree               (Storage *storage);
char *storageAdd                (Storage *storage, int size);
char *storageAddStr             (Storage *storage, int len);
DynArray *storageAddDynArray    (Storage *storage, struct tagType *type, int len);

void  moduleInit                (Modules *modules, bool implLibsEnabled, Error *error);
void  moduleFree                (Modules *modules);
void  moduleNameFromPath        (Modules *modules, const char *path, char *folder, char *name, int size);
int   moduleFind                (Modules *modules, const char *path);
int   moduleFindImported        (Modules *modules, Blocks *blocks, const char *name);
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
void blocksReenter(Blocks *blocks);
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


static inline StrDimensions *getStrDims(const char *str)
{
    return (StrDimensions *)(str - sizeof(StrDimensions));
}


static inline DynArrayDimensions *getDims(const DynArray *array)
{
    return (DynArrayDimensions *)((char *)array->data - sizeof(DynArrayDimensions));
}


#endif // UMKA_COMMON_H_INCLUDED
