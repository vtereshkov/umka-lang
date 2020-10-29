#ifndef UMKA_COMMON_H_INCLUDED
#define UMKA_COMMON_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>


enum
{
    DEFAULT_STR_LEN     = 255,
    MAX_IDENT_LEN       = 255,
    MAX_MODULES         = 256,
    MAX_FIELDS          = 100,
    MAX_PARAMS          = 16,
    MAX_BLOCK_NESTING   = 100,
    MAX_GOTOS           = 100
};


typedef struct
{
    int64_t len;
    int64_t itemSize;
    void *data;
} DynArray;


typedef struct
{
    void (*handler)(void *context, const char *format, ...);
    void (*handlerRuntime)(void *context, const char *format, ...);
    void *context;
    jmp_buf jumper;

    // Error report
    char fileName[DEFAULT_STR_LEN];
    int line, pos;
    char msg[DEFAULT_STR_LEN];
} Error;


typedef struct
{
    char *data;
    int capacity, len;
} Storage;


typedef struct
{
    char path[DEFAULT_STR_LEN + 1], folder[DEFAULT_STR_LEN + 1], name[DEFAULT_STR_LEN + 1];
    unsigned int hash, pathHash;
    bool imports[MAX_MODULES];
} Module;


typedef struct
{
    Module *module[MAX_MODULES];
    int numModules;
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
    struct tagExternal *next;
} External;


typedef struct
{
    External *first, *last;
} Externals;


typedef struct
{
    char *fileName;
    int line;
} DebugInfo;


void storageInit(Storage *storage, int capacity);
void storageFree(Storage *storage);

void moduleInit         (Modules *modules, Error *error);
void moduleFree         (Modules *modules);
int  moduleFind         (Modules *modules, const char *name);
int  moduleAssertFind   (Modules *modules, const char *name);
int  moduleFindByPath   (Modules *modules, const char *path);
int  moduleAdd          (Modules *modules, const char *path);

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


static inline int align(int size, int alignment)
{
    return ((size + (alignment - 1)) / alignment) * alignment;
}

#endif // UMKA_COMMON_H_INCLUDED
