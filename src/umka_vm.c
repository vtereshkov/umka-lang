#define __USE_MINGW_ANSI_STDIO 1

#ifdef _MSC_VER  // MSVC++ only
    #define FORCE_INLINE __forceinline
#else
    #define FORCE_INLINE __attribute__((always_inline)) inline
#endif

//#define UMKA_STR_DEBUG
//#define UMKA_REF_CNT_DEBUG
//#define UMKA_DETAILED_LEAK_INFO


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

#include "umka_vm.h"


/*
Virtual machine stack layout (64-bit slots):

0   ...
    ...                                              <- Stack origin
    ...
    Temporary value 1                                <- Stack top
    Temporary value 0
    ...
    Local variable 1
    ...
    Local variable 1
    Local variable 0
    ...
    Local variable 0
    Parameter layout table pointer
    Stack frame ref count
    Caller's stack frame base pointer                <- Stack frame base pointer
    Return address
    Parameter N
    ...
    Parameter N
    Parameter N - 1
    ...
    Parameter N - 1
    ...                                              <- Stack origin + size
*/


static const char *opcodeSpelling [] =
{
    "NOP",
    "PUSH",
    "PUSH_ZERO",
    "PUSH_LOCAL_PTR",
    "PUSH_LOCAL_PTR_ZERO",
    "PUSH_LOCAL",
    "PUSH_REG",
    "PUSH_UPVALUE",
    "POP",
    "POP_REG",
    "DUP",
    "SWAP",
    "ZERO",
    "DEREF",
    "ASSIGN",
    "ASSIGN_PARAM",
    "CHANGE_REF_CNT",
    "CHANGE_REF_CNT_GLOBAL",
    "CHANGE_REF_CNT_LOCAL",
    "CHANGE_REF_CNT_ASSIGN",
    "UNARY",
    "BINARY",
    "GET_ARRAY_PTR",
    "GET_DYNARRAY_PTR",
    "GET_MAP_PTR",
    "GET_FIELD_PTR",
    "ASSERT_TYPE",
    "ASSERT_RANGE",
    "WEAKEN_PTR",
    "STRENGTHEN_PTR",
    "GOTO",
    "GOTO_IF",
    "GOTO_IF_NOT",
    "CALL",
    "CALL_INDIRECT",
    "CALL_EXTERN",
    "CALL_BUILTIN",
    "RETURN",
    "ENTER_FRAME",
    "LEAVE_FRAME",
    "HALT"
};


static const char *builtinSpelling [] =
{
    "printf",
    "fprintf",
    "sprintf",
    "scanf",
    "fscanf",
    "sscanf",
    "real",
    "real_lhs",
    "round",
    "trunc",
    "ceil",
    "floor",
    "fabs",
    "sqrt",
    "sin",
    "cos",
    "atan",
    "atan2",
    "exp",
    "log",
    "new",
    "make",
    "makefromarr",
    "makefromstr",
    "maketoarr",
    "maketostr",
    "copy",
    "append",
    "insert",
    "delete",
    "slice",
    "sort",
    "sortfast",
    "len",
    "cap",
    "sizeof",
    "sizeofself",
    "selfhasptr",
    "selftypeeq",
    "typeptr",
    "valid",
    "validkey",
    "keys",
    "resume",
    "memusage",
    "exit"
};


// Memory management

static void pageInit(HeapPages *pages, Fiber *fiber, Error *error)
{
    pages->first = pages->last = NULL;
    pages->freeId = 1;
    pages->totalSize = 0;
    pages->fiber = fiber;
    pages->error = error;
}


static void pageFree(HeapPages *pages, bool warnLeak)
{
    HeapPage *page = pages->first;
    while (page)
    {
        HeapPage *next = page->next;
        if (page->ptr)
        {
            // Report memory leaks
            if (warnLeak)
            {
                fprintf(stderr, "Warning: Memory leak at %p (%d refs)\n", page->ptr, page->refCnt);

#ifdef UMKA_DETAILED_LEAK_INFO
                for (int i = 0; i < page->numOccupiedChunks; i++)
                {
                    HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)page->ptr + i * page->chunkSize);
                    if (chunk->refCnt == 0)
                        continue;

                    DebugInfo *debug = &pages->fiber->debugPerInstr[chunk->ip];
                    fprintf(stderr, "    Chunk allocated in %s: %s (%d)\n", debug->fnName, debug->fileName, debug->line);
                }
 #endif
            }

            // Call custom deallocators, if any
            for (int i = 0; i < page->numOccupiedChunks && page->numChunksWithOnFree > 0; i++)
            {
                HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)page->ptr + i * page->chunkSize);
                if (chunk->refCnt == 0 || !chunk->onFree)
                    continue;

                Slot param = {.ptrVal = (char *)chunk + sizeof(HeapChunkHeader)};
                chunk->onFree(&param, NULL);
                page->numChunksWithOnFree--;
            }

            free(page->ptr);
        }
        free(page);
        page = next;
    }
}


static FORCE_INLINE HeapPage *pageAdd(HeapPages *pages, int numChunks, int chunkSize)
{
    HeapPage *page = malloc(sizeof(HeapPage));

    page->id = pages->freeId++;

    const int size = numChunks * chunkSize;
    page->ptr = malloc(size);
    if (!page->ptr)
        return NULL;

    page->numChunks = numChunks;
    page->numOccupiedChunks = 0;
    page->numChunksWithOnFree = 0;
    page->chunkSize = chunkSize;
    page->refCnt = 0;
    page->prev = pages->last;
    page->next = NULL;

    // Add to list
    if (!pages->first)
        pages->first = pages->last = page;
    else
    {
        pages->last->next = page;
        pages->last = page;
    }

    pages->totalSize += page->numChunks * page->chunkSize;

#ifdef UMKA_REF_CNT_DEBUG
    printf("Add page at %p\n", page->ptr);
#endif

    return pages->last;
}


static FORCE_INLINE void pageRemove(HeapPages *pages, HeapPage *page)
{
#ifdef UMKA_REF_CNT_DEBUG
    printf("Remove page at %p\n", page->ptr);
#endif

    pages->totalSize -= page->numChunks * page->chunkSize;

    if (page == pages->first)
        pages->first = page->next;

    if (page == pages->last)
        pages->last = page->prev;

    if (page->prev)
        page->prev->next = page->next;

    if (page->next)
        page->next->prev = page->prev;

    free(page->ptr);
    free(page);
}


static FORCE_INLINE HeapChunkHeader *pageGetChunkHeader(HeapPage *page, void *ptr)
{
    const int chunkOffset = ((char *)ptr - (char *)page->ptr) % page->chunkSize;
    return (HeapChunkHeader *)((char *)ptr - chunkOffset);
}


static FORCE_INLINE HeapPage *pageFind(HeapPages *pages, void *ptr, bool warnDangling)
{
    for (HeapPage *page = pages->first; page; page = page->next)
        if (ptr >= page->ptr && ptr < (void *)((char *)page->ptr + page->numChunks * page->chunkSize))
        {
            HeapChunkHeader *chunk = pageGetChunkHeader(page, ptr);

            if (warnDangling && chunk->refCnt == 0)
                pages->error->runtimeHandler(pages->error->context, VM_RUNTIME_ERROR, "Dangling pointer at %p", ptr);

            if (chunk->refCnt > 0)
                return page;
            return NULL;
        }
    return NULL;
}


static FORCE_INLINE HeapPage *pageFindForAlloc(HeapPages *pages, int size)
{
    HeapPage *bestPage = NULL;
    int bestSize = 1 << 30;

    for (HeapPage *page = pages->first; page; page = page->next)
        if (page->numOccupiedChunks < page->numChunks && page->chunkSize >= size && page->chunkSize < bestSize)
        {
            bestPage = page;
            bestSize = page->chunkSize;
        }
    return bestPage;
}


static FORCE_INLINE HeapPage *pageFindById(HeapPages *pages, int id)
{
    for (HeapPage *page = pages->first; page; page = page->next)
        if (page->id == id)
            return page;
    return NULL;
}


static FORCE_INLINE bool stackUnwind(Fiber *fiber, Slot **base, int *ip)
{
    if (*base == fiber->stack + fiber->stackSize - 1)
        return false;

    int returnOffset = (*base + 1)->intVal;
    if (returnOffset == VM_RETURN_FROM_FIBER || returnOffset == VM_RETURN_FROM_VM)
        return false;

    *base = (Slot *)((*base)->ptrVal);
    if (ip)
        *ip = returnOffset;
    return true;
}


static FORCE_INLINE void stackChangeFrameRefCnt(Fiber *fiber, HeapPages *pages, void *ptr, int delta)
{
    if (ptr >= (void *)fiber->top && ptr < (void *)(fiber->stack + fiber->stackSize))
    {
        Slot *base = fiber->base;
        while (ptr > (void *)base)
        {
            if (!stackUnwind(fiber, &base, NULL))
                pages->error->runtimeHandler(pages->error->context, VM_RUNTIME_ERROR, "Illegal stack pointer");
        }

        int64_t *stackFrameRefCnt = &(base - 1)->intVal;
        *stackFrameRefCnt += delta;
    }
}


static FORCE_INLINE void *chunkAlloc(HeapPages *pages, int64_t size, Type *type, ExternFunc onFree, bool isStack, Error *error)
{
    // Page layout: header, data, footer (char), padding, header, data, footer (char), padding...
    int64_t chunkSize = align(sizeof(HeapChunkHeader) + align(size + 1, sizeof(int64_t)), VM_MIN_HEAP_CHUNK);

    if (size < 0 || chunkSize > INT_MAX)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal block size");

    HeapPage *page = pageFindForAlloc(pages, chunkSize);
    if (!page)
    {
        int numChunks = VM_MIN_HEAP_PAGE / chunkSize;
        if (numChunks == 0)
            numChunks = 1;

        page = pageAdd(pages, numChunks, chunkSize);
        if (!page)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Out of memory");
    }

    HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)page->ptr + page->numOccupiedChunks * page->chunkSize);

    memset(chunk, 0, page->chunkSize);
    chunk->refCnt = 1;
    chunk->size = size;
    chunk->type = type;
    chunk->onFree = onFree;
    chunk->ip = pages->fiber->ip;
    chunk->isStack = isStack;

    page->numOccupiedChunks++;
    if (onFree)
        page->numChunksWithOnFree++;

    page->refCnt++;

#ifdef UMKA_REF_CNT_DEBUG
    printf("Add chunk at %p\n", (void *)chunk + sizeof(HeapChunkHeader));
#endif

    return (char *)chunk + sizeof(HeapChunkHeader);
}


static FORCE_INLINE int chunkChangeRefCnt(HeapPages *pages, HeapPage *page, void *ptr, int delta)
{
    HeapChunkHeader *chunk = pageGetChunkHeader(page, ptr);

    if (chunk->refCnt <= 0 || page->refCnt < chunk->refCnt)
        pages->error->runtimeHandler(pages->error->context, VM_RUNTIME_ERROR, "Wrong reference count for pointer at %p", ptr);

    if (chunk->onFree && chunk->refCnt == 1 && delta == -1)
    {
        Slot param = {.ptrVal = ptr};
        chunk->onFree(&param, NULL);
        page->numChunksWithOnFree--;
    }

    chunk->refCnt += delta;
    page->refCnt += delta;

    // Additional ref counts for a user-defined address interval (used for stack frames to detect escaping refs)
    stackChangeFrameRefCnt(pages->fiber, pages, ptr, delta);

#ifdef UMKA_REF_CNT_DEBUG
    printf("%p: delta: %d  chunk: %d  page: %d\n", ptr, delta, chunk->refCnt, page->refCnt);
#endif

    if (page->refCnt == 0)
    {
        pageRemove(pages, page);
        return 0;
    }

    return chunk->refCnt;
}


static FORCE_INLINE void candidateInit(RefCntChangeCandidates *candidates)
{
    candidates->capacity = 100;
    candidates->stack = malloc(candidates->capacity * sizeof(RefCntChangeCandidate));
    candidates->top = -1;
}


static FORCE_INLINE void candidateFree(RefCntChangeCandidates *candidates)
{
    free(candidates->stack);
}


static FORCE_INLINE void candidateReset(RefCntChangeCandidates *candidates)
{
    candidates->top = -1;
}


static FORCE_INLINE void candidatePush(RefCntChangeCandidates *candidates, void *ptr, Type *type)
{
    if (candidates->top >= candidates->capacity - 1)
    {
        candidates->capacity *= 2;
        candidates->stack = realloc(candidates->stack, candidates->capacity * sizeof(RefCntChangeCandidate));
    }

    RefCntChangeCandidate *candidate = &candidates->stack[++candidates->top];
    candidate->ptr = ptr;
    candidate->type = type;
    candidate->pageForDeferred = NULL;
}


static FORCE_INLINE void candidatePushDeferred(RefCntChangeCandidates *candidates, void *ptr, Type *type, HeapPage *page)
{
    candidatePush(candidates, ptr, type);
    candidates->stack[candidates->top].pageForDeferred = page;
}


static FORCE_INLINE void candidatePop(RefCntChangeCandidates *candidates, void **ptr, Type **type, HeapPage **page)
{
    RefCntChangeCandidate *candidate = &candidates->stack[candidates->top--];
    *ptr = candidate->ptr;
    *type = candidate->type;
    *page = candidate->pageForDeferred;
}


// Helper functions

static FORCE_INLINE int fsgetc(bool string, void *stream, int *len)
{
    int ch = string ? ((char *)stream)[*len] : fgetc((FILE *)stream);
    (*len)++;
    return ch;
}


static int fsnprintf(bool string, void *stream, int size, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    int res = string ? vsnprintf((char *)stream, size, format, args) : vfprintf((FILE *)stream, format, args);

    va_end(args);
    return res;
}


static int fsscanf(bool string, void *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    int res = string ? vsscanf((char *)stream, format, args) : vfscanf((FILE *)stream, format, args);

    va_end(args);
    return res;
}


static FORCE_INLINE char *fsscanfString(bool string, void *stream, int *len)
{
    int capacity = 8;
    char *str = malloc(capacity);

    *len = 0;
    int writtenLen = 0;
    int ch = ' ';

    // Skip whitespace
    while (isspace(ch))
        ch = fsgetc(string, stream, len);

    // Read string
    while (ch && ch != EOF && !isspace(ch))
    {
        str[writtenLen++] = ch;
        if (writtenLen == capacity - 1)
        {
            capacity *= 2;
            str = realloc(str, capacity);
        }
        ch = fsgetc(string, stream, len);
    }

    str[writtenLen] = '\0';
    return str;
}


typedef int (*QSortCompareFn)(const void *a, const void *b, void *context);


void FORCE_INLINE qsortSwap(void *a, void *b, void *temp, int itemSize)
{
    memcpy(temp, a, itemSize);
    memcpy(a, b, itemSize);
    memcpy(b, temp, itemSize);
}


char FORCE_INLINE *qsortPartition(char *first, char *last, int itemSize, QSortCompareFn compare, void *context, void *temp)
{
    char *i = first;
    char *j = last;

    char *pivot = first;

    while (i < j)
    {
        while (compare(i, pivot, context) <= 0 && i < last)
            i += itemSize;

        while (compare(j, pivot, context) > 0 && j > first)
            j -= itemSize;

        if (i < j)
            qsortSwap(i, j, temp, itemSize);
    }

    qsortSwap(pivot, j, temp, itemSize);

    return j;
}


void qsortEx(char *first, char *last, int itemSize, QSortCompareFn compare, void *context, void *temp)
{
    if (first >= last)
        return;

    char *partition = qsortPartition(first, last, itemSize, compare, context, temp);

    qsortEx(first, partition - itemSize, itemSize, compare, context, temp);
    qsortEx(partition + itemSize, last, itemSize, compare, context, temp);
}


// Virtual machine

void vmInit(VM *vm, int stackSize, bool fileSystemEnabled, Error *error)
{
    vm->fiber = vm->mainFiber = malloc(sizeof(Fiber));
    vm->fiber->parent = NULL;
    vm->fiber->refCntChangeCandidates = &vm->refCntChangeCandidates;
    vm->fiber->vm = vm;
    vm->fiber->alive = true;
    vm->fiber->fileSystemEnabled = fileSystemEnabled;

    pageInit(&vm->pages, vm->fiber, error);

    vm->fiber->stack = chunkAlloc(&vm->pages, stackSize * sizeof(Slot), NULL, NULL, true, error);
    vm->fiber->stackSize = stackSize;

    candidateInit(&vm->refCntChangeCandidates);

    memset(&vm->hooks, 0, sizeof(vm->hooks));
    vm->terminatedNormally = false;
    vm->error = error;
}


