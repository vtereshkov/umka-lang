#ifndef UMKA_COMMON_H_INCLUDED
#define UMKA_COMMON_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>


enum
{
    MAX_IDENT_LEN       = 255,
    DEFAULT_STR_LEN     = 255,
    MAX_FIELDS          = 100,
    MAX_PARAMS          = 16,
    MAX_RESULTS         = 16,
    MAX_BLOCK_NESTING   = 100,
    MAX_GOTOS           = 100
};


typedef void (*ErrorFunc)(const char *format, ...);


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
    ErrorFunc error;
} Blocks;


typedef struct
{
    char *data;
    int capacity, len;
} Storage;


void storageInit(Storage *storage, int capacity);
void storageFree(Storage *storage);

void blocksInit(Blocks *blocks, ErrorFunc error);
void blocksFree(Blocks *blocks);
void blocksEnter(Blocks *blocks, struct tagIdent *fn);
void blocksLeave(Blocks *blocks);

int hash(const char *str);
int align(int size, int alignment);

#endif // UMKA_COMMON_H_INCLUDED
