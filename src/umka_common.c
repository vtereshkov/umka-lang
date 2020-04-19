#include <stdlib.h>

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


void blocksInit(Blocks *blocks, ErrorFunc error)
{
    blocks->numBlocks = 0;
    blocks->top = -1;
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