void vmFree(VM *vm)
{
    HeapPage *page = pageFind(&vm->pages, vm->mainFiber->stack, true);
    if (!page)
       vm->error->runtimeHandler(vm->error->context, VM_RUNTIME_ERROR, "No fiber stack");

    chunkChangeRefCnt(&vm->pages, page, vm->mainFiber->stack, -1);

    candidateFree(&vm->refCntChangeCandidates);
    pageFree(&vm->pages, vm->terminatedNormally);

    free(vm->mainFiber);
}


void vmReset(VM *vm, Instruction *code, DebugInfo *debugPerInstr)
{
    vm->fiber = vm->pages.fiber = vm->mainFiber;
    vm->fiber->code = code;
    vm->fiber->debugPerInstr = debugPerInstr;
    vm->fiber->ip = 0;
    vm->fiber->top = vm->fiber->base = vm->fiber->stack + vm->fiber->stackSize - 1;
}


static FORCE_INLINE void vmLoop(VM *vm);


static FORCE_INLINE void doCheckStr(const char *str, Error *error)
{
#ifdef UMKA_STR_DEBUG
    if (!str)
        return;

    const StrDimensions *dims = getStrDims(str);
    if (dims->len != strlen(str) || dims->capacity < dims->len + 1)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Invalid string: %s", str);
#endif
}


static FORCE_INLINE void doHook(Fiber *fiber, HookFunc *hooks, HookEvent event)
{
    if (!hooks || !hooks[event])
        return;

    const DebugInfo *debug = &fiber->debugPerInstr[fiber->ip];
    hooks[event](debug->fileName, debug->fnName, debug->line);
}


static FORCE_INLINE void doSwapImpl(Slot *slot)
{
    Slot val = slot[0];
    slot[0] = slot[1];
    slot[1] = val;
}


static FORCE_INLINE void doDerefImpl(Slot *slot, TypeKind typeKind, Error *error)
{
    if (!slot->ptrVal)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Pointer is null");

    switch (typeKind)
    {
        case TYPE_INT8:         slot->intVal     = *(int8_t         *)slot->ptrVal; break;
        case TYPE_INT16:        slot->intVal     = *(int16_t        *)slot->ptrVal; break;
        case TYPE_INT32:        slot->intVal     = *(int32_t        *)slot->ptrVal; break;
        case TYPE_INT:          slot->intVal     = *(int64_t        *)slot->ptrVal; break;
        case TYPE_UINT8:        slot->intVal     = *(uint8_t        *)slot->ptrVal; break;
        case TYPE_UINT16:       slot->intVal     = *(uint16_t       *)slot->ptrVal; break;
        case TYPE_UINT32:       slot->intVal     = *(uint32_t       *)slot->ptrVal; break;
        case TYPE_UINT:         slot->uintVal    = *(uint64_t       *)slot->ptrVal; break;
        case TYPE_BOOL:         slot->intVal     = *(bool           *)slot->ptrVal; break;
        case TYPE_CHAR:         slot->intVal     = *(unsigned char  *)slot->ptrVal; break;
        case TYPE_REAL32:       slot->realVal    = *(float          *)slot->ptrVal; break;
        case TYPE_REAL:         slot->realVal    = *(double         *)slot->ptrVal; break;
        case TYPE_PTR:          slot->ptrVal     = *(void *         *)slot->ptrVal; break;
        case TYPE_WEAKPTR:      slot->weakPtrVal = *(uint64_t       *)slot->ptrVal; break;
        case TYPE_STR:
        {
            slot->ptrVal = *(void **)slot->ptrVal;
            doCheckStr((char *)slot->ptrVal, error);
            break;
        }
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_MAP:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:      break;  // Always represented by pointer, not dereferenced
        case TYPE_FIBER:        slot->ptrVal     = *(void *         *)slot->ptrVal; break;
        case TYPE_FN:           slot->intVal     = *(int64_t        *)slot->ptrVal; break;

        default:                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type"); return;
    }
}


static FORCE_INLINE void doAssignImpl(void *lhs, Slot rhs, TypeKind typeKind, int structSize, Error *error)
{
    if (!lhs)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Pointer is null");

    Const rhsConstant = {.intVal = rhs.intVal};
    if (typeOverflow(typeKind, rhsConstant))
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Overflow of %s", typeKindSpelling(typeKind));

    switch (typeKind)
    {
        case TYPE_INT8:         *(int8_t        *)lhs = rhs.intVal;  break;
        case TYPE_INT16:        *(int16_t       *)lhs = rhs.intVal;  break;
        case TYPE_INT32:        *(int32_t       *)lhs = rhs.intVal;  break;
        case TYPE_INT:          *(int64_t       *)lhs = rhs.intVal;  break;
        case TYPE_UINT8:        *(uint8_t       *)lhs = rhs.intVal;  break;
        case TYPE_UINT16:       *(uint16_t      *)lhs = rhs.intVal;  break;
        case TYPE_UINT32:       *(uint32_t      *)lhs = rhs.intVal;  break;
        case TYPE_UINT:         *(uint64_t      *)lhs = rhs.uintVal; break;
        case TYPE_BOOL:         *(bool          *)lhs = rhs.intVal;  break;
        case TYPE_CHAR:         *(unsigned char *)lhs = rhs.intVal;  break;
        case TYPE_REAL32:       *(float         *)lhs = rhs.realVal; break;
        case TYPE_REAL:         *(double        *)lhs = rhs.realVal; break;
        case TYPE_PTR:          *(void *        *)lhs = rhs.ptrVal;  break;
        case TYPE_WEAKPTR:      *(uint64_t      *)lhs = rhs.weakPtrVal; break;
        case TYPE_STR:
        {
            doCheckStr((char *)rhs.ptrVal, error);
            *(void **)lhs = rhs.ptrVal;
            break;
        }
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_MAP:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:
        {
            if (!rhs.ptrVal)
                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Pointer is null");
            memcpy(lhs, rhs.ptrVal, structSize);
            break;
        }
        case TYPE_FIBER:        *(void *        *)lhs = rhs.ptrVal;  break;
        case TYPE_FN:           *(int64_t       *)lhs = rhs.intVal;  break;

        default:                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type"); return;
    }
}


static FORCE_INLINE void doAddPtrBaseRefCntCandidate(RefCntChangeCandidates *candidates, void *ptr, Type *type)
{
    if (typeKindGarbageCollected(type->base->kind))
    {
        void *data = ptr;
        if (type->base->kind == TYPE_PTR || type->base->kind == TYPE_STR || type->base->kind == TYPE_FIBER)
            data = *(void **)data;

        candidatePush(candidates, data, type->base);
    }
}


static FORCE_INLINE void doAddArrayItemsRefCntCandidates(RefCntChangeCandidates *candidates, void *ptr, Type *type, int len)
{
    if (typeKindGarbageCollected(type->base->kind))
    {
        char *itemPtr = ptr;
        int itemSize = typeSizeNoCheck(type->base);

        for (int i = 0; i < len; i++)
        {
            void *item = itemPtr;
            if (type->base->kind == TYPE_PTR || type->base->kind == TYPE_STR || type->base->kind == TYPE_FIBER)
                item = *(void **)item;

            candidatePush(candidates, item, type->base);
            itemPtr += itemSize;
        }
    }
}


static FORCE_INLINE void doAddStructFieldsRefCntCandidates(RefCntChangeCandidates *candidates, void *ptr, Type *type)
{
    for (int i = 0; i < type->numItems; i++)
    {
        if (typeKindGarbageCollected(type->field[i]->type->kind))
        {
            void *field = (char *)ptr + type->field[i]->offset;
            if (type->field[i]->type->kind == TYPE_PTR || type->field[i]->type->kind == TYPE_STR || type->field[i]->type->kind == TYPE_FIBER)
                field = *(void **)field;

            candidatePush(candidates, field, type->field[i]->type);
        }
    }
}


static FORCE_INLINE void doChangeRefCntImpl(Fiber *fiber, HeapPages *pages, void *ptr, Type *type, TokenKind tokKind)
{
    // Update ref counts for pointers (including static/dynamic array items and structure/interface fields) if allocated dynamically
    // All garbage collected composite types are represented by pointers by default
    // RTTI is required for lists, trees, etc., since the propagation depth for the root ref count is unknown at compile time

    RefCntChangeCandidates *candidates = fiber->refCntChangeCandidates;
    candidateReset(candidates);
    candidatePush(candidates, ptr, type);

    while (candidates->top >= 0)
    {
        HeapPage *pageForDeferred = NULL;
        candidatePop(candidates, &ptr, &type, &pageForDeferred);

        // Process deferred ref count updates first (the heap page should have been memoized for them)
        if (pageForDeferred)
        {
            chunkChangeRefCnt(pages, pageForDeferred, ptr, (tokKind == TOK_PLUSPLUS) ? 1 : -1);
            continue;
        }

        // Process all other updates
        switch (type->kind)
        {
            case TYPE_PTR:
            {
                HeapPage *page = pageFind(pages, ptr, true);
                if (!page)
                    break;

                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, ptr, 1);
                else
                {
                    HeapChunkHeader *chunk = pageGetChunkHeader(page, ptr);
                    if (chunk->refCnt > 1)
                    {
                        chunkChangeRefCnt(pages, page, ptr, -1);
                        break;
                    }

                    // Only one ref is left. Defer processing the parent and traverse the children before removing the ref
                    candidatePushDeferred(candidates, ptr, type, page);

                    // Sometimes the last remaining ref to chunk data is a pointer to a single item of a composite type (interior pointer)
                    // In this case, we should traverse children as for the actual composite type, rather than for the pointer
                    if (chunk->type)
                    {
                        void *chunkDataPtr = (char *)chunk + sizeof(HeapChunkHeader);

                        switch (chunk->type->kind)
                        {
                            case TYPE_ARRAY:
                            {
                                doAddArrayItemsRefCntCandidates(candidates, chunkDataPtr, chunk->type, chunk->type->numItems);
                                break;
                            }
                            case TYPE_DYNARRAY:
                            {
                                DynArrayDimensions *dims = (DynArrayDimensions *)chunkDataPtr;
                                void *data = (char *)chunkDataPtr + sizeof(DynArrayDimensions);
                                doAddArrayItemsRefCntCandidates(candidates, data, chunk->type, dims->len);
                                break;
                            }
                            case TYPE_STRUCT:
                            {
                                doAddStructFieldsRefCntCandidates(candidates, chunkDataPtr, chunk->type);
                                break;
                            }
                            default:
                            {
                                doAddPtrBaseRefCntCandidate(candidates, ptr, type);
                                break;
                            }
                        }
                    }
                    else
                        doAddPtrBaseRefCntCandidate(candidates, ptr, type);
                }
                break;
            }

            case TYPE_STR:
            {
                doCheckStr((char *)ptr, pages->error);

                HeapPage *page = pageFind(pages, ptr, true);
                if (!page)
                    break;

                chunkChangeRefCnt(pages, page, ptr, (tokKind == TOK_PLUSPLUS) ? 1 : -1);
                break;
            }

            case TYPE_ARRAY:
            {
                doAddArrayItemsRefCntCandidates(candidates, ptr, type, type->numItems);
                break;
            }

            case TYPE_DYNARRAY:
            {
                DynArray *array = (DynArray *)ptr;
                HeapPage *page = pageFind(pages, array->data, true);
                if (!page)
                    break;

                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, array->data, 1);
                else
                {
                    HeapChunkHeader *chunk = pageGetChunkHeader(page, array->data);
                    if (chunk->refCnt > 1)
                    {
                        chunkChangeRefCnt(pages, page, array->data, -1);
                        break;
                    }

                    // Only one ref is left. Defer processing the parent and traverse the children before removing the ref
                    candidatePushDeferred(candidates, array->data, type, page);
                    doAddArrayItemsRefCntCandidates(candidates, array->data, type, getDims(array)->len);
                }
                break;
            }

            case TYPE_MAP:
            {
                Map *map = (Map *)ptr;
                candidatePush(candidates, map->root, typeMapNodePtr(type));
                break;
            }

            case TYPE_STRUCT:
            {
                doAddStructFieldsRefCntCandidates(candidates, ptr, type);
                break;
            }

            case TYPE_INTERFACE:
            {
                Interface *interface = (Interface *)ptr;
                if (interface->self)
                    candidatePush(candidates, interface->self, interface->selfType);
                break;
            }

            case TYPE_CLOSURE:
            {
                doAddStructFieldsRefCntCandidates(candidates, ptr, type);
                break;
            }

            case TYPE_FIBER:
            {
                HeapPage *page = pageFind(pages, ptr, true);
                if (!page)
                    break;

                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, ptr, 1);
                else
                {
                    HeapChunkHeader *chunk = pageGetChunkHeader(page, ptr);
                    if (chunk->refCnt > 1)
                    {
                        chunkChangeRefCnt(pages, page, ptr, -1);
                        break;
                    }

                    if (((Fiber *)ptr)->alive)
                        pages->error->runtimeHandler(pages->error->context, VM_RUNTIME_ERROR, "Cannot destroy a busy fiber");

                    // Only one ref is left. Defer processing the parent and traverse the children before removing the ref
                    HeapPage *stackPage = pageFind(pages, ((Fiber *)ptr)->stack, true);
                    if (!stackPage)
                        pages->error->runtimeHandler(pages->error->context, VM_RUNTIME_ERROR, "No fiber stack");

                    chunkChangeRefCnt(pages, stackPage, ((Fiber *)ptr)->stack, -1);
                    chunkChangeRefCnt(pages, page, ptr, -1);
                }
                break;
            }

            default: break;
        }
    }
}


static FORCE_INLINE char *doAllocStr(HeapPages *pages, int64_t len, Error *error)
{
    StrDimensions dims = {.len = len, .capacity = 2 * (len + 1)};

    char *dimsAndData = chunkAlloc(pages, sizeof(StrDimensions) + dims.capacity, NULL, NULL, false, error);
    *(StrDimensions *)dimsAndData = dims;

    char *data = dimsAndData + sizeof(StrDimensions);
    data[len] = 0;

    return data;
}


static FORCE_INLINE char *doGetEmptyStr()
{
    StrDimensions dims = {.len = 0, .capacity = 1};

    static char dimsAndData[sizeof(StrDimensions) + 1];
    *(DynArrayDimensions *)dimsAndData = dims;

    char *data = dimsAndData + sizeof(StrDimensions);
    data[0] = 0;

    return data;
}


static FORCE_INLINE void doAllocDynArray(HeapPages *pages, DynArray *array, Type *type, int64_t len, Error *error)
{
    array->type     = type;
    array->itemSize = typeSizeNoCheck(array->type->base);

    DynArrayDimensions dims = {.len = len, .capacity = 2 * (len + 1)};

    char *dimsAndData = chunkAlloc(pages, sizeof(DynArrayDimensions) + dims.capacity * array->itemSize, array->type, NULL, false, error);
    *(DynArrayDimensions *)dimsAndData = dims;

    array->data = dimsAndData + sizeof(DynArrayDimensions);
}


static FORCE_INLINE void doGetEmptyDynArray(DynArray *array, Type *type)
{
    array->type     = type;
    array->itemSize = typeSizeNoCheck(array->type->base);

    static DynArrayDimensions dims = {.len = 0, .capacity = 0};
    array->data = (char *)(&dims) + sizeof(DynArrayDimensions);
}


static FORCE_INLINE void doAllocMap(HeapPages *pages, Map *map, Type *type, Error *error)
{
    map->type      = type;
    map->root      = chunkAlloc(pages, typeSizeNoCheck(type->base), type->base, NULL, false, error);
    map->root->len = 0;
}


