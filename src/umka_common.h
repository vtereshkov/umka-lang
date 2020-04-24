#ifndef UMKA_COMMON_H_INCLUDED
#define UMKA_COMMON_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>


enum
{
    DEFAULT_STR_LEN     = 255,
    MAX_IDENT_LEN       = 255,
    MAX_MODULES         = 256,
    MAX_FIELDS          = 100,
    MAX_PARAMS          = 16,
    MAX_RESULTS         = 16,
    MAX_BLOCK_NESTING   = 100,
    MAX_GOTOS           = 100
};


typedef void (*ErrorFunc)(const char *format, ...);


typedef struct
{
    char *data;
    int capacity, len;
} Storage;


typedef struct
{
    char name[DEFAULT_STR_LEN + 1], path[DEFAULT_STR_LEN + 1];
    int hash, pathHash;
    bool imports[MAX_MODULES];
} Module;


typedef struct
{
    Module *module[MAX_MODULES];
    int numModules;
    ErrorFunc error;
} Modules;


typedef struct
{
    int block;
    struct tagIdent *fn;
    int localVarSize;       // For function blocks only
} BlockStackSlot;


typedef struct
{
    BlockStackSlot item[MAX_BLOCK_NESTING];
    int numBlocks, top;
    int module;
    ErrorFunc error;
} Blocks;


void storageInit(Storage *storage, int capacity);
void storageFree(Storage *storage);

void moduleInit         (Modules *modules, ErrorFunc error);
void moduleFree         (Modules *modules);
int  moduleFind         (Modules *modules, char *name);
int  moduleAssertFind   (Modules *modules, char *name);
int  moduleFindByPath   (Modules *modules, char *path);
void moduleNameFromPath (Modules *modules, char *path, char *name);
void moduleAdd          (Modules *modules, char *path);

void blocksInit (Blocks *blocks, ErrorFunc error);
void blocksFree (Blocks *blocks);
void blocksEnter(Blocks *blocks, struct tagIdent *fn);
void blocksLeave(Blocks *blocks);

int hash(const char *str);
int align(int size, int alignment);

#endif // UMKA_COMMON_H_INCLUDED