static void doGetMapKeyBytes(Slot key, Type *keyType, Error *error, char **keyBytes, int *keySize)
{
    switch (keyType->kind)
    {
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:
        case TYPE_UINT:
        case TYPE_BOOL:
        case TYPE_CHAR:
        case TYPE_REAL32:
        case TYPE_REAL:
        case TYPE_PTR:
        case TYPE_WEAKPTR:
        {
            // keyBytes must point to a pre-allocated 8-byte buffer
            doAssignImpl(*keyBytes, key, keyType->kind, 0, error);
            *keySize = typeSizeNoCheck(keyType);
            break;
        }
        case TYPE_STR:
        {
            doCheckStr((char *)key.ptrVal, error);
            *keyBytes = key.ptrVal ? (char *)key.ptrVal : doGetEmptyStr();
            *keySize = getStrDims(*keyBytes)->len + 1;
            break;
        }
        case TYPE_ARRAY:
        case TYPE_STRUCT:
        {
            *keyBytes = (char *)key.ptrVal;
            *keySize = typeSizeNoCheck(keyType);
            break;
        }
        default:
        {
            *keyBytes = NULL;
            *keySize = 0;
            break;
        }
    }
}


static FORCE_INLINE void doRebalanceMapNodes(MapNode **nodeInParent)
{
    // A naive tree rotation to prevent degeneration into a linked list
    MapNode *node = *nodeInParent;

    if (node && !node->left && node->right && !node->right->left && node->right->right)
    {
        *nodeInParent = node->right;
        node->right = NULL;
        (*nodeInParent)->left = node;
    }

    if (node && node->left && !node->right && node->left->left && !node->left->right)
    {
        *nodeInParent = node->left;
        node->left = NULL;
        (*nodeInParent)->right = node;
    }
}


static FORCE_INLINE MapNode **doGetMapNode(Map *map, Slot key, bool createMissingNodes, HeapPages *pages, Error *error)
{
    if (!map || !map->root)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map is null");

    Slot keyBytesBuffer = {0};
    char *keyBytes = (char *)&keyBytesBuffer;
    int keySize = 0;

    doGetMapKeyBytes(key, typeMapKey(map->type), error, &keyBytes, &keySize);

    if (!keyBytes)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map key is null");

    if (keySize == 0)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map key has zero length");

    MapNode **node = &map->root;

    while (*node)
    {
        Slot nodeKeyBytesBuffer = {0};
        char *nodeKeyBytes = (char *)&nodeKeyBytesBuffer;
        int nodeKeySize = 0;

        if ((*node)->key)
        {
            Slot nodeKeySlot = {.ptrVal = (*node)->key};
            doDerefImpl(&nodeKeySlot, typeMapKey(map->type)->kind, error);
            doGetMapKeyBytes(nodeKeySlot, typeMapKey(map->type), error, &nodeKeyBytes, &nodeKeySize);
        }

        int keyDiff = 0;
        if (keySize != nodeKeySize)
            keyDiff = keySize - nodeKeySize;
        else
            keyDiff = memcmp(keyBytes, nodeKeyBytes, keySize);

        if (keyDiff > 0)
            node = &(*node)->right;
        else if (keyDiff < 0)
            node = &(*node)->left;
        else
            return node;

        doRebalanceMapNodes(node);
    }

    if (createMissingNodes)
    {
        Type *nodeType = map->type->base;
        *node = (MapNode *)chunkAlloc(pages, typeSizeNoCheck(nodeType), nodeType, NULL, false, error);
    }

    return node;
}


static MapNode *doCopyMapNode(Map *map, MapNode *node, Fiber *fiber, HeapPages *pages, Error *error)
{
    if (!node)
        return NULL;

    Type *nodeType = map->type->base;
    MapNode *result = (MapNode *)chunkAlloc(pages, typeSizeNoCheck(nodeType), nodeType, NULL, false, error);

    result->len = node->len;

    if (node->key)
    {
        Type *keyType = typeMapKey(map->type);
        int keySize = typeSizeNoCheck(keyType);

        Slot srcKey = {.ptrVal = node->key};
        doDerefImpl(&srcKey, keyType->kind, error);

        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        result->key = chunkAlloc(pages, typeSizeNoCheck(keyType), keyType->kind == TYPE_DYNARRAY ? NULL : keyType, NULL, false, error);

        if (typeGarbageCollected(keyType))
            doChangeRefCntImpl(fiber, pages, srcKey.ptrVal, keyType, TOK_PLUSPLUS);

        doAssignImpl(result->key, srcKey, keyType->kind, keySize, error);
    }

    if (node->data)
    {
        Type *itemType = typeMapItem(map->type);
        int itemSize = typeSizeNoCheck(itemType);

        Slot srcItem = {.ptrVal = node->data};
        doDerefImpl(&srcItem, itemType->kind, error);

        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        result->data = chunkAlloc(pages, typeSizeNoCheck(itemType), itemType->kind == TYPE_DYNARRAY ? NULL : itemType, NULL, false, error);

        if (typeGarbageCollected(itemType))
            doChangeRefCntImpl(fiber, pages, srcItem.ptrVal, itemType, TOK_PLUSPLUS);

        doAssignImpl(result->data, srcItem, itemType->kind, itemSize, error);
    }

    if (node->left)
        result->left = doCopyMapNode(map, node->left, fiber, pages, error);

    if (node->right)
        result->right = doCopyMapNode(map, node->right, fiber, pages, error);

    return result;
}


static void doGetMapKeysRecursively(Map *map, MapNode *node, void *keys, int *numKeys, Error *error)
{
    if (node->left)
        doGetMapKeysRecursively(map, node->left, keys, numKeys, error);

    if (node->key)
    {
        Type *keyType = typeMapKey(map->type);
        int keySize = typeSizeNoCheck(keyType);
        void *destKey = (char *)keys + keySize * (*numKeys);

        Slot srcKey = {.ptrVal = node->key};
        doDerefImpl(&srcKey, keyType->kind, error);
        doAssignImpl(destKey, srcKey, keyType->kind, keySize, error);

        (*numKeys)++;
    }

    if (node->right)
        doGetMapKeysRecursively(map, node->right, keys, numKeys, error);
}


static FORCE_INLINE void doGetMapKeys(Map *map, void *keys, Error *error)
{
    int numKeys = 0;
    doGetMapKeysRecursively(map, map->root, keys, &numKeys, error);
    if (numKeys != map->root->len)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Wrong number of map keys");
}


static FORCE_INLINE Fiber *doAllocFiber(Fiber *parent, Closure *childClosure, Type *childClosureType, HeapPages *pages, Error *error)
{
    if (!childClosure || childClosure->entryOffset <= 0)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Called function is not defined");

    // Copy whole fiber context
    Fiber *child = chunkAlloc(pages, sizeof(Fiber), NULL, NULL, false, error);

    *child = *parent;
    child->stack = chunkAlloc(pages, child->stackSize * sizeof(Slot), NULL, NULL, true, error);
    child->top = child->base = child->stack + child->stackSize - 1;

    child->parent = parent;

    Signature *childClosureSig = &childClosureType->field[0]->type->sig;

    // Push upvalues
    child->top -= sizeof(Interface) / sizeof(Slot);
    *(Interface *)child->top = childClosure->upvalue;
    doChangeRefCntImpl(child, pages, child->top, childClosureSig->param[0]->type, TOK_PLUSPLUS);

    // Push 'return from fiber' signal instead of return address
    (--child->top)->intVal = VM_RETURN_FROM_FIBER;

     // Call child fiber closure
    child->ip = childClosure->entryOffset;

    return child;
}


static FORCE_INLINE int doPrintIndented(char *buf, int maxLen, int depth, bool pretty, char ch)
{
    enum {INDENT_WIDTH = 4};

    int len = 0;

    switch (ch)
    {
        case '(':
        case '[':
        case '{':
        {
            len += snprintf(buf + len, maxLen, "%c", ch);
            if (pretty)
                len += snprintf(buf + len, maxLen, "\n%*c", INDENT_WIDTH * (depth + 1), ' ');
            break;
        }

        case ')':
        case ']':
        case '}':
        {
            if (pretty)
            {
                if (depth > 0)
                    len += snprintf(buf + len, maxLen, "\n%*c", INDENT_WIDTH * depth, ' ');
                else
                    len += snprintf(buf + len, maxLen, "\n");
            }
            len += snprintf(buf + len, maxLen, "%c", ch);
            break;
        }

        case ' ':
        {
            if (pretty)
                len += snprintf(buf + len, maxLen, "\n%*c", INDENT_WIDTH * (depth + 1), ' ');
            else
                len += snprintf(buf + len, maxLen, " ");
            break;
        }

        default: break;
    }

    return len;
}


static int doFillReprBuf(Slot *slot, Type *type, char *buf, int maxLen, int depth, bool pretty, bool dereferenced, Error *error)
{
    enum {MAX_DEPTH = 20};

    int len = 0;

    if (depth == MAX_DEPTH)
    {
        len += snprintf(buf + len, maxLen, "...");
        return len;
    }

    switch (type->kind)
    {
        case TYPE_VOID:     len += snprintf(buf + len, maxLen, "void");                                                             break;
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:   len += snprintf(buf + len, maxLen, "%lld", (long long int)slot->intVal);                                break;
        case TYPE_UINT:     len += snprintf(buf + len, maxLen, "%llu", (unsigned long long int)slot->uintVal);                      break;
        case TYPE_BOOL:     len += snprintf(buf + len, maxLen, slot->intVal ? "true" : "false");                                    break;
        case TYPE_CHAR:
        {
            const char *format = (unsigned char)slot->intVal >= ' ' ? "'%c'" : "0x%02X";
            len += snprintf(buf + len, maxLen, format, (unsigned char)slot->intVal);
            break;
        }
        case TYPE_REAL32:
        case TYPE_REAL:     len += snprintf(buf + len, maxLen, "%lg", slot->realVal);                                               break;
        case TYPE_PTR:
        {
            len += snprintf(buf + len, maxLen, "%p", slot->ptrVal);

            if (dereferenced && slot->ptrVal && type->base->kind != TYPE_VOID)
            {
                Slot dataSlot = {.ptrVal = slot->ptrVal};
                doDerefImpl(&dataSlot, type->base->kind, error);

                len += snprintf(buf + len, maxLen, " -> ");
                len += doPrintIndented(buf + len, maxLen, depth, pretty, '(');
                len += doFillReprBuf(&dataSlot, type->base, buf + len, maxLen, depth + 1, pretty, dereferenced, error);
                len += doPrintIndented(buf + len, maxLen, depth, pretty, ')');
            }
            break;
        }
        case TYPE_WEAKPTR:  len += snprintf(buf + len, maxLen, "%llx", (unsigned long long int)slot->weakPtrVal);                   break;
        case TYPE_STR:
        {
            doCheckStr((char *)slot->ptrVal, error);
            len += snprintf(buf + len, maxLen, "\"%s\"", slot->ptrVal ? (char *)slot->ptrVal : "");
            break;
        }
        case TYPE_ARRAY:
        {
            len += doPrintIndented(buf + len, maxLen, depth, pretty, '[');

            char *itemPtr = (char *)slot->ptrVal;
            int itemSize = typeSizeNoCheck(type->base);

            for (int i = 0; i < type->numItems; i++)
            {
                Slot itemSlot = {.ptrVal = itemPtr};
                doDerefImpl(&itemSlot, type->base->kind, error);
                len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, depth + 1, pretty, dereferenced, error);

                if (i < type->numItems - 1)
                    len += doPrintIndented(buf + len, maxLen, depth, pretty, ' ');

                itemPtr += itemSize;
            }

            len += doPrintIndented(buf + len, maxLen, depth, pretty, ']');
            break;
        }

        case TYPE_DYNARRAY:
        {
            len += doPrintIndented(buf + len, maxLen, depth, pretty, '[');

            DynArray *array = (DynArray *)slot->ptrVal;
            if (array && array->data)
            {
                char *itemPtr = array->data;
                for (int i = 0; i < getDims(array)->len; i++)
                {
                    Slot itemSlot = {.ptrVal = itemPtr};
                    doDerefImpl(&itemSlot, type->base->kind, error);
                    len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, depth + 1, pretty, dereferenced, error);

                    if (i < getDims(array)->len - 1)
                        len += doPrintIndented(buf + len, maxLen, depth, pretty, ' ');

                    itemPtr += array->itemSize;
                }
            }

            len += doPrintIndented(buf + len, maxLen, depth, pretty, ']');
            break;
        }

        case TYPE_MAP:
        {
            len += doPrintIndented(buf + len, maxLen, depth, pretty, '{');

            Map *map = (Map *)slot->ptrVal;
            if (map && map->root)
            {
                Type *keyType = typeMapKey(map->type);
                Type *itemType = typeMapItem(map->type);

                int keySize = typeSizeNoCheck(keyType);
                void *keys = malloc(map->root->len * keySize);

                doGetMapKeys(map, keys, error);

                char *keyPtr = (char *)keys;
                for (int i = 0; i < map->root->len; i++)
                {
                    Slot keySlot = {.ptrVal = keyPtr};
                    doDerefImpl(&keySlot, keyType->kind, error);
                    len += doFillReprBuf(&keySlot, keyType, buf + len, maxLen, depth + 1, pretty, dereferenced, error);

                    len += snprintf(buf + len, maxLen, ": ");

                    MapNode *node = *doGetMapNode(map, keySlot, false, NULL, error);
                    if (!node)
                        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map node is null");

                    Slot itemSlot = {.ptrVal = node->data};
                    doDerefImpl(&itemSlot, itemType->kind, error);
                    len += doFillReprBuf(&itemSlot, itemType, buf + len, maxLen, depth + 1, pretty, dereferenced, error);

                    if (i < map->root->len - 1)
                        len += doPrintIndented(buf + len, maxLen, depth, pretty, ' ');

                    keyPtr += keySize;
                }

                free(keys);
            }

            len += doPrintIndented(buf + len, maxLen, depth, pretty, '}');
            break;
        }


        case TYPE_STRUCT:
        case TYPE_CLOSURE:
        {
            len += doPrintIndented(buf + len, maxLen, depth, pretty, '{');

            bool skipNames = typeExprListStruct(type);

            for (int i = 0; i < type->numItems; i++)
            {
                Slot fieldSlot = {.ptrVal = (char *)slot->ptrVal + type->field[i]->offset};
                doDerefImpl(&fieldSlot, type->field[i]->type->kind, error);
                if (!skipNames)
                    len += snprintf(buf + len, maxLen, "%s: ", type->field[i]->name);
                len += doFillReprBuf(&fieldSlot, type->field[i]->type, buf + len, maxLen, depth + 1, pretty, dereferenced, error);

                if (i < type->numItems - 1)
                    len += doPrintIndented(buf + len, maxLen, depth, pretty, ' ');
            }

            len += doPrintIndented(buf + len, maxLen, depth, pretty, '}');
            break;
        }

        case TYPE_INTERFACE:
        {
            Interface *interface = (Interface *)slot->ptrVal;
            if (interface->self)
            {
                Slot selfSlot = {.ptrVal = interface->self};
                doDerefImpl(&selfSlot, interface->selfType->base->kind, error);

                if (pretty)
                {
                    char selfTypeBuf[DEFAULT_STR_LEN + 1];
                    len += snprintf(buf + len, maxLen, "%s", typeSpelling(interface->selfType->base, selfTypeBuf));
                    len += doPrintIndented(buf + len, maxLen, depth, pretty, '(');
                }

                len += doFillReprBuf(&selfSlot, interface->selfType->base, buf + len, maxLen, depth + 1, pretty, dereferenced, error);

                if (pretty)
                    len += doPrintIndented(buf + len, maxLen, depth, pretty, ')');
            }
            else
                len += snprintf(buf + len, maxLen, "null");
            break;
        }

        case TYPE_FIBER:    len += snprintf(buf + len, maxLen, "fiber @ %p", slot->ptrVal);                break;
        case TYPE_FN:       len += snprintf(buf + len, maxLen, "fn @ %lld", (long long int)slot->intVal);  break;
        default:            break;
    }

    return len;
}


typedef enum
{
    FORMAT_SIZE_SHORT_SHORT,
    FORMAT_SIZE_SHORT,
    FORMAT_SIZE_NORMAL,
    FORMAT_SIZE_LONG,
    FORMAT_SIZE_LONG_LONG
} FormatStringTypeSize;


static FORCE_INLINE void doCheckFormatString(const char *format, int *formatLen, int *typeLetterPos, TypeKind *typeKind, FormatStringTypeSize *size, Error *error)
{
    *size = FORMAT_SIZE_NORMAL;
    *typeKind = TYPE_VOID;
    int i = 0;

    while (format[i])
    {
        *size = FORMAT_SIZE_NORMAL;
        *typeKind = TYPE_VOID;

        while (format[i] && format[i] != '%')
            i++;

        // "%" [flags] [width] ["." precision] [length] type
        // "%"
        if (format[i] == '%')
        {
            i++;

            // [flags]
            while (format[i] == '+' || format[i] == '-'  || format[i] == ' ' ||
                   format[i] == '0' || format[i] == '\'' || format[i] == '#')
                i++;

            // [width]
            while (format[i] >= '0' && format[i] <= '9')
                i++;

            // [.precision]
            if (format[i] == '.')
            {
                i++;
                while (format[i] >= '0' && format[i] <= '9')
                    i++;
            }

            // [length]
            if (format[i] == 'h')
            {
                *size = FORMAT_SIZE_SHORT;
                i++;

                if (format[i] == 'h')
                {
                    *size = FORMAT_SIZE_SHORT_SHORT;
                    i++;
                }
            }
            else if (format[i] == 'l')
            {
                *size = FORMAT_SIZE_LONG;
                i++;

                if (format[i] == 'l')
                {
                    *size = FORMAT_SIZE_LONG_LONG;
                    i++;
                }
            }

            // type
            *typeLetterPos = i;
            switch (format[i])
            {
                case '%': i++; continue;
                case 'd':
                case 'i':
                {
                    switch (*size)
                    {
                        case FORMAT_SIZE_SHORT_SHORT:  *typeKind = TYPE_INT8;      break;
                        case FORMAT_SIZE_SHORT:        *typeKind = TYPE_INT16;     break;
                        case FORMAT_SIZE_NORMAL:
                        case FORMAT_SIZE_LONG:         *typeKind = TYPE_INT32;     break;
                        case FORMAT_SIZE_LONG_LONG:    *typeKind = TYPE_INT;       break;
                    }
                    break;
                }
                case 'u':
                case 'x':
                case 'X':
                {
                    switch (*size)
                    {
                        case FORMAT_SIZE_SHORT_SHORT:  *typeKind = TYPE_UINT8;      break;
                        case FORMAT_SIZE_SHORT:        *typeKind = TYPE_UINT16;     break;
                        case FORMAT_SIZE_NORMAL:
                        case FORMAT_SIZE_LONG:         *typeKind = TYPE_UINT32;     break;
                        case FORMAT_SIZE_LONG_LONG:    *typeKind = TYPE_UINT;       break;
                    }
                    break;
                }
                case 'f':
                case 'F':
                case 'e':
                case 'E':
                case 'g':
                case 'G': *typeKind = (*size == FORMAT_SIZE_NORMAL) ? TYPE_REAL32 : TYPE_REAL;  break;
                case 's': *typeKind = TYPE_STR;                                                 break;
                case 'c': *typeKind = TYPE_CHAR;                                                break;
                case 'v': *typeKind = TYPE_INTERFACE;  /* Actually any type */                  break;

                default : error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type character %c in format string", format[i]);
            }
            i++;
        }
        break;
    }
    *formatLen = i;
}


static FORCE_INLINE int doPrintSlot(bool string, void *stream, int maxLen, const char *format, Slot slot, TypeKind typeKind, Error *error)
{
    int len = -1;

    switch (typeKind)
    {
        case TYPE_VOID:         len = fsnprintf(string, stream, maxLen, format);                               break;
        case TYPE_INT8:         len = fsnprintf(string, stream, maxLen, format, (int8_t        )slot.intVal);  break;
        case TYPE_INT16:        len = fsnprintf(string, stream, maxLen, format, (int16_t       )slot.intVal);  break;
        case TYPE_INT32:        len = fsnprintf(string, stream, maxLen, format, (int32_t       )slot.intVal);  break;
        case TYPE_INT:          len = fsnprintf(string, stream, maxLen, format,                 slot.intVal);  break;
        case TYPE_UINT8:        len = fsnprintf(string, stream, maxLen, format, (uint8_t       )slot.intVal);  break;
        case TYPE_UINT16:       len = fsnprintf(string, stream, maxLen, format, (uint16_t      )slot.intVal);  break;
        case TYPE_UINT32:       len = fsnprintf(string, stream, maxLen, format, (uint32_t      )slot.intVal);  break;
        case TYPE_UINT:         len = fsnprintf(string, stream, maxLen, format,                 slot.uintVal); break;
        case TYPE_BOOL:         len = fsnprintf(string, stream, maxLen, format, (bool          )slot.intVal);  break;
        case TYPE_CHAR:         len = fsnprintf(string, stream, maxLen, format, (unsigned char )slot.intVal);  break;
        case TYPE_REAL32:
        case TYPE_REAL:         len = fsnprintf(string, stream, maxLen, format,                 slot.realVal); break;
        case TYPE_STR:
        {
            doCheckStr((char *)slot.ptrVal, error);
            len = fsnprintf(string, stream, maxLen, format, slot.ptrVal ? (char *)slot.ptrVal : "");
            break;
        }
        default:                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type"); break;
    }

    return len;
}


enum
{
    STACK_OFFSET_COUNT  = 3,
    STACK_OFFSET_STREAM = 2,
    STACK_OFFSET_FORMAT = 1,
    STACK_OFFSET_VALUE  = 0
};


static FORCE_INLINE void doBuiltinPrintf(Fiber *fiber, HeapPages *pages, bool console, bool string, Error *error)
{
    const int prevLen  = fiber->top[STACK_OFFSET_COUNT].intVal;
    void *stream       = console ? stdout : fiber->top[STACK_OFFSET_STREAM].ptrVal;
    const char *format = (const char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal;
    Slot value         = fiber->top[STACK_OFFSET_VALUE];

    Type *type         = fiber->code[fiber->ip].type;
    TypeKind typeKind  = type->kind;

    if (!string && (!stream || (!fiber->fileSystemEnabled && !console)))
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "printf() destination is null");

    if (!format)
        format = doGetEmptyStr();

    int formatLen = -1, typeLetterPos = -1;
    TypeKind expectedTypeKind = TYPE_NONE;
    FormatStringTypeSize formatStringTypeSize = FORMAT_SIZE_NORMAL;

    doCheckFormatString(format, &formatLen, &typeLetterPos, &expectedTypeKind, &formatStringTypeSize, error);

    const bool hasAnyTypeFormatter = expectedTypeKind == TYPE_INTERFACE && typeLetterPos >= 0 && typeLetterPos < formatLen;     // %v

    if (type->kind != expectedTypeKind &&
        !(type->kind != TYPE_VOID            && expectedTypeKind == TYPE_INTERFACE) &&
        !(typeKindIntegerOrEnum(type->kind)  && typeKindIntegerOrEnum(expectedTypeKind)) &&
        !(typeKindReal(type->kind)           && typeKindReal(expectedTypeKind)))
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Incompatible types %s and %s in printf()", typeKindSpelling(expectedTypeKind), typeSpelling(type, typeBuf));
    }

    // Check overflow
    if (expectedTypeKind != TYPE_VOID)
    {
        Const arg;
        if (typeKindReal(expectedTypeKind))
            arg.realVal = value.realVal;
        else
            arg.intVal = value.intVal;

        if (typeOverflow(expectedTypeKind, arg))
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Overflow of %s", typeKindSpelling(expectedTypeKind));
    }

    char curFormatBuf[DEFAULT_STR_LEN + 1];
    bool isCurFormatBufInHeap = formatLen + 1 > sizeof(curFormatBuf);
    char *curFormat = isCurFormatBufInHeap ? malloc(formatLen + 1) : &curFormatBuf;

    memcpy(curFormat, format, formatLen);
    curFormat[formatLen] = 0;

    // Special case: %v formatter - convert argument of any type to its string representation
    char reprBuf[sizeof(StrDimensions) + DEFAULT_STR_LEN + 1];
    bool isReprBufInHeap = false;
    char *dimsAndRepr = NULL;

    if (hasAnyTypeFormatter)
    {
        // %hhv -> %  s
        // %hv  -> % s
        // %v   -> %s
        // %lv  -> % s
        // %llv -> %  s

        curFormat[typeLetterPos] = 's';
        if (formatStringTypeSize != FORMAT_SIZE_NORMAL && typeLetterPos - 1 >= 0)
            curFormat[typeLetterPos - 1] = ' ';
        if ((formatStringTypeSize == FORMAT_SIZE_LONG_LONG || formatStringTypeSize == FORMAT_SIZE_SHORT_SHORT) && typeLetterPos - 2 >= 0)
            curFormat[typeLetterPos - 2] = ' ';

        const bool pretty = formatStringTypeSize == FORMAT_SIZE_LONG_LONG;
        const bool dereferenced = formatStringTypeSize == FORMAT_SIZE_LONG || formatStringTypeSize == FORMAT_SIZE_LONG_LONG;

        const int reprLen = doFillReprBuf(&value, type, NULL, 0, 0, pretty, dereferenced, error);  // Predict buffer length

        isReprBufInHeap = sizeof(StrDimensions) + reprLen + 1 > sizeof(reprBuf);
        dimsAndRepr = isReprBufInHeap ? malloc(sizeof(StrDimensions) + reprLen + 1) : &reprBuf;

        StrDimensions dims = {.len = reprLen, .capacity = reprLen + 1};
        *(StrDimensions *)dimsAndRepr = dims;

        char *repr = dimsAndRepr + sizeof(StrDimensions);
        repr[reprLen] = 0;

        doFillReprBuf(&value, type, repr, reprLen + 1, 0, pretty, dereferenced, error);            // Fill buffer

        value.ptrVal = repr;
        typeKind = TYPE_STR;
    }

    // Predict buffer length for sprintf() and reallocate it if needed
    int len = 0;
    if (string)
    {
        len = doPrintSlot(true, NULL, 0, curFormat, value, typeKind, error);

        const bool inPlace = stream && getStrDims(stream)->capacity >= prevLen + len + 1;
        if (inPlace)
        {
            getStrDims(stream)->len = prevLen + len;
        }
        else
        {
            char *newStream = doAllocStr(pages, prevLen + len, error);
            if (stream)
                memcpy(newStream, stream, prevLen);
            newStream[prevLen] = 0;

            // Decrease old string ref count
            Type strType = {.kind = TYPE_STR};
            doChangeRefCntImpl(fiber, pages, stream, &strType, TOK_MINUSMINUS);

            stream = newStream;
        }

        len = doPrintSlot(true, (char *)stream + prevLen, len + 1, curFormat, value, typeKind, error);
    }
    else
        len = doPrintSlot(false, stream, INT_MAX, curFormat, value, typeKind, error);

    fiber->top[STACK_OFFSET_FORMAT].ptrVal = (char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal + formatLen;
    fiber->top[STACK_OFFSET_COUNT].intVal += len;
    fiber->top[STACK_OFFSET_STREAM].ptrVal = stream;

    fiber->top++;   // Remove value

    if (isCurFormatBufInHeap)
        free(curFormat);

    if (isReprBufInHeap)
        free(dimsAndRepr);
}


static FORCE_INLINE void doBuiltinScanf(Fiber *fiber, HeapPages *pages, bool console, bool string, Error *error)
{
    void *stream       = console ? stdin : (void *)fiber->top[STACK_OFFSET_STREAM].ptrVal;
    const char *format = (const char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal;
    Slot value         = fiber->top[STACK_OFFSET_VALUE];

    Type *type         = fiber->code[fiber->ip].type;
    TypeKind typeKind  = type->kind;

    if (!stream || (!fiber->fileSystemEnabled && !console && !string))
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "scanf() source is null");

    if (!format)
        format = doGetEmptyStr();

    int formatLen = -1, typeLetterPos = -1;
    TypeKind expectedTypeKind = TYPE_NONE;
    FormatStringTypeSize formatStringTypeSize = FORMAT_SIZE_NORMAL;

    doCheckFormatString(format, &formatLen, &typeLetterPos, &expectedTypeKind, &formatStringTypeSize, error);

    if (typeKind != expectedTypeKind || expectedTypeKind == TYPE_INTERFACE)
    {
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Incompatible types %s and %s in scanf()", typeKindSpelling(expectedTypeKind), typeKindSpelling(typeKind));
    }

    char curFormatBuf[DEFAULT_STR_LEN + 1];
    bool isCurFormatBufInHeap = formatLen + 2 + 1 > sizeof(curFormatBuf);                   // + 2 for "%n"
    char *curFormat = isCurFormatBufInHeap ? malloc(formatLen + 2 + 1) : &curFormatBuf;

    memcpy(curFormat, format, formatLen);
    curFormat[formatLen + 0] = '%';
    curFormat[formatLen + 1] = 'n';
    curFormat[formatLen + 2] = '\0';

    int len = 0, cnt = 0;

    if (typeKind == TYPE_VOID)
        cnt = fsscanf(string, stream, curFormat, &len);
    else
    {
        if (!value.ptrVal)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "scanf() destination is null");

        // Strings need special handling, as the required buffer size is unknown
        if (typeKind == TYPE_STR)
        {
            char *src = fsscanfString(string, stream, &len);
            char **dest = (char **)value.ptrVal;

            // Decrease old string ref count
            Type destType = {.kind = TYPE_STR};
            doChangeRefCntImpl(fiber, pages, *dest, &destType, TOK_MINUSMINUS);

            // Allocate new string
            *dest = doAllocStr(pages, strlen(src), error);
            strcpy(*dest, src);
            free(src);

            cnt = (*dest)[0] ? 1 : 0;
        }
        else
            cnt = fsscanf(string, stream, curFormat, (void *)value.ptrVal, &len);
    }

    fiber->top[STACK_OFFSET_FORMAT].ptrVal = (char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal + formatLen;
    fiber->top[STACK_OFFSET_COUNT].intVal += cnt;
    if (string)
        fiber->top[STACK_OFFSET_STREAM].ptrVal = (char *)fiber->top[STACK_OFFSET_STREAM].ptrVal + len;

    fiber->top++;   // Remove value

    if (isCurFormatBufInHeap)
        free(curFormat);
}


// fn new(type: Type, size: int [, expr: type]): ^type
static FORCE_INLINE void doBuiltinNew(Fiber *fiber, HeapPages *pages, Error *error)
{
    int size     = (fiber->top++)->intVal;
    Type *type   = fiber->code[fiber->ip].type;

    // For dynamic arrays, we mark with type the data chunk, not the header chunk
    if (type && type->kind == TYPE_DYNARRAY)
        type = NULL;

    void *result = chunkAlloc(pages, size, type, NULL, false, error);

    (--fiber->top)->ptrVal = result;
}


// fn make(type: Type, len: int): type
// fn make(type: Type): type
// fn make(type: Type, childFunc: fn()): type
static FORCE_INLINE void doBuiltinMake(Fiber *fiber, HeapPages *pages, Error *error)
{
    Type *type = fiber->code[fiber->ip].type;

    if (type->kind == TYPE_DYNARRAY)
    {
        DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
        int len = (fiber->top++)->intVal;

        doAllocDynArray(pages, result, type, len, error);
        (--fiber->top)->ptrVal = result;
    }
    else if (type->kind == TYPE_MAP)
    {
        Map *result = (Map *)(fiber->top++)->ptrVal;

        doAllocMap(pages, result, type, error);
        (--fiber->top)->ptrVal = result;
    }
    else if (type->kind == TYPE_FIBER)
    {
        Closure *childClosure = (Closure *)(fiber->top++)->ptrVal;

        Fiber *result = doAllocFiber(fiber, childClosure, type->base, pages, error);
        (--fiber->top)->ptrVal = result;
    }
    else
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type");
}


// fn makefromarr(src: [...]ItemType, len: int): []ItemType
static FORCE_INLINE void doBuiltinMakefromarr(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *dest = (DynArray *)(fiber->top++)->ptrVal;
    int len        = (fiber->top++)->intVal;
    void *src      = (fiber->top++)->ptrVal;

    Type *destType = fiber->code[fiber->ip].type;

    doAllocDynArray(pages, dest, destType, len, error);
    memcpy(dest->data, src, getDims(dest)->len * dest->itemSize);

    // Increase result items' ref counts, as if they have been assigned one by one
    Type staticArrayType = {.kind = TYPE_ARRAY, .base = dest->type->base, .numItems = getDims(dest)->len, .next = NULL};
    doChangeRefCntImpl(fiber, pages, dest->data, &staticArrayType, TOK_PLUSPLUS);

    (--fiber->top)->ptrVal = dest;
}


// fn makefromstr(src: str): []char
static FORCE_INLINE void doBuiltinMakefromstr(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *dest  = (DynArray   *)(fiber->top++)->ptrVal;
    const char *src = (const char *)(fiber->top++)->ptrVal;

    Type *destType = fiber->code[fiber->ip].type;

    if (!src)
        src = doGetEmptyStr();

    doAllocDynArray(pages, dest, destType, getStrDims(src)->len, error);
    memcpy(dest->data, src, getDims(dest)->len);

    (--fiber->top)->ptrVal = dest;
}


// fn maketoarr(src: []ItemType): [...]ItemType
static FORCE_INLINE void doBuiltinMaketoarr(Fiber *fiber, HeapPages *pages, Error *error)
{
    void *dest     = (fiber->top++)->ptrVal;
    DynArray *src  = (DynArray *)(fiber->top++)->ptrVal;

    Type *destType = fiber->code[fiber->ip].type;

    if (!src)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    memset(dest, 0, typeSizeNoCheck(destType));

    if (src->data)
    {
        if (getDims(src)->len > destType->numItems)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is too long");

        memcpy(dest, src->data, getDims(src)->len * src->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        doChangeRefCntImpl(fiber, pages, dest, destType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = dest;
}


// fn maketostr(src: char | []char): str
static FORCE_INLINE void doBuiltinMaketostr(Fiber *fiber, HeapPages *pages, Error *error)
{
    char *dest = doGetEmptyStr();

    Type *type = fiber->code[fiber->ip].type;
    if (type->kind == TYPE_CHAR)
    {
        // Character to string
        const char src = (char)((fiber->top++)->intVal);

        if (src)
        {
            dest = doAllocStr(pages, 1, error);
            dest[0] = src;
            dest[1] = 0;
        }
    }
    else
    {
        // Dynamic array to string
        DynArray *src  = (DynArray *)(fiber->top++)->ptrVal;

        if (!src)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

        if (src->data)
        {
            const int len = strlen((const char *)src->data);
            dest = doAllocStr(pages, len, error);
            memcpy(dest, src->data, len);
        }
    }

    (--fiber->top)->ptrVal = dest;
}


// fn copy(array: [] type): [] type
static FORCE_INLINE void doBuiltinCopyDynArray(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    if (!array)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    *result = *array;

    if (array->data)
    {
        doAllocDynArray(pages, result, array->type, getDims(array)->len, error);
        memmove((char *)result->data, (char *)array->data, getDims(array)->len * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = getDims(result)->len, .next = NULL};
        doChangeRefCntImpl(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn copy(m: map [keyType] type): map [keyType] type
static FORCE_INLINE void doBuiltinCopyMap(Fiber *fiber, HeapPages *pages, Error *error)
{
    Map *result = (Map *)(fiber->top++)->ptrVal;
    Map *map    = (Map *)(fiber->top++)->ptrVal;

    if (!map)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map is null");

    if (!map->root)
        *result = *map;
    else
    {
        result->type = map->type;
        result->root = doCopyMapNode(map, map->root, fiber, pages, error);
    }

    (--fiber->top)->ptrVal = result;
}


static FORCE_INLINE void doBuiltinCopy(Fiber *fiber, HeapPages *pages, Error *error)
{
    Type *type  = fiber->code[fiber->ip].type;
    if (type->kind == TYPE_DYNARRAY)
        doBuiltinCopyDynArray(fiber, pages, error);
    else
        doBuiltinCopyMap(fiber, pages, error);
}


// fn append(array: [] type, item: (^type | [] type), single: bool): [] type
static FORCE_INLINE void doBuiltinAppend(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    bool single      = (bool      )(fiber->top++)->intVal;
    void *item       = (fiber->top++)->ptrVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    Type *arrayType  = fiber->code[fiber->ip].type;

    if (!array)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    if (!array->data)
        doGetEmptyDynArray(array, arrayType);

    void *rhs = item;
    int rhsLen = 1;

    if (!single)
    {
        DynArray *rhsArray = item;

        if (!rhsArray)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

        if (!rhsArray->data)
            doGetEmptyDynArray(rhsArray, arrayType);

        rhs = rhsArray->data;
        rhsLen = getDims(rhsArray)->len;
    }

    int newLen = getDims(array)->len + rhsLen;

    if (newLen <= getDims(array)->capacity)
    {
        doChangeRefCntImpl(fiber, pages, array, array->type, TOK_PLUSPLUS);
        *result = *array;

        memmove((char *)result->data + getDims(array)->len * array->itemSize, (char *)rhs, rhsLen * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = rhsLen, .next = NULL};
        doChangeRefCntImpl(fiber, pages, (char *)result->data + getDims(array)->len * array->itemSize, &staticArrayType, TOK_PLUSPLUS);

        getDims(result)->len = newLen;
    }
    else
    {
        doAllocDynArray(pages, result, array->type, newLen, error);

        memmove((char *)result->data, (char *)array->data, getDims(array)->len * array->itemSize);
        memmove((char *)result->data + getDims(array)->len * array->itemSize, (char *)rhs, rhsLen * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = newLen, .next = NULL};
        doChangeRefCntImpl(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn insert(array: [] type, index: int, item: type): [] type
static FORCE_INLINE void doBuiltinInsert(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    void *item       = (fiber->top++)->ptrVal;
    int64_t index    = (fiber->top++)->intVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    Type *arrayType  = fiber->code[fiber->ip].type;

    if (!array)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    if (!array->data)
        doGetEmptyDynArray(array, arrayType);

    if (index < 0 || index > getDims(array)->len)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Index %lld is out of range 0...%lld", index, getDims(array)->len);

    if (getDims(array)->len + 1 <= getDims(array)->capacity)
    {
        doChangeRefCntImpl(fiber, pages, array, array->type, TOK_PLUSPLUS);
        *result = *array;

        memmove((char *)result->data + (index + 1) * result->itemSize, (char *)result->data + index * result->itemSize, (getDims(array)->len - index) * result->itemSize);
        memmove((char *)result->data + index * result->itemSize, (char *)item, result->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = 1, .next = NULL};
        doChangeRefCntImpl(fiber, pages, (char *)result->data + index * result->itemSize, &staticArrayType, TOK_PLUSPLUS);

        getDims(result)->len++;
    }
    else
    {
        doAllocDynArray(pages, result, array->type, getDims(array)->len + 1, error);

        memmove((char *)result->data, (char *)array->data, index * result->itemSize);
        memmove((char *)result->data + (index + 1) * result->itemSize, (char *)array->data + index * result->itemSize, (getDims(array)->len - index) * result->itemSize);
        memmove((char *)result->data + index * result->itemSize, (char *)item, result->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = getDims(result)->len, .next = NULL};
        doChangeRefCntImpl(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn delete(array: [] type, index: int): [] type
static FORCE_INLINE void doBuiltinDeleteDynArray(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    int64_t index    =             (fiber->top++)->intVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    if (!array || !array->data)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    if (index < 0 || index > getDims(array)->len - 1)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Index %lld is out of range 0...%lld", index, getDims(array)->len - 1);

    doChangeRefCntImpl(fiber, pages, array, array->type, TOK_PLUSPLUS);
    *result = *array;

    // Decrease result item's ref count
    Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = 1, .next = NULL};
    doChangeRefCntImpl(fiber, pages, (char *)result->data + index * result->itemSize, &staticArrayType, TOK_MINUSMINUS);

    memmove((char *)result->data + index * result->itemSize, (char *)result->data + (index + 1) * result->itemSize, (getDims(array)->len - index - 1) * result->itemSize);

    getDims(result)->len--;

    (--fiber->top)->ptrVal = result;
}


// fn delete(m: map [keyType] type, key: keyType): map [keyType] type
static FORCE_INLINE void doBuiltinDeleteMap(Fiber *fiber, HeapPages *pages, Error *error)
{
    Map *result = (Map *)(fiber->top++)->ptrVal;
    Slot key    = *fiber->top++;
    Map *map    = (Map *)(fiber->top++)->ptrVal;

    if (!map || !map->root)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map is null");

    MapNode **nodeInParent = doGetMapNode(map, key, false, pages, error);
    MapNode *node = *nodeInParent;

    if (node)
    {
        if (node->left && !node->right)
            *nodeInParent = node->left;
        else if (!node->left && node->right)
            *nodeInParent = node->right;
        else if (node->left && node->right)
        {
            MapNode **successorInParent = &node->right;
            while ((*successorInParent)->left)
                successorInParent = &(*successorInParent)->left;

            MapNode *successor = *successorInParent;
            *successorInParent = successor->right;

            successor->left = node->left;
            successor->right = node->right;

            *nodeInParent = successor;
        }
        else
            *nodeInParent = NULL;

        node->left = NULL;
        node->right = NULL;

        doChangeRefCntImpl(fiber, pages, node, typeMapNodePtr(map->type), TOK_MINUSMINUS);

        if (--map->root->len < 0)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map length is negative");
    }

    doChangeRefCntImpl(fiber, pages, map->root, typeMapNodePtr(map->type), TOK_PLUSPLUS);
    result->type = map->type;
    result->root = map->root;

    (--fiber->top)->ptrVal = result;
}


static FORCE_INLINE void doBuiltinDelete(Fiber *fiber, HeapPages *pages, Error *error)
{
    Type *type  = fiber->code[fiber->ip].type;
    if (type->kind == TYPE_DYNARRAY)
        doBuiltinDeleteDynArray(fiber, pages, error);
    else
        doBuiltinDeleteMap(fiber, pages, error);
}


// fn slice(array: [] type | str, startIndex [, endIndex]: int): [] type | str
static FORCE_INLINE void doBuiltinSlice(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result   = (DynArray *)(fiber->top++)->ptrVal;
    int64_t endIndex   = (fiber->top++)->intVal;
    int64_t startIndex = (fiber->top++)->intVal;
    void *arg          = (fiber->top++)->ptrVal;

    Type *argType  = fiber->code[fiber->ip].type;

    DynArray *array = NULL;
    const char *str = NULL;
    int64_t len = 0;

    if (result)
    {
        // Dynamic array
        array = (DynArray *)arg;

        if (!array)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

        if (!array->data)
            doGetEmptyDynArray(array, argType);

        len = getDims(array)->len;
    }
    else
    {
        // String
        str = (const char *)arg;
        if (!str)
            str = doGetEmptyStr();

        len = getStrDims(str)->len;
    }

    // Missing end index means the end of the array
    if (endIndex == INT_MIN)
        endIndex = len;

    // Negative end index is counted from the end of the array
    if (endIndex < 0)
        endIndex += len;

    if (startIndex < 0)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Index %lld is out of range 0...%lld", startIndex, len);

    if (endIndex < startIndex || endIndex > len)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Index %lld is out of range %lld...%lld", endIndex, startIndex, len);

    if (result)
    {
        // Dynamic array
        doAllocDynArray(pages, result, array->type, endIndex - startIndex, error);

        memcpy((char *)result->data, (char *)array->data + startIndex * result->itemSize, getDims(result)->len * result->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = getDims(result)->len, .next = NULL};
        doChangeRefCntImpl(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);

        (--fiber->top)->ptrVal = result;
    }
    else
    {
        // String
        char *substr = doAllocStr(pages, endIndex - startIndex, error);
        memcpy(substr, &str[startIndex], endIndex - startIndex);
        substr[endIndex - startIndex] = 0;

        (--fiber->top)->ptrVal = substr;
    }
}


// fn sort(array: [] type, compare: fn (a, b: ^type): int)
typedef struct
{
    Fiber *fiber;
    Closure *compare;
    Type *compareType;
} CompareContext;


static int qsortCompare(const void *a, const void *b, void *context)
{
    Fiber *fiber      = ((CompareContext *)context)->fiber;
    Closure *compare  = ((CompareContext *)context)->compare;
    Type *compareType = ((CompareContext *)context)->compareType;

    Signature *compareSig = &compareType->field[0]->type->sig;

    // Push upvalues
    fiber->top -= sizeof(Interface) / sizeof(Slot);
    *(Interface *)fiber->top = compare->upvalue;
    doChangeRefCntImpl(fiber, &fiber->vm->pages, fiber->top, compareSig->param[0]->type, TOK_PLUSPLUS);

    // Push pointers to values to be compared
    (--fiber->top)->ptrVal = (void *)a;
    doChangeRefCntImpl(fiber, &fiber->vm->pages, fiber->top->ptrVal, compareSig->param[1]->type, TOK_PLUSPLUS);

    (--fiber->top)->ptrVal = (void *)b;
    doChangeRefCntImpl(fiber, &fiber->vm->pages, fiber->top->ptrVal, compareSig->param[2]->type, TOK_PLUSPLUS);

    // Push 'return from VM' signal as return address
    (--fiber->top)->intVal = VM_RETURN_FROM_VM;

    // Call the compare function
    int ip = fiber->ip;
    fiber->ip = compare->entryOffset;
    vmLoop(fiber->vm);
    fiber->ip = ip;

    return fiber->reg[VM_REG_RESULT].intVal;
}


static FORCE_INLINE void doBuiltinSort(Fiber *fiber, HeapPages *pages, Error *error)
{
    Type *compareType = (Type *)(fiber->top++)->ptrVal;
    Closure *compare  = (Closure *)(fiber->top++)->ptrVal;
    DynArray *array   = (DynArray *)(fiber->top++)->ptrVal;

    if (!array)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    if (!compare || compare->entryOffset <= 0)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Called function is not defined");

    if (array->data && getDims(array)->len > 0)
    {
        CompareContext context = {fiber, compare, compareType};

        const int numTempSlots = align(array->itemSize, sizeof(Slot)) / sizeof(Slot);
        fiber->top -= numTempSlots;

        qsortEx((char *)array->data, (char *)array->data + array->itemSize * (getDims(array)->len - 1), array->itemSize, qsortCompare, &context, fiber->top);

        fiber->top += numTempSlots;
    }
}


// fn sort(array: [] type, ascending: bool [, ident])
typedef struct
{
    TypeKind comparedTypeKind;
    int64_t offset;
    bool ascending;
    Error *error;
} FastCompareContext;


static int qsortFastCompare(const void *a, const void *b, void *context)
{
    FastCompareContext *fastCompareContext = (FastCompareContext *)context;

    int sign = fastCompareContext->ascending ? 1 : -1;
    const char *lhs = (const char *)a + fastCompareContext->offset;
    const char *rhs = (const char *)b + fastCompareContext->offset;
    Error *error = fastCompareContext->error;

    switch (fastCompareContext->comparedTypeKind)
    {
        case TYPE_INT8:     {int64_t diff = (int64_t)(*(int8_t        *)lhs) - (int64_t)(*(int8_t        *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_INT16:    {int64_t diff = (int64_t)(*(int16_t       *)lhs) - (int64_t)(*(int16_t       *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_INT32:    {int64_t diff = (int64_t)(*(int32_t       *)lhs) - (int64_t)(*(int32_t       *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_INT:      {int64_t diff =          (*(int64_t       *)lhs) -          (*(int64_t       *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_UINT8:    {int64_t diff = (int64_t)(*(uint8_t       *)lhs) - (int64_t)(*(uint8_t       *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_UINT16:   {int64_t diff = (int64_t)(*(uint16_t      *)lhs) - (int64_t)(*(uint16_t      *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_UINT32:   {int64_t diff = (int64_t)(*(uint32_t      *)lhs) - (int64_t)(*(uint32_t      *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_UINT:     {int64_t diff =          (*(uint64_t      *)lhs) -          (*(uint64_t      *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_BOOL:     {int64_t diff = (int64_t)(*(bool          *)lhs) - (int64_t)(*(bool          *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_CHAR:     {int64_t diff = (int64_t)(*(unsigned char *)lhs) - (int64_t)(*(unsigned char *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_REAL32:   {double  diff = (double )(*(float         *)lhs) - (double )(*(float         *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_REAL:     {double  diff =          (*(double        *)lhs) -          (*(double        *)rhs);    return diff == 0 ? 0 : diff > 0 ? sign : -sign;}
        case TYPE_STR:
        {
            char *lhsStr = *(char **)lhs;
            if (!lhsStr)
                lhsStr = doGetEmptyStr();

            char *rhsStr = *(char **)rhs;
            if (!rhsStr)
                rhsStr = doGetEmptyStr();

            doCheckStr(lhsStr, error);
            doCheckStr(rhsStr, error);

            return strcmp(lhsStr, rhsStr) * sign;
        }
        default:
        {
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type");
            return 0;
        }
    }
}


static FORCE_INLINE void doBuiltinSortfast(Fiber *fiber, HeapPages *pages, Error *error)
{
    int64_t offset = (fiber->top++)->intVal;
    bool ascending = (bool)(fiber->top++)->intVal;
    DynArray *array = (DynArray *)(fiber->top++)->ptrVal;
    TypeKind comparedTypeKind = fiber->code[fiber->ip].typeKind;

    if (!array)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    if (array->data && getDims(array)->len > 0)
    {
        FastCompareContext context = {comparedTypeKind, offset, ascending, error};

        const int numTempSlots = align(array->itemSize, sizeof(Slot)) / sizeof(Slot);
        fiber->top -= numTempSlots;

        qsortEx((char *)array->data, (char *)array->data + array->itemSize * (getDims(array)->len - 1), array->itemSize, qsortFastCompare, &context, fiber->top);

        fiber->top += numTempSlots;
    }
}


static FORCE_INLINE void doBuiltinLen(Fiber *fiber, Error *error)
{
    switch (fiber->code[fiber->ip].typeKind)
    {
        // Done at compile time for arrays
        case TYPE_DYNARRAY:
        {
            const DynArray *array = (DynArray *)(fiber->top->ptrVal);
            if (!array)
                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

            fiber->top->intVal = array->data ? getDims(array)->len : 0;
            break;
        }
        case TYPE_STR:
        {
            const char *str = (const char *)fiber->top->ptrVal;
            doCheckStr(str, error);
            fiber->top->intVal = str ? getStrDims(str)->len : 0;
            break;
        }
        case TYPE_MAP:
        {
            Map *map = (Map *)(fiber->top->ptrVal);
            if (!map)
                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map is null");

            fiber->top->intVal =  map->root ? map->root->len : 0;
            break;
        }
        default:
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type"); return;
    }
}


static FORCE_INLINE void doBuiltinCap(Fiber *fiber, Error *error)
{
    const DynArray *array = (DynArray *)(fiber->top->ptrVal);
    if (!array)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    fiber->top->intVal = array->data ? getDims(array)->capacity : 0;
}


static FORCE_INLINE void doBuiltinSizeofself(Fiber *fiber, Error *error)
{
    Interface *interface = (Interface *)fiber->top->ptrVal;

    int size = 0;
    if (interface->selfType)
        size = typeSizeNoCheck(interface->selfType->base);

    fiber->top->intVal = size;
}


static FORCE_INLINE void doBuiltinSelfhasptr(Fiber *fiber, Error *error)
{
    Interface *interface = (Interface *)fiber->top->ptrVal;

    bool hasPtr = false;
    if (interface->selfType)
        hasPtr = typeGarbageCollected(interface->selfType->base);

    fiber->top->intVal = hasPtr;
}


static FORCE_INLINE void doBuiltinSelftypeeq(Fiber *fiber, Error *error)
{
    Interface *right = (Interface *)(fiber->top++)->ptrVal;
    Interface *left  = (Interface *)(fiber->top++)->ptrVal;

    bool typesEq = false;
    if (left->selfType && right->selfType)
        typesEq = typeEquivalent(left->selfType->base, right->selfType->base);

    (--fiber->top)->intVal = typesEq;
}


static FORCE_INLINE void doBuiltinValid(Fiber *fiber, Error *error)
{
    bool isValid = true;

    switch (fiber->code[fiber->ip].typeKind)
    {
        case TYPE_DYNARRAY:
        {
            DynArray *array = (DynArray *)fiber->top->ptrVal;
            isValid = array && array->data;
            break;
        }
        case TYPE_MAP:
        {
            Map *map = (Map *)fiber->top->ptrVal;
            isValid = map && map->root;
            break;
        }
        case TYPE_INTERFACE:
        {
            Interface *interface = (Interface *)fiber->top->ptrVal;
            isValid = interface && interface->self;
            break;
        }
        case TYPE_FN:
        {
            int entryOffset = fiber->top->intVal;
            isValid = entryOffset > 0;
            break;
        }
        case TYPE_CLOSURE:
        {
            Closure *closure = (Closure *)fiber->top->ptrVal;
            isValid = closure && closure->entryOffset > 0;
            break;
        }
        case TYPE_FIBER:
        {
            Fiber *child = (Fiber *)fiber->top->ptrVal;
            isValid = child && child->alive;
            break;
        }
        default:
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type"); return;
    }

    fiber->top->intVal = isValid;
}


// fn validkey(m: map [keyType] type, key: keyType): bool
static FORCE_INLINE void doBuiltinValidkey(Fiber *fiber, HeapPages *pages, Error *error)
{
    Slot key  = *fiber->top++;
    Map *map  = (Map *)(fiber->top++)->ptrVal;

    if (!map)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map is null");

    bool isValid = false;

    if (map->root)
    {
        MapNode *node = *doGetMapNode(map, key, false, pages, error);
        isValid = node && node->data;
    }

    (--fiber->top)->intVal = isValid;
}


// fn keys(m: map [keyType] type): []keyType
static FORCE_INLINE void doBuiltinKeys(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    Type *resultType = (Type *)(fiber->top++)->ptrVal;
    Map *map         = (Map *)(fiber->top++)->ptrVal;

    if (!map)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map is null");

    doAllocDynArray(pages, result, resultType, map->root ? map->root->len : 0, error);

    if (map->root)
    {
        doGetMapKeys(map, result->data, error);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = getDims(result)->len, .next = NULL};
        doChangeRefCntImpl(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn resume([child: fiber])
static FORCE_INLINE void doBuiltinResume(Fiber *fiber, Fiber **newFiber, HeapPages *pages, Error *error)
{
    Fiber *child = (Fiber *)(fiber->top++)->ptrVal;
    if (child && child->alive)
        *newFiber = child;
    else if (!child && fiber->parent)
        *newFiber = fiber->parent;
    else
        *newFiber = fiber;
}


// fn memusage(): int
static FORCE_INLINE void doBuiltinMemusage(Fiber *fiber, HeapPages *pages, Error *error)
{
    (--fiber->top)->intVal = pages->totalSize;
}


// fn exit(code: int, msg: str = "")
static FORCE_INLINE void doBuiltinExit(Fiber *fiber, Error *error)
{
    fiber->alive = false;
    char *msg = (char *)(fiber->top++)->ptrVal;

    error->runtimeHandler(error->context, fiber->top->intVal, "%s", msg);
}


static FORCE_INLINE void doPush(Fiber *fiber, Error *error)
{
    (--fiber->top)->intVal = fiber->code[fiber->ip].operand.intVal;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doPushZero(Fiber *fiber, Error *error)
{
    int slots = fiber->code[fiber->ip].operand.intVal;

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Stack overflow");

    fiber->top -= slots;
    memset(fiber->top, 0, slots * sizeof(Slot));

    fiber->ip++;
}


static FORCE_INLINE void doPushLocalPtr(Fiber *fiber)
{
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->code[fiber->ip].operand.intVal;
    fiber->ip++;
}


static FORCE_INLINE void doPushLocalPtrZero(Fiber *fiber)
{
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->code[fiber->ip].operand.int32Val[0];
    int size = fiber->code[fiber->ip].operand.int32Val[1];
    memset(fiber->top->ptrVal, 0, size);
    fiber->ip++;
}


static FORCE_INLINE void doPushLocal(Fiber *fiber, Error *error)
{
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->code[fiber->ip].operand.intVal;
    doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);
    fiber->ip++;
}


static FORCE_INLINE void doPushReg(Fiber *fiber)
{
    (--fiber->top)->intVal = fiber->reg[fiber->code[fiber->ip].operand.intVal].intVal;
    fiber->ip++;
}


static FORCE_INLINE void doPushUpvalue(Fiber *fiber, HeapPages *pages, Error *error)
{
    Closure *closure = (Closure *)(fiber->top++)->ptrVal;

    (--fiber->top)->intVal = closure->entryOffset;
    (--fiber->top)->ptrVal = &closure->upvalue;

    fiber->ip++;
}


static FORCE_INLINE void doPop(Fiber *fiber)
{
    fiber->top += fiber->code[fiber->ip].operand.intVal;
    fiber->ip++;
}


static FORCE_INLINE void doPopReg(Fiber *fiber)
{
    fiber->reg[fiber->code[fiber->ip].operand.intVal].intVal = (fiber->top++)->intVal;
    fiber->ip++;
}


static FORCE_INLINE void doDup(Fiber *fiber)
{
    Slot val = *fiber->top;
    *(--fiber->top) = val;
    fiber->ip++;
}


static FORCE_INLINE void doSwap(Fiber *fiber)
{
    doSwapImpl(fiber->top);
    fiber->ip++;
}


static FORCE_INLINE void doZero(Fiber *fiber)
{
    void *ptr = (fiber->top++)->ptrVal;
    int size = fiber->code[fiber->ip].operand.intVal;
    memset(ptr, 0, size);
    fiber->ip++;
}


static FORCE_INLINE void doDeref(Fiber *fiber, Error *error)
{
    doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);
    fiber->ip++;
}


static FORCE_INLINE void doAssign(Fiber *fiber, Error *error)
{
    if (fiber->code[fiber->ip].inlineOpcode == OP_SWAP)
        doSwapImpl(fiber->top);

    Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;

    doAssignImpl(lhs, rhs, fiber->code[fiber->ip].typeKind, fiber->code[fiber->ip].operand.intVal, error);
    fiber->ip++;
}


static FORCE_INLINE void doAssignParam(Fiber *fiber, Error *error)
{
    Slot rhs = *fiber->top;

    int paramSize = fiber->code[fiber->ip].operand.intVal;
    int paramSlots = align(paramSize, sizeof(Slot)) / sizeof(Slot);
    if (paramSlots != 1)
    {
        if (fiber->top - (paramSlots - 1) - fiber->stack < VM_MIN_FREE_STACK)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Stack overflow");

        fiber->top -= paramSlots - 1;
    }

    doAssignImpl(fiber->top, rhs, fiber->code[fiber->ip].typeKind, paramSize, error);
    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCnt(Fiber *fiber, HeapPages *pages)
{
    void *ptr         = fiber->top->ptrVal;
    TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    Type *type        = fiber->code[fiber->ip].type;

    doChangeRefCntImpl(fiber, pages, ptr, type, tokKind);

    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCntGlobal(Fiber *fiber, HeapPages *pages, Error *error)
{
    TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    Type *type        = fiber->code[fiber->ip].type;
    void *ptr         = fiber->code[fiber->ip].operand.ptrVal;

    Slot slot = {.ptrVal = ptr};

    doDerefImpl(&slot, type->kind, error);
    doChangeRefCntImpl(fiber, pages, slot.ptrVal, type, tokKind);

    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCntLocal(Fiber *fiber, HeapPages *pages, Error *error)
{
    TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    Type *type        = fiber->code[fiber->ip].type;
    int offset        = fiber->code[fiber->ip].operand.intVal;

    Slot slot = {.ptrVal = (int8_t *)fiber->base + offset};

    doDerefImpl(&slot, type->kind, error);
    doChangeRefCntImpl(fiber, pages, slot.ptrVal, type, tokKind);

    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCntAssign(Fiber *fiber, HeapPages *pages, Error *error)
{
    if (fiber->code[fiber->ip].inlineOpcode == OP_SWAP)
        doSwapImpl(fiber->top);

    Slot rhs   = *fiber->top++;
    void *lhs  = (fiber->top++)->ptrVal;
    Type *type = fiber->code[fiber->ip].type;

    // Increase right-hand side ref count
    if (fiber->code[fiber->ip].tokKind != TOK_MINUSMINUS)      // "--" means that the right-hand side ref count should not be increased
        doChangeRefCntImpl(fiber, pages, rhs.ptrVal, type, TOK_PLUSPLUS);

    // Decrease left-hand side ref count
    Slot lhsDeref = {.ptrVal = lhs};
    doDerefImpl(&lhsDeref, type->kind, error);
    doChangeRefCntImpl(fiber, pages, lhsDeref.ptrVal, type, TOK_MINUSMINUS);

    doAssignImpl(lhs, rhs, type->kind, typeSizeNoCheck(type), error);
    fiber->ip++;
}


static FORCE_INLINE void doUnary(Fiber *fiber, Error *error)
{
    if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  break;
            case TOK_MINUS: fiber->top->realVal = -fiber->top->realVal; break;
            default:        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    else
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:       break;
            case TOK_MINUS:      fiber->top->intVal = -fiber->top->intVal; break;
            case TOK_NOT:        fiber->top->intVal = !fiber->top->intVal; break;
            case TOK_XOR:        fiber->top->intVal = ~fiber->top->intVal; break;

            case TOK_PLUSPLUS:
            {
                void *ptr = (fiber->top++)->ptrVal;
                switch (fiber->code[fiber->ip].typeKind)
                {
                    case TYPE_INT8:   (*(int8_t   *)ptr)++; break;
                    case TYPE_INT16:  (*(int16_t  *)ptr)++; break;
                    case TYPE_INT32:  (*(int32_t  *)ptr)++; break;
                    case TYPE_INT:    (*(int64_t  *)ptr)++; break;
                    case TYPE_UINT8:  (*(uint8_t  *)ptr)++; break;
                    case TYPE_UINT16: (*(uint16_t *)ptr)++; break;
                    case TYPE_UINT32: (*(uint32_t *)ptr)++; break;
                    case TYPE_UINT:   (*(uint64_t *)ptr)++; break;
                    // Structured, boolean, char and real types are not incremented/decremented
                    default:          error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type"); return;
                }
            break;
            }

            case TOK_MINUSMINUS:
            {
                void *ptr = (fiber->top++)->ptrVal;
                switch (fiber->code[fiber->ip].typeKind)
                {
                    case TYPE_INT8:   (*(int8_t   *)ptr)--; break;
                    case TYPE_INT16:  (*(int16_t  *)ptr)--; break;
                    case TYPE_INT32:  (*(int32_t  *)ptr)--; break;
                    case TYPE_INT:    (*(int64_t  *)ptr)--; break;
                    case TYPE_UINT8:  (*(uint8_t  *)ptr)--; break;
                    case TYPE_UINT16: (*(uint16_t *)ptr)--; break;
                    case TYPE_UINT32: (*(uint32_t *)ptr)--; break;
                    case TYPE_UINT:   (*(uint64_t *)ptr)--; break;
                    // Structured, boolean, char and real types are not incremented/decremented
                    default:          error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal type"); return;
                }
            break;
            }

            default: error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    fiber->ip++;
}


static FORCE_INLINE void doBinary(Fiber *fiber, HeapPages *pages, Error *error)
{
    Slot rhs = *fiber->top++;

    if (fiber->code[fiber->ip].typeKind == TYPE_PTR)
    {
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_EQEQ:      fiber->top->intVal = fiber->top->ptrVal == rhs.ptrVal; break;
            case TOK_NOTEQ:     fiber->top->intVal = fiber->top->ptrVal != rhs.ptrVal; break;

            default:            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    }
    else if (fiber->code[fiber->ip].typeKind == TYPE_STR)
    {
        char *lhsStr = (char *)fiber->top->ptrVal;
        if (!lhsStr)
            lhsStr = doGetEmptyStr();

        char *rhsStr = (char *)rhs.ptrVal;
        if (!rhsStr)
            rhsStr = doGetEmptyStr();

        doCheckStr(lhsStr, error);
        doCheckStr(rhsStr, error);

        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:
            case TOK_PLUSEQ:
            {
                const int lhsLen = getStrDims(lhsStr)->len;
                const int rhsLen = getStrDims(rhsStr)->len;

                const bool inPlace = fiber->code[fiber->ip].tokKind == TOK_PLUSEQ && getStrDims(lhsStr)->capacity >= lhsLen + rhsLen + 1;

                char *buf = NULL;
                if (inPlace)
                {
                    buf = lhsStr;
                    Type strType = {.kind = TYPE_STR};
                    doChangeRefCntImpl(fiber, pages, buf, &strType, TOK_PLUSPLUS);
                }
                else
                {
                    buf = doAllocStr(pages, lhsLen + rhsLen, error);
                    memmove(buf, lhsStr, lhsLen);
                }

                memmove(buf + lhsLen, rhsStr, rhsLen + 1);
                getStrDims(buf)->len = lhsLen + rhsLen;

                fiber->top->ptrVal = buf;
                break;
            }

            case TOK_EQEQ:      fiber->top->intVal = strcmp(lhsStr, rhsStr) == 0; break;
            case TOK_NOTEQ:     fiber->top->intVal = strcmp(lhsStr, rhsStr) != 0; break;
            case TOK_GREATER:   fiber->top->intVal = strcmp(lhsStr, rhsStr)  > 0; break;
            case TOK_LESS:      fiber->top->intVal = strcmp(lhsStr, rhsStr)  < 0; break;
            case TOK_GREATEREQ: fiber->top->intVal = strcmp(lhsStr, rhsStr) >= 0; break;
            case TOK_LESSEQ:    fiber->top->intVal = strcmp(lhsStr, rhsStr) <= 0; break;

            default:            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    }
    else if (fiber->code[fiber->ip].typeKind == TYPE_ARRAY || fiber->code[fiber->ip].typeKind == TYPE_STRUCT)
    {
        const int structSize = fiber->code[fiber->ip].operand.intVal;

        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_EQEQ:      fiber->top->intVal = memcmp(fiber->top->ptrVal, rhs.ptrVal, structSize) == 0; break;
            case TOK_NOTEQ:     fiber->top->intVal = memcmp(fiber->top->ptrVal, rhs.ptrVal, structSize) != 0; break;

            default:            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    }
    else if (typeKindReal(fiber->code[fiber->ip].typeKind))
    {
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  fiber->top->realVal += rhs.realVal; break;
            case TOK_MINUS: fiber->top->realVal -= rhs.realVal; break;
            case TOK_MUL:   fiber->top->realVal *= rhs.realVal; break;
            case TOK_DIV:
            {
                if (rhs.realVal == 0)
                    error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Division by zero");
                fiber->top->realVal /= rhs.realVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.realVal == 0)
                    error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Division by zero");
                fiber->top->realVal = fmod(fiber->top->realVal, rhs.realVal);
                break;
            }

            case TOK_EQEQ:      fiber->top->intVal = fiber->top->realVal == rhs.realVal; break;
            case TOK_NOTEQ:     fiber->top->intVal = fiber->top->realVal != rhs.realVal; break;
            case TOK_GREATER:   fiber->top->intVal = fiber->top->realVal >  rhs.realVal; break;
            case TOK_LESS:      fiber->top->intVal = fiber->top->realVal <  rhs.realVal; break;
            case TOK_GREATEREQ: fiber->top->intVal = fiber->top->realVal >= rhs.realVal; break;
            case TOK_LESSEQ:    fiber->top->intVal = fiber->top->realVal <= rhs.realVal; break;

            default:            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    }
    else if (fiber->code[fiber->ip].typeKind == TYPE_UINT)
    {
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  fiber->top->uintVal += rhs.uintVal; break;
            case TOK_MINUS: fiber->top->uintVal -= rhs.uintVal; break;
            case TOK_MUL:   fiber->top->uintVal *= rhs.uintVal; break;
            case TOK_DIV:
            {
                if (rhs.uintVal == 0)
                    error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Division by zero");
                fiber->top->uintVal /= rhs.uintVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.uintVal == 0)
                    error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Division by zero");
                fiber->top->uintVal %= rhs.uintVal;
                break;
            }

            case TOK_SHL:   fiber->top->uintVal <<= rhs.uintVal; break;
            case TOK_SHR:   fiber->top->uintVal >>= rhs.uintVal; break;
            case TOK_AND:   fiber->top->uintVal &= rhs.uintVal; break;
            case TOK_OR:    fiber->top->uintVal |= rhs.uintVal; break;
            case TOK_XOR:   fiber->top->uintVal ^= rhs.uintVal; break;

            case TOK_EQEQ:      fiber->top->uintVal = fiber->top->uintVal == rhs.uintVal; break;
            case TOK_NOTEQ:     fiber->top->uintVal = fiber->top->uintVal != rhs.uintVal; break;
            case TOK_GREATER:   fiber->top->uintVal = fiber->top->uintVal >  rhs.uintVal; break;
            case TOK_LESS:      fiber->top->uintVal = fiber->top->uintVal <  rhs.uintVal; break;
            case TOK_GREATEREQ: fiber->top->uintVal = fiber->top->uintVal >= rhs.uintVal; break;
            case TOK_LESSEQ:    fiber->top->uintVal = fiber->top->uintVal <= rhs.uintVal; break;

            default:            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    }
    else  // All ordinal types except TYPE_UINT
    {
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  fiber->top->intVal += rhs.intVal; break;
            case TOK_MINUS: fiber->top->intVal -= rhs.intVal; break;
            case TOK_MUL:   fiber->top->intVal *= rhs.intVal; break;
            case TOK_DIV:
            {
                if (rhs.intVal == 0)
                    error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Division by zero");
                fiber->top->intVal /= rhs.intVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.intVal == 0)
                    error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Division by zero");
                fiber->top->intVal %= rhs.intVal;
                break;
            }

            case TOK_SHL:   fiber->top->intVal <<= rhs.intVal; break;
            case TOK_SHR:   fiber->top->intVal >>= rhs.intVal; break;
            case TOK_AND:   fiber->top->intVal &= rhs.intVal; break;
            case TOK_OR:    fiber->top->intVal |= rhs.intVal; break;
            case TOK_XOR:   fiber->top->intVal ^= rhs.intVal; break;

            case TOK_EQEQ:      fiber->top->intVal = fiber->top->intVal == rhs.intVal; break;
            case TOK_NOTEQ:     fiber->top->intVal = fiber->top->intVal != rhs.intVal; break;
            case TOK_GREATER:   fiber->top->intVal = fiber->top->intVal >  rhs.intVal; break;
            case TOK_LESS:      fiber->top->intVal = fiber->top->intVal <  rhs.intVal; break;
            case TOK_GREATEREQ: fiber->top->intVal = fiber->top->intVal >= rhs.intVal; break;
            case TOK_LESSEQ:    fiber->top->intVal = fiber->top->intVal <= rhs.intVal; break;

            default:            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        }
    }

    fiber->ip++;
}


static FORCE_INLINE void doGetArrayPtr(Fiber *fiber, Error *error)
{
    int itemSize = fiber->code[fiber->ip].operand.int32Val[0];
    int len      = fiber->code[fiber->ip].operand.int32Val[1];
    int index    = (fiber->top++)->intVal;

    char *data = (char *)fiber->top->ptrVal;

    if (len >= 0)   // For arrays, nonnegative length must be explicitly provided
    {
        if (!data)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Array is null");
    }
    else            // For strings, negative length means that the actual string length is to be used
    {
        if (!data)
            data = doGetEmptyStr();
        doCheckStr(data, error);
        len = getStrDims(data)->len;
    }

    if (index < 0 || index > len - 1)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Index %d is out of range 0...%d", index, len - 1);

    fiber->top->ptrVal = data + itemSize * index;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doGetDynArrayPtr(Fiber *fiber, Error *error)
{
    int index       = (fiber->top++)->intVal;
    DynArray *array = (DynArray *)(fiber->top++)->ptrVal;

    if (!array || !array->data)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Dynamic array is null");

    int itemSize    = array->itemSize;
    int len         = getDims(array)->len;

    if (index < 0 || index > len - 1)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Index %d is out of range 0...%d", index, len - 1);

    (--fiber->top)->ptrVal = (char *)array->data + itemSize * index;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doGetMapPtr(Fiber *fiber, HeapPages *pages, Error *error)
{
    Slot key  = *fiber->top++;
    Map *map  = (Map *)(fiber->top++)->ptrVal;
    Type *mapType = fiber->code[fiber->ip].type;

    if (!map)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Map is null");

    if (!map->root)
        doAllocMap(pages, map, mapType, error);

    Type *keyType = typeMapKey(map->type);
    Type *itemType = typeMapItem(map->type);

    MapNode *node = *doGetMapNode(map, key, true, pages, error);
    if (!node->data)
    {
        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        node->key  = chunkAlloc(pages, typeSizeNoCheck(keyType),  keyType->kind  == TYPE_DYNARRAY ? NULL : keyType,  NULL, false, error);
        node->data = chunkAlloc(pages, typeSizeNoCheck(itemType), itemType->kind == TYPE_DYNARRAY ? NULL : itemType, NULL, false, error);

        // Increase key ref count
        if (typeGarbageCollected(keyType))
            doChangeRefCntImpl(fiber, pages, key.ptrVal, keyType, TOK_PLUSPLUS);

        doAssignImpl(node->key, key, keyType->kind, typeSizeNoCheck(keyType), error);
        map->root->len++;
    }

    (--fiber->top)->ptrVal = node->data;
    fiber->ip++;
}


static FORCE_INLINE void doGetFieldPtr(Fiber *fiber, Error *error)
{
    int fieldOffset = fiber->code[fiber->ip].operand.intVal;

    if (!fiber->top->ptrVal)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Array or structure is null");

    fiber->top->ptrVal = (char *)fiber->top->ptrVal + fieldOffset;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doAssertType(Fiber *fiber)
{
    Interface *interface  = (Interface *)(fiber->top++)->ptrVal;
    Type *type            = fiber->code[fiber->ip].type;

    (--fiber->top)->ptrVal = (interface->selfType && typeEquivalent(type, interface->selfType)) ? interface->self : NULL;
    fiber->ip++;
}


static FORCE_INLINE void doAssertRange(Fiber *fiber, Error *error)
{
    TypeKind typeKind = fiber->code[fiber->ip].typeKind;

    Const arg;
    if (typeKindReal(typeKind))
        arg.realVal = fiber->top->realVal;
    else
        arg.intVal = fiber->top->intVal;

    if (typeOverflow(typeKind, arg))
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Overflow of %s", typeKindSpelling(typeKind));

    fiber->ip++;
}


static FORCE_INLINE void doWeakenPtr(Fiber *fiber, HeapPages *pages)
{
    void *ptr = fiber->top->ptrVal;
    uint64_t weakPtr = 0;

    HeapPage *page = pageFind(pages, ptr, false);
    if (page)
    {
        HeapChunkHeader *chunk = pageGetChunkHeader(page, ptr);
        if (chunk->refCnt > 0 && !chunk->isStack)
        {
            int pageId = page->id;
            int pageOffset = (char *)ptr - (char *)page->ptr;
            weakPtr = ((uint64_t)pageId << 32) | pageOffset;
        }
    }

    fiber->top->weakPtrVal = weakPtr;
    fiber->ip++;
}


static FORCE_INLINE void doStrengthenPtr(Fiber *fiber, HeapPages *pages)
{
    uint64_t weakPtr = fiber->top->weakPtrVal;
    void *ptr = NULL;

    int pageId = (weakPtr >> 32) & 0x7FFFFFFF;
    HeapPage *page = pageFindById(pages, pageId);
    if (page)
    {
        int pageOffset = weakPtr & 0x7FFFFFFF;
        ptr = (char *)page->ptr + pageOffset;

        HeapChunkHeader * chunk = pageGetChunkHeader(page, ptr);
        if (chunk->refCnt == 0 || chunk->isStack)
            ptr = NULL;
    }

    fiber->top->ptrVal = ptr;
    fiber->ip++;
}


static FORCE_INLINE void doGoto(Fiber *fiber)
{
    fiber->ip = fiber->code[fiber->ip].operand.intVal;
}


static FORCE_INLINE void doGotoIf(Fiber *fiber)
{
    if ((fiber->top++)->intVal)
        fiber->ip = fiber->code[fiber->ip].operand.intVal;
    else
        fiber->ip++;
}


static FORCE_INLINE void doGotoIfNot(Fiber *fiber)
{
    if (!(fiber->top++)->intVal)
        fiber->ip = fiber->code[fiber->ip].operand.intVal;
    else
        fiber->ip++;
}


static FORCE_INLINE void doCall(Fiber *fiber, Error *error)
{
    // For direct calls, entry point address is stored in the instruction
    int entryOffset = fiber->code[fiber->ip].operand.intVal;

    // Push return address and go to the entry point
    (--fiber->top)->intVal = fiber->ip + 1;
    fiber->ip = entryOffset;
}


static FORCE_INLINE void doCallIndirect(Fiber *fiber, Error *error)
{
    // For indirect calls, entry point address is below the parameters on the stack
    int paramSlots = fiber->code[fiber->ip].operand.intVal;
    int entryOffset = (fiber->top + paramSlots)->intVal;

    if (entryOffset == 0)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Called function is not defined");

    // Push return address and go to the entry point
    (--fiber->top)->intVal = fiber->ip + 1;
    fiber->ip = entryOffset;
}


static FORCE_INLINE void doCallExtern(Fiber *fiber, Error *error)
{
    ExternFunc fn = (ExternFunc)fiber->code[fiber->ip].operand.ptrVal;

    Slot *paramLayout = fiber->base - 2;
    paramLayout->ptrVal = (fiber->top++)->ptrVal;

    fiber->reg[VM_REG_RESULT].ptrVal = error->context;    // Upon entry, the result slot stores the Umka instance

    int ip = fiber->ip;
    fn(fiber->base + 2, &fiber->reg[VM_REG_RESULT]);      // + 2 for old base pointer and return address
    fiber->ip = ip;

    fiber->ip++;
}


static FORCE_INLINE void doCallBuiltin(Fiber *fiber, Fiber **newFiber, HeapPages *pages, Error *error)
{
    // Preserve instruction pointer, in case any of the standard calls end up calling Umka again
    int ip = fiber->ip;

    BuiltinFunc builtin = fiber->code[fiber->ip].operand.builtinVal;
    TypeKind typeKind   = fiber->code[fiber->ip].typeKind;

    switch (builtin)
    {
        // I/O
        case BUILTIN_PRINTF:        doBuiltinPrintf(fiber, pages, true,  false, error); break;
        case BUILTIN_FPRINTF:       doBuiltinPrintf(fiber, pages, false, false, error); break;
        case BUILTIN_SPRINTF:       doBuiltinPrintf(fiber, pages, false, true,  error); break;
        case BUILTIN_SCANF:         doBuiltinScanf (fiber, pages, true,  false, error); break;
        case BUILTIN_FSCANF:        doBuiltinScanf (fiber, pages, false, false, error); break;
        case BUILTIN_SSCANF:        doBuiltinScanf (fiber, pages, false, true,  error); break;

        // Math
        case BUILTIN_REAL:
        case BUILTIN_REAL_LHS:
        {
            const int depth = (builtin == BUILTIN_REAL_LHS) ? 1 : 0;
            if (typeKind == TYPE_UINT)
                (fiber->top + depth)->realVal = (fiber->top + depth)->uintVal;
            else
                (fiber->top + depth)->realVal = (fiber->top + depth)->intVal;
            break;
        }
        case BUILTIN_ROUND:         fiber->top->intVal = (int64_t)round(fiber->top->realVal); break;
        case BUILTIN_TRUNC:         fiber->top->intVal = (int64_t)trunc(fiber->top->realVal); break;
        case BUILTIN_CEIL:          fiber->top->intVal = (int64_t)ceil (fiber->top->realVal); break;
        case BUILTIN_FLOOR:         fiber->top->intVal = (int64_t)floor(fiber->top->realVal); break;
        case BUILTIN_FABS:          fiber->top->realVal = fabs(fiber->top->realVal); break;
        case BUILTIN_SQRT:
        {
            if (fiber->top->realVal < 0)
                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "sqrt() domain error");
            fiber->top->realVal = sqrt(fiber->top->realVal);
            break;
        }
        case BUILTIN_SIN:           fiber->top->realVal = sin (fiber->top->realVal); break;
        case BUILTIN_COS:           fiber->top->realVal = cos (fiber->top->realVal); break;
        case BUILTIN_ATAN:          fiber->top->realVal = atan(fiber->top->realVal); break;
        case BUILTIN_ATAN2:
        {
            double x = (fiber->top++)->realVal;
            double y = fiber->top->realVal;
            if (x == 0 && y == 0)
                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "atan2() domain error");
            fiber->top->realVal = atan2(y, x);
            break;
        }
        case BUILTIN_EXP:           fiber->top->realVal = exp (fiber->top->realVal); break;
        case BUILTIN_LOG:
        {
            if (fiber->top->realVal <= 0)
                error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "log() domain error");
            fiber->top->realVal = log(fiber->top->realVal);
            break;
        }

        // Memory
        case BUILTIN_NEW:           doBuiltinNew(fiber, pages, error); break;
        case BUILTIN_MAKE:          doBuiltinMake(fiber, pages, error); break;
        case BUILTIN_MAKEFROMARR:   doBuiltinMakefromarr(fiber, pages, error); break;
        case BUILTIN_MAKEFROMSTR:   doBuiltinMakefromstr(fiber, pages, error); break;
        case BUILTIN_MAKETOARR:     doBuiltinMaketoarr(fiber, pages, error); break;
        case BUILTIN_MAKETOSTR:     doBuiltinMaketostr(fiber, pages, error); break;
        case BUILTIN_COPY:          doBuiltinCopy(fiber, pages, error); break;
        case BUILTIN_APPEND:        doBuiltinAppend(fiber, pages, error); break;
        case BUILTIN_INSERT:        doBuiltinInsert(fiber, pages, error); break;
        case BUILTIN_DELETE:        doBuiltinDelete(fiber, pages, error); break;
        case BUILTIN_SLICE:         doBuiltinSlice(fiber, pages, error); break;
        case BUILTIN_SORT:          doBuiltinSort(fiber, pages, error); break;
        case BUILTIN_SORTFAST:      doBuiltinSortfast(fiber, pages, error); break;
        case BUILTIN_LEN:           doBuiltinLen(fiber, error); break;
        case BUILTIN_CAP:           doBuiltinCap(fiber, error); break;
        case BUILTIN_SIZEOF:        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;       // Done at compile time
        case BUILTIN_SIZEOFSELF:    doBuiltinSizeofself(fiber, error); break;
        case BUILTIN_SELFHASPTR:    doBuiltinSelfhasptr(fiber, error); break;
        case BUILTIN_SELFTYPEEQ:    doBuiltinSelftypeeq(fiber, error); break;
        case BUILTIN_TYPEPTR:       error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;       // Done at compile time
        case BUILTIN_VALID:         doBuiltinValid(fiber, error); break;

        // Maps
        case BUILTIN_VALIDKEY:      doBuiltinValidkey(fiber, pages, error); break;
        case BUILTIN_KEYS:          doBuiltinKeys(fiber, pages, error); break;

        // Fibers
        case BUILTIN_RESUME:        doBuiltinResume(fiber, newFiber, pages, error); break;

        // Misc
        case BUILTIN_MEMUSAGE:      doBuiltinMemusage(fiber, pages, error); break;
        case BUILTIN_EXIT:          doBuiltinExit(fiber, error); return;
    }

    fiber->ip = ip;
    fiber->ip++;
}


static FORCE_INLINE void doReturn(Fiber *fiber, Fiber **newFiber)
{
    // Pop return address
    int returnOffset = (fiber->top++)->intVal;

    if (returnOffset == VM_RETURN_FROM_FIBER)
    {
        // For fiber function, kill the fiber, extract the parent fiber pointer and switch to it
        fiber->alive = false;
        *newFiber = fiber->parent;
    }
    else
    {
        // For conventional function, remove parameters from the stack and go back
        fiber->top += fiber->code[fiber->ip].operand.intVal;
        fiber->ip = returnOffset;
    }
}


static FORCE_INLINE void doEnterFrame(Fiber *fiber, HeapPages *pages, HookFunc *hooks, Error *error)
{
    int localVarSlots = fiber->code[fiber->ip].operand.intVal;

    // Allocate stack frame
    if (fiber->top - localVarSlots - fiber->stack < VM_MIN_FREE_STACK)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Stack overflow");

    // Push old stack frame base pointer, set new one
    (--fiber->top)->ptrVal = fiber->base;
    fiber->base = fiber->top;

    // Push stack frame ref count
    (--fiber->top)->intVal = 0;

    // Push parameter layout table pointer
    (--fiber->top)->ptrVal = NULL;

    // Move stack top
    fiber->top -= localVarSlots;

    // Zero the whole stack frame
    memset(fiber->top, 0, localVarSlots * sizeof(Slot));

    // Call 'call' hook, if any
    doHook(fiber, hooks, HOOK_CALL);

    fiber->ip++;
}


static FORCE_INLINE void doLeaveFrame(Fiber *fiber, HeapPages *pages, HookFunc *hooks, Error *error)
{
    // Check stack frame ref count
    int64_t stackFrameRefCnt = (fiber->base - 1)->intVal;
    if (stackFrameRefCnt != 0)
        error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Pointer to a local variable escapes from the function");

    // Call 'return' hook, if any
    doHook(fiber, hooks, HOOK_RETURN);

    // Restore stack top
    fiber->top = fiber->base;

    // Pop old stack frame base pointer
    fiber->base = (Slot *)(fiber->top++)->ptrVal;

    fiber->ip++;
}


static FORCE_INLINE void doHalt(VM *vm)
{
    vm->terminatedNormally = true;
    vm->fiber->alive = false;
}


static FORCE_INLINE void vmLoop(VM *vm)
{
    Fiber *fiber = vm->fiber;
    HeapPages *pages = &vm->pages;
    HookFunc *hooks = vm->hooks;
    Error *error = vm->error;

    while (1)
    {
        if (fiber->top - fiber->stack < VM_MIN_FREE_STACK)
            error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Stack overflow");

        switch (fiber->code[fiber->ip].opcode)
        {
            case OP_PUSH:                           doPush(fiber, error);                         break;
            case OP_PUSH_ZERO:                      doPushZero(fiber, error);                     break;
            case OP_PUSH_LOCAL_PTR:                 doPushLocalPtr(fiber);                        break;
            case OP_PUSH_LOCAL_PTR_ZERO:            doPushLocalPtrZero(fiber);                    break;
            case OP_PUSH_LOCAL:                     doPushLocal(fiber, error);                    break;
            case OP_PUSH_REG:                       doPushReg(fiber);                             break;
            case OP_PUSH_UPVALUE:                   doPushUpvalue(fiber, pages, error);           break;
            case OP_POP:                            doPop(fiber);                                 break;
            case OP_POP_REG:                        doPopReg(fiber);                              break;
            case OP_DUP:                            doDup(fiber);                                 break;
            case OP_SWAP:                           doSwap(fiber);                                break;
            case OP_ZERO:                           doZero(fiber);                                break;
            case OP_DEREF:                          doDeref(fiber, error);                        break;
            case OP_ASSIGN:                         doAssign(fiber, error);                       break;
            case OP_ASSIGN_PARAM:                   doAssignParam(fiber, error);                  break;
            case OP_CHANGE_REF_CNT:                 doChangeRefCnt(fiber, pages);                 break;
            case OP_CHANGE_REF_CNT_GLOBAL:          doChangeRefCntGlobal(fiber, pages, error);    break;
            case OP_CHANGE_REF_CNT_LOCAL:           doChangeRefCntLocal(fiber, pages, error);     break;
            case OP_CHANGE_REF_CNT_ASSIGN:          doChangeRefCntAssign(fiber, pages, error);    break;
            case OP_UNARY:                          doUnary(fiber, error);                        break;
            case OP_BINARY:                         doBinary(fiber, pages, error);                break;
            case OP_GET_ARRAY_PTR:                  doGetArrayPtr(fiber, error);                  break;
            case OP_GET_DYNARRAY_PTR:               doGetDynArrayPtr(fiber, error);               break;
            case OP_GET_MAP_PTR:                    doGetMapPtr(fiber, pages, error);             break;
            case OP_GET_FIELD_PTR:                  doGetFieldPtr(fiber, error);                  break;
            case OP_ASSERT_TYPE:                    doAssertType(fiber);                          break;
            case OP_ASSERT_RANGE:                   doAssertRange(fiber, error);                  break;
            case OP_WEAKEN_PTR:                     doWeakenPtr(fiber, pages);                    break;
            case OP_STRENGTHEN_PTR:                 doStrengthenPtr(fiber, pages);                break;
            case OP_GOTO:                           doGoto(fiber);                                break;
            case OP_GOTO_IF:                        doGotoIf(fiber);                              break;
            case OP_GOTO_IF_NOT:                    doGotoIfNot(fiber);                           break;
            case OP_CALL:                           doCall(fiber, error);                         break;
            case OP_CALL_INDIRECT:                  doCallIndirect(fiber, error);                 break;
            case OP_CALL_EXTERN:                    doCallExtern(fiber, error);                   break;
            case OP_CALL_BUILTIN:
            {
                Fiber *newFiber = NULL;
                doCallBuiltin(fiber, &newFiber, pages, error);

                if (!fiber->alive)
                    return;

                if (newFiber)
                    fiber = vm->fiber = vm->pages.fiber = newFiber;

                break;
            }
            case OP_RETURN:
            {
                Fiber *newFiber = NULL;
                doReturn(fiber, &newFiber);

                if (newFiber)
                    fiber = vm->fiber = vm->pages.fiber = newFiber;

                if (!fiber->alive || fiber->ip == VM_RETURN_FROM_VM)
                    return;

                break;
            }
            case OP_ENTER_FRAME:                    doEnterFrame(fiber, pages, hooks, error);     break;
            case OP_LEAVE_FRAME:                    doLeaveFrame(fiber, pages, hooks, error);     break;
            case OP_HALT:                           doHalt(vm);                                   return;

            default: error->runtimeHandler(error->context, VM_RUNTIME_ERROR, "Illegal instruction"); return;
        } // switch
    }
}


void vmRun(VM *vm, FuncContext *fn)
{
    if (!vm->fiber->alive)
        vm->error->runtimeHandler(vm->error->context, VM_RUNTIME_ERROR, "Cannot run a dead fiber");

    if (fn)
    {
        // Calling individual function
        if (fn->entryOffset <= 0)
            vm->error->runtimeHandler(vm->error->context, VM_RUNTIME_ERROR, "Called function is not defined");

        // Push parameters
        int numParamSlots = 0;
        if (fn->params)
        {
            const ExternalCallParamLayout *paramLayout = (ExternalCallParamLayout *)fn->params[-4].ptrVal;   // For -4, see the stack layout diagram in umka_vm.c
            numParamSlots = paramLayout->numParamSlots;
        }
        else
            numParamSlots = sizeof(Interface) / sizeof(Slot);    // Only upvalue

        vm->fiber->top -= numParamSlots;

        Slot empty = {0};
        for (int i = 0; i < numParamSlots; i++)
            vm->fiber->top[i] = fn->params ? fn->params[i] : empty;

        // Push 'return from VM' signal as return address
        (--vm->fiber->top)->intVal = VM_RETURN_FROM_VM;

        // Go to the entry point
        vm->fiber->ip = fn->entryOffset;
    }
    else
    {
        // Calling main()
        vm->fiber->ip = 0;
    }

    // Main loop
    vmLoop(vm);

    // Save result
    if (fn && fn->result)
        *(fn->result) = vm->fiber->reg[VM_REG_RESULT];
}


bool vmAlive(VM *vm)
{
    return vm->mainFiber->alive;
}


void vmKill(VM *vm)
{
    vm->mainFiber->alive = false;
}


int vmAsm(int ip, Instruction *code, DebugInfo *debugPerInstr, char *buf, int size)
{
    Instruction *instr = &code[ip];
    DebugInfo *debug = &debugPerInstr[ip];

    char opcodeBuf[DEFAULT_STR_LEN + 1];
    snprintf(opcodeBuf, DEFAULT_STR_LEN + 1, "%s%s", instr->inlineOpcode == OP_SWAP ? "SWAP; " : "", opcodeSpelling[instr->opcode]);
    int chars = snprintf(buf, size, "%09d %6d %28s", ip, debug->line, opcodeBuf);

    if (instr->tokKind != TOK_NONE)
        chars += snprintf(buf + chars, nonneg(size - chars), " %s", lexSpelling(instr->tokKind));

    if (instr->type)
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        chars += snprintf(buf + chars, nonneg(size - chars), " %s", typeSpelling(instr->type, typeBuf));
    }
    else if (instr->typeKind != TYPE_NONE)
        chars += snprintf(buf + chars, nonneg(size - chars), " %s", typeKindSpelling(instr->typeKind));

    switch (instr->opcode)
    {
        case OP_PUSH:
        {
            if (instr->typeKind == TYPE_PTR || instr->inlineOpcode == OP_DEREF)
                chars += snprintf(buf + chars, nonneg(size - chars), " %p", instr->operand.ptrVal);
            else if (instr->typeKind == TYPE_REAL)
                chars += snprintf(buf + chars, nonneg(size - chars), " %lg", instr->operand.realVal);
            else
                chars += snprintf(buf + chars, nonneg(size - chars), " %lld", (long long int)instr->operand.intVal);
            break;
        }
        case OP_PUSH_ZERO:
        case OP_PUSH_LOCAL_PTR:
        case OP_PUSH_LOCAL:
        case OP_PUSH_REG:
        case OP_POP:
        case OP_POP_REG:
        case OP_ZERO:
        case OP_ASSIGN:
        case OP_ASSIGN_PARAM:
        case OP_BINARY:
        case OP_CHANGE_REF_CNT_LOCAL:
        case OP_GET_FIELD_PTR:
        case OP_GOTO:
        case OP_GOTO_IF:
        case OP_GOTO_IF_NOT:
        case OP_ENTER_FRAME:
        case OP_CALL_INDIRECT:
        case OP_RETURN:                 chars += snprintf(buf + chars, nonneg(size - chars), " %lld",  (long long int)instr->operand.intVal); break;
        case OP_CALL:
        {
            const char *fnName = debugPerInstr[instr->operand.intVal].fnName;
            chars += snprintf(buf + chars, nonneg(size - chars), " %s (%lld)", fnName, (long long int)instr->operand.intVal);
            break;
        }
        case OP_PUSH_LOCAL_PTR_ZERO:
        case OP_GET_ARRAY_PTR:          chars += snprintf(buf + chars, nonneg(size - chars), " %d %d", (int)instr->operand.int32Val[0], (int)instr->operand.int32Val[1]); break;
        case OP_CHANGE_REF_CNT_GLOBAL:
        case OP_CALL_EXTERN:            chars += snprintf(buf + chars, nonneg(size - chars), " %p",    instr->operand.ptrVal); break;
        case OP_CALL_BUILTIN:           chars += snprintf(buf + chars, nonneg(size - chars), " %s",    builtinSpelling[instr->operand.builtinVal]); break;

        default: break;
    }

    if (instr->inlineOpcode == OP_DEREF)
        chars += snprintf(buf + chars, nonneg(size - chars), "; DEREF");

    return chars;
}


bool vmUnwindCallStack(VM *vm, Slot **base, int *ip)
{
    return stackUnwind(vm->fiber, base, ip);
}


void vmSetHook(VM *vm, HookEvent event, HookFunc hook)
{
    vm->hooks[event] = hook;
}


void *vmAllocData(VM *vm, int size, ExternFunc onFree)
{
    return chunkAlloc(&vm->pages, size, NULL, onFree, false, vm->error);
}


void vmIncRef(VM *vm, void *ptr)
{
    HeapPage *page = pageFind(&vm->pages, ptr, true);
    if (page)
        chunkChangeRefCnt(&vm->pages, page, ptr, 1);
}


void vmDecRef(VM *vm, void *ptr)
{
    HeapPage *page = pageFind(&vm->pages, ptr, true);
    if (page)
        chunkChangeRefCnt(&vm->pages, page, ptr, -1);
}


void *vmGetMapNodeData(VM *vm, Map *map, Slot key)
{
    if (!map || !map->root)
        return NULL;

    const MapNode *node = *doGetMapNode(map, key, false, NULL, vm->error);
    return node ? node->data : NULL;
}


char *vmMakeStr(VM *vm, const char *str)
{
    if (!str)
        return NULL;

    char *buf = doAllocStr(&vm->pages, strlen(str), vm->error);
    strcpy(buf, str);
    return buf;
}


void vmMakeDynArray(VM *vm, DynArray *array, Type *type, int len)
{
    if (!array)
        return;

    doChangeRefCntImpl(vm->fiber, &vm->pages, array, type, TOK_MINUSMINUS);
    doAllocDynArray(&vm->pages, array, type, len, vm->error);
}


int64_t vmGetMemUsage(VM *vm)
{
    return vm->pages.totalSize;
}


const char *vmBuiltinSpelling(BuiltinFunc builtin)
{
    return builtinSpelling[builtin];
}

