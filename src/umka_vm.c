#define __USE_MINGW_ANSI_STDIO 1

//#define UMKA_VM_DEBUG
//#define UMKA_STR_DEBUG
//#define UMKA_REF_CNT_DEBUG
//#define UMKA_DETAILED_LEAK_INFO

#ifdef UMKA_VM_DEBUG
    #define FORCE_INLINE
    #define UNLIKELY(x)  (x)
#else
    #ifdef _MSC_VER  // MSVC++ only
        #define FORCE_INLINE __forceinline
        #define UNLIKELY(x)  (x)
    #else
        #define FORCE_INLINE __attribute__((always_inline)) inline
        #define UNLIKELY(x)  __builtin_expect(!!(x), 0)
    #endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>
#include <inttypes.h>

#include "umka_vm.h"


/*
Virtual machine stack layout (64-bit slots):

    ...                                              <- Address 0
    ...
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
    Parameter N                                      <- Parameter array (external functions only)
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
    "abs",
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
    "selfptr",
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


static const char *regSpelling [] =
{
    "RESULT",
    "SELF",
    "HEAP_COPY",
    "SWITCH_EXPR",
    "EXPR_LIST"
};


// Memory management

static FORCE_INLINE Slot *doGetOnFreeParams(void *ptr)
{
    static char paramLayoutBuf[sizeof(ParamLayout) + 2 * sizeof(int64_t)];
    ParamLayout *paramLayout = (ParamLayout *)&paramLayoutBuf;

    paramLayout->numParams = 2;
    paramLayout->numParamSlots = 1;
    paramLayout->numResultParams = 0;
    paramLayout->firstSlotIndex[0] = 0;     // No upvalues
    paramLayout->firstSlotIndex[1] = 0;     // Pointer to data to deallocate

    static Slot paramsBuf[4 + 1] = {0};
    Slot *params = paramsBuf + 4;

    params[-4].ptrVal = paramLayout;        // For -4, see the stack layout diagram above
    params[ 0].ptrVal = ptr;

    return params;
}


static FORCE_INLINE Slot *doGetOnFreeResult(HeapPages *pages)
{
    static Slot resultBuf = {0};
    Slot *result = &resultBuf;

    result->ptrVal = pages->error->context;     // Upon entry, the result slot stores the Umka instance

    return result;
}


static void pageInit(HeapPages *pages, Fiber *fiber, Error *error)
{
    pages->first = NULL;
    pages->lastAccessed = NULL;
    pages->lowest = pages->highest = NULL;
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

        // Report memory leaks
        if (warnLeak)
        {
            fprintf(stderr, "Warning: Memory leak at %p (%d refs)\n", page->data, page->refCnt);

#ifdef UMKA_DETAILED_LEAK_INFO
            for (int i = 0; i < page->numOccupiedChunks; i++)
            {
                const HeapChunk *chunk = (const HeapChunk *)(page->data + i * page->chunkSize);
                if (chunk->refCnt == 0)
                    continue;

                const DebugInfo *debug = &pages->fiber->debugPerInstr[chunk->ip];
                fprintf(stderr, "    Chunk allocated in %s: %s (%d)\n", debug->fnName, debug->fileName, debug->line);
            }
#endif
        }

        // Call custom deallocators, if any
        for (int i = 0; i < page->numOccupiedChunks && page->numChunksWithOnFree > 0; i++)
        {
            HeapChunk *chunk = (HeapChunk *)(page->data + i * page->chunkSize);
            if (chunk->refCnt == 0 || !chunk->onFree)
                continue;

            chunk->onFree(&doGetOnFreeParams(chunk->data)->apiSlot, &doGetOnFreeResult(pages)->apiSlot);
            page->numChunksWithOnFree--;
        }

        free(page);
        page = next;
    }
}


static FORCE_INLINE HeapPage *pageAdd(HeapPages *pages, int numChunks, int chunkSize)
{
    const int size = numChunks * chunkSize;

    HeapPage *page = malloc(sizeof(HeapPage) + size);
    if (UNLIKELY(!page))
        pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "Out of memory");

    page->id = pages->freeId++;
    page->refCnt = 0;
    page->numChunks = numChunks;
    page->numOccupiedChunks = 0;
    page->numChunksWithOnFree = 0;
    page->chunkSize = chunkSize;
    page->prev = NULL;
    page->next = pages->first;
    page->end = page->data + size;

    pages->totalSize += size;

    if (pages->first)
        pages->first->prev = page;
    pages->first = page;

    if (!pages->lowest || pages->lowest > page->data)
        pages->lowest = page->data;

    if (!pages->highest || pages->highest < page->end)
        pages->highest = page->end;

    pages->lastAccessed = page;

#ifdef UMKA_REF_CNT_DEBUG
    fprintf(stderr, "Add page at %p\n", page->data);
#endif

    return page;
}


static FORCE_INLINE void pageRemove(HeapPages *pages, HeapPage *page)
{
#ifdef UMKA_REF_CNT_DEBUG
    fprintf(stderr, "Remove page at %p\n", page->data);
#endif

    pages->totalSize -= page->numChunks * page->chunkSize;

    if (page == pages->first)
        pages->first = page->next;

    if (page->prev)
        page->prev->next = page->next;

    if (page->next)
        page->next->prev = page->prev;

    if (page == pages->lastAccessed)
        pages->lastAccessed = pages->first;        

    free(page);
}


static FORCE_INLINE HeapChunk *pageGetChunk(const HeapPage *page, void *ptr)
{
    const int chunkOffset = ((char *)ptr - page->data) % page->chunkSize;
    return (HeapChunk *)((char *)ptr - chunkOffset);
}


static FORCE_INLINE bool pageContainsPtr(HeapPages *pages, const HeapPage *page, void *ptr)
{
    if (ptr >= (void *)page->data && ptr < (void *)page->end)
    {
        const HeapChunk *chunk = pageGetChunk(page, ptr);
        if (UNLIKELY(chunk->refCnt <= 0))
            pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "Dangling pointer at %p", ptr);
        return true;
    }
    return false;
}


static FORCE_INLINE HeapPage *pageFind(HeapPages *pages, void *ptr)
{
    if (pages->lowest && ptr < (void *)pages->lowest)
        return NULL;

    if (UNLIKELY(pages->highest && ptr >= (void *)pages->highest))
        return NULL;

    if (pages->lastAccessed && pageContainsPtr(pages, pages->lastAccessed, ptr))
        return pages->lastAccessed;
        
    for (HeapPage *page = pages->first; page; page = page->next)
    {
        if (page != pages->lastAccessed && pageContainsPtr(pages, page, ptr))
        {
            pages->lastAccessed = page;
            return page;
        }
    }

    return NULL;
}


static FORCE_INLINE HeapPage *pageFindForAlloc(HeapPages *pages, int chunkSize)
{
    HeapPage *bestPage = NULL;
    int bestSize = INT_MAX;

    for (HeapPage *page = pages->first; page; page = page->next)
    {
        if (page->numOccupiedChunks < page->numChunks)
        {
            if (page->chunkSize == chunkSize)
            {
                pages->lastAccessed = page;
                return page;
            }

            if (page->chunkSize > chunkSize && page->chunkSize < bestSize)
            {
                bestPage = page;
                bestSize = page->chunkSize;
            }
        }
    }

    if (bestPage)
        pages->lastAccessed = bestPage;
    return bestPage;
}


static FORCE_INLINE HeapPage *pageFindById(HeapPages *pages, int id)
{
    for (HeapPage *page = pages->first; page; page = page->next)
    {
        if (page->id == id)
            return page;
    }
    return NULL;
}


static FORCE_INLINE bool stackUnwind(Fiber *fiber, Slot **base, int *ip)
{
    if (*base == fiber->stack + fiber->stackSize - 1)
        return false;

    const int returnOffset = (*base + 1)->intVal;
    if (returnOffset == RETURN_FROM_FIBER || returnOffset == RETURN_FROM_VM)
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
        const ParamLayout *paramLayout = base[-2].ptrVal;

        while (ptr > (void *)(base + 1 + paramLayout->numParamSlots))   // + 1 for return address
        {
            if (UNLIKELY(!stackUnwind(fiber, &base, NULL)))
                pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "Illegal stack pointer");

            paramLayout = base[-2].ptrVal;
        }

        int64_t *stackFrameRefCnt = &base[-1].intVal;
        *stackFrameRefCnt += delta;
    }
}


static FORCE_INLINE void *chunkAlloc(HeapPages *pages, int64_t size, const Type *type, UmkaExternFunc onFree, bool isStack, Error *error)
{
    // Page layout: header, data, footer (char), padding, header, data, footer (char), padding...
    const int64_t chunkSize = align(sizeof(HeapChunk) + align(size + 1, sizeof(int64_t)), MEM_MIN_HEAP_CHUNK);

    if (UNLIKELY(size < 0 || chunkSize > INT_MAX))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Cannot allocate a block of %lld bytes", size);

    HeapPage *page = pageFindForAlloc(pages, chunkSize);
    if (!page)
    {
        int numChunks = MEM_MIN_HEAP_PAGE / chunkSize;
        if (numChunks == 0)
            numChunks = 1;

        page = pageAdd(pages, numChunks, chunkSize);
    }

    HeapChunk *chunk = (HeapChunk *)(page->data + page->numOccupiedChunks * page->chunkSize);

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
    fprintf(stderr, "Add chunk at %p\n", chunk->data);
#endif

    return chunk->data;
}


static FORCE_INLINE int chunkChangeRefCnt(HeapPages *pages, HeapPage *page, void *ptr, int delta)
{
    HeapChunk *chunk = pageGetChunk(page, ptr);

    if (UNLIKELY(chunk->refCnt <= 0 || page->refCnt < chunk->refCnt))
        pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "Wrong reference count for pointer at %p", ptr);

    if (chunk->onFree && chunk->refCnt == 1 && delta == -1)
    {
        chunk->onFree(&doGetOnFreeParams(ptr)->apiSlot, &doGetOnFreeResult(pages)->apiSlot);
        page->numChunksWithOnFree--;
    }

    chunk->refCnt += delta;
    page->refCnt += delta;

    // Additional ref counts for a user-defined address interval (used for stack frames to detect escaping refs)
    stackChangeFrameRefCnt(pages->fiber, pages, ptr, delta);

#ifdef UMKA_REF_CNT_DEBUG
    fprintf(stderr, "%p: delta: %+d  chunk: %d  page: %d\n", ptr, delta, chunk->refCnt, page->refCnt);
#endif

    if (page->refCnt == 0)
    {
        pageRemove(pages, page);
        return 0;
    }

    return chunk->refCnt;
}


static FORCE_INLINE void candidateInit(RefCntChangeCandidates *candidates, Storage *storage)
{
    candidates->storage = storage;
    candidates->capacity = 100;
    candidates->stack = storageAdd(candidates->storage, candidates->capacity * sizeof(RefCntChangeCandidate));
    candidates->top = -1;
}


static FORCE_INLINE void candidateReset(RefCntChangeCandidates *candidates)
{
    candidates->top = -1;
}


static FORCE_INLINE void candidatePush(RefCntChangeCandidates *candidates, void *ptr, const Type *type)
{
    if (candidates->top >= candidates->capacity - 1)
    {
        candidates->capacity *= 2;
        candidates->stack = storageRealloc(candidates->storage, candidates->stack, candidates->capacity * sizeof(RefCntChangeCandidate));
    }

    RefCntChangeCandidate *candidate = &candidates->stack[++candidates->top];
    candidate->ptr = ptr;
    candidate->type = type;
    candidate->pageForDeferred = NULL;
}


static FORCE_INLINE void candidatePushDeferred(RefCntChangeCandidates *candidates, void *ptr, const Type *type, HeapPage *page)
{
    candidatePush(candidates, ptr, type);
    candidates->stack[candidates->top].pageForDeferred = page;
}


static FORCE_INLINE void candidatePop(RefCntChangeCandidates *candidates, void **ptr, const Type **type, HeapPage **page)
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


static FORCE_INLINE char *fsscanfString(Storage *storage, bool string, void *stream, int *len)
{
    int capacity = 8;
    char *str = storageAdd(storage, capacity);

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
            str = storageRealloc(storage, str, capacity);
        }
        ch = fsgetc(string, stream, len);
    }

    str[writtenLen] = '\0';
    return str;
}


typedef int (*QSortCompareFn)(const void *a, const void *b, void *context);


FORCE_INLINE void qsortSwap(void *a, void *b, void *temp, int itemSize)
{
    memcpy(temp, a, itemSize);
    memcpy(a, b, itemSize);
    memcpy(b, temp, itemSize);
}


FORCE_INLINE char *qsortPartition(char *first, char *last, int itemSize, QSortCompareFn compare, void *context, void *temp)
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

void vmInit(VM *vm, Storage *storage, int stackSize, bool fileSystemEnabled, Error *error)
{
    vm->storage = storage;
    vm->fiber = vm->mainFiber = storageAdd(vm->storage, sizeof(Fiber));
    vm->fiber->parent = NULL;
    vm->fiber->refCntChangeCandidates = &vm->refCntChangeCandidates;
    vm->fiber->vm = vm;
    vm->fiber->alive = true;
    vm->fiber->fileSystemEnabled = fileSystemEnabled;

    pageInit(&vm->pages, vm->fiber, error);

    vm->fiber->stack = chunkAlloc(&vm->pages, stackSize * sizeof(Slot), NULL, NULL, true, error);
    vm->fiber->stackSize = stackSize;

    candidateInit(&vm->refCntChangeCandidates, vm->storage);

    memset(&vm->hooks, 0, sizeof(vm->hooks));
    vm->terminatedNormally = false;
    vm->error = error;

    srand(1);
}


void vmFree(VM *vm)
{
    HeapPage *page = pageFind(&vm->pages, vm->mainFiber->stack);
    if (UNLIKELY(!page))
       vm->error->runtimeHandler(vm->error->context, ERR_RUNTIME, "No fiber stack");

    chunkChangeRefCnt(&vm->pages, page, vm->mainFiber->stack, -1);
    pageFree(&vm->pages, vm->terminatedNormally);
}


void vmReset(VM *vm, const Instruction *code, const DebugInfo *debugPerInstr)
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
    if (UNLIKELY(dims->len != strlen(str) || dims->capacity < dims->len + 1))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Invalid string: %s", str);
#endif
}


static FORCE_INLINE char *doGetEmptyStr(void);
static FORCE_INLINE void doGetEmptyDynArray(DynArray *array, const Type *type);


static FORCE_INLINE void doHook(Fiber *fiber, const UmkaHookFunc *hooks, UmkaHookEvent event)
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
    if (UNLIKELY(!slot->ptrVal))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Pointer is null");

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

        default:                error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); return;
    }
}


static FORCE_INLINE void doAssignImpl(void *lhs, Slot rhs, TypeKind typeKind, int structSize, Error *error)
{
    if (UNLIKELY(!lhs))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Pointer is null");

    Const rhsConstant = {.intVal = rhs.intVal};
    if (UNLIKELY(typeOverflow(typeKind, rhsConstant)))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Overflow of %s", typeKindSpelling(typeKind));

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
            if (UNLIKELY(!rhs.ptrVal))
                error->runtimeHandler(error->context, ERR_RUNTIME, "Pointer is null");
            memcpy(lhs, rhs.ptrVal, structSize);
            break;
        }
        case TYPE_FIBER:        *(void *        *)lhs = rhs.ptrVal;  break;
        case TYPE_FN:           *(int64_t       *)lhs = rhs.intVal;  break;

        default:                error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); return;
    }
}


static int64_t doCompare(Slot lhs, Slot rhs, const Type *type, Error *error)
{
    switch (type->kind)
    {
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:   return lhs.intVal - rhs.intVal;
        case TYPE_UINT:     return (lhs.uintVal == rhs.uintVal) ? 0 : (lhs.uintVal > rhs.uintVal) ? 1 : -1;
        case TYPE_BOOL:
        case TYPE_CHAR:     return lhs.intVal - rhs.intVal;
        case TYPE_REAL32:
        case TYPE_REAL:
        {
            const double diff = lhs.realVal - rhs.realVal;
            return (diff == 0.0) ? 0 : (diff > 0.0) ? 1 : -1;
        }
        case TYPE_PTR:      return (char *)lhs.ptrVal - (char *)rhs.ptrVal;
        case TYPE_WEAKPTR:  return lhs.weakPtrVal - rhs.weakPtrVal;
        case TYPE_STR:
        {
            const char *lhsStr = lhs.ptrVal;
            if (!lhsStr)
                lhsStr = doGetEmptyStr();

            const char *rhsStr = rhs.ptrVal;
            if (!rhsStr)
                rhsStr = doGetEmptyStr();

            doCheckStr(lhsStr, error);
            doCheckStr(rhsStr, error);

            return strcmp(lhsStr, rhsStr);
        }
        case TYPE_ARRAY:
        case TYPE_STRUCT:
        {
            for (int i = 0; i < type->numItems; i++)
            {
                const Type *itemType = (type->kind == TYPE_ARRAY) ? type->base : type->field[i]->type;
                const int itemOffset = (type->kind == TYPE_ARRAY) ? (i * itemType->size) : type->field[i]->offset;                
                
                Slot lhsItem = {.ptrVal = (char *)lhs.ptrVal + itemOffset};
                Slot rhsItem = {.ptrVal = (char *)rhs.ptrVal + itemOffset};
            
                doDerefImpl(&lhsItem, itemType->kind, error);
                doDerefImpl(&rhsItem, itemType->kind, error);

                const int64_t itemDiff = doCompare(lhsItem, rhsItem, itemType, error);
                if (itemDiff != 0)
                    return itemDiff;
            }
            return 0;
        }
        case TYPE_DYNARRAY:
        {
            const DynArray *lhsArray = lhs.ptrVal;
            if (UNLIKELY(!lhsArray))
                error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

            const int64_t lhsLen = lhsArray->data ? getDims(lhsArray)->len : 0;

            const DynArray *rhsArray = rhs.ptrVal;
            if (UNLIKELY(!rhsArray))
                error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

            const int64_t rhsLen = rhsArray->data ? getDims(rhsArray)->len : 0;

            for (int i = 0; ; i++)
            {
                if (i == lhsLen && i == rhsLen)
                    return 0;
                if (i == lhsLen)
                    return -1;
                if (i == rhsLen)
                    return 1;
                
                const int itemOffset = i * type->base->size;                
                
                Slot lhsItem = {.ptrVal = (char *)lhsArray->data + itemOffset};
                Slot rhsItem = {.ptrVal = (char *)rhsArray->data + itemOffset};
            
                doDerefImpl(&lhsItem, type->base->kind, error);
                doDerefImpl(&rhsItem, type->base->kind, error);

                const int64_t itemDiff = doCompare(lhsItem, rhsItem, type->base, error);
                if (itemDiff != 0)
                    return itemDiff;
            }
            return 0;
        }
                
        default: error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); return 0;
    }
}


static FORCE_INLINE void doAddPtrBaseRefCntCandidate(RefCntChangeCandidates *candidates, void *ptr, const Type *type)
{
    if (typeKindGarbageCollected(type->base->kind))
    {
        void *data = ptr;
        if (type->base->kind == TYPE_PTR || type->base->kind == TYPE_STR || type->base->kind == TYPE_FIBER)
            data = *(void **)data;

        candidatePush(candidates, data, type->base);
    }
}


static FORCE_INLINE void doAddArrayItemsRefCntCandidates(RefCntChangeCandidates *candidates, void *ptr, const Type *type, int len)
{
    if (typeKindGarbageCollected(type->base->kind))
    {
        char *itemPtr = ptr;
        const int itemSize = type->base->size;

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


static FORCE_INLINE void doAddStructFieldsRefCntCandidates(RefCntChangeCandidates *candidates, void *ptr, const Type *type)
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


static FORCE_INLINE void doChangeRefCntImpl(Fiber *fiber, HeapPages *pages, void *ptr, const Type *type, TokenKind tokKind)
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
                HeapPage *page = pageFind(pages, ptr);
                if (!page)
                    break;

                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, ptr, 1);
                else
                {
                    HeapChunk *chunk = pageGetChunk(page, ptr);
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
                        switch (chunk->type->kind)
                        {
                            case TYPE_ARRAY:
                            case TYPE_MAP:
                            case TYPE_STRUCT:
                            case TYPE_INTERFACE:
                            case TYPE_CLOSURE:
                            {
                                candidatePush(candidates, chunk->data, chunk->type);
                                break;
                            }
                            case TYPE_DYNARRAY:
                            {
                                // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
                                const DynArrayDimensions *dims = (DynArrayDimensions *)chunk->data;
                                void *data = chunk->data + sizeof(DynArrayDimensions);
                                doAddArrayItemsRefCntCandidates(candidates, data, chunk->type, dims->len);
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

                HeapPage *page = pageFind(pages, ptr);
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
                HeapPage *page = pageFind(pages, array->data);
                if (!page)
                    break;

                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, array->data, 1);
                else
                {
                    const HeapChunk *chunk = pageGetChunk(page, array->data);
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
                HeapPage *page = pageFind(pages, ptr);
                if (!page)
                    break;

                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, ptr, 1);
                else
                {
                    const HeapChunk *chunk = pageGetChunk(page, ptr);
                    if (chunk->refCnt > 1)
                    {
                        chunkChangeRefCnt(pages, page, ptr, -1);
                        break;
                    }

                    if (UNLIKELY(((Fiber *)ptr)->alive))
                        pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "Cannot destroy a busy fiber");

                    // Only one ref is left. Defer processing the parent and traverse the children before removing the ref
                    HeapPage *stackPage = pageFind(pages, ((Fiber *)ptr)->stack);
                    if (UNLIKELY(!stackPage))
                        pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "No fiber stack");

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

    if (dims.capacity > INT_MAX - MEM_MIN_FREE_HEAP)
        dims.capacity = INT_MAX - MEM_MIN_FREE_HEAP;
    if (dims.capacity < dims.len)
        dims.capacity = dims.len;

    char *dimsAndData = chunkAlloc(pages, sizeof(StrDimensions) + dims.capacity, NULL, NULL, false, error);
    *(StrDimensions *)dimsAndData = dims;

    char *data = dimsAndData + sizeof(StrDimensions);
    data[len] = 0;

    return data;
}


static FORCE_INLINE char *doGetEmptyStr(void)
{
    StrDimensions dims = {.len = 0, .capacity = 1};

    static char dimsAndData[sizeof(StrDimensions) + 1];
    *(DynArrayDimensions *)dimsAndData = dims;

    char *data = dimsAndData + sizeof(StrDimensions);
    data[0] = 0;

    return data;
}


static FORCE_INLINE void doAllocDynArray(HeapPages *pages, DynArray *array, const Type *type, int64_t len, Error *error)
{
    array->type     = type;
    array->itemSize = array->type->base->size;

    DynArrayDimensions dims = {.len = len, .capacity = 2 * (len + 1)};

    if (dims.capacity * array->itemSize > INT_MAX - MEM_MIN_FREE_HEAP)
        dims.capacity = (INT_MAX - MEM_MIN_FREE_HEAP) / array->itemSize;
    if (dims.capacity < dims.len)
        dims.capacity = dims.len;

    char *dimsAndData = chunkAlloc(pages, sizeof(DynArrayDimensions) + dims.capacity * array->itemSize, array->type, NULL, false, error);
    *(DynArrayDimensions *)dimsAndData = dims;

    array->data = dimsAndData + sizeof(DynArrayDimensions);
}


static FORCE_INLINE void doGetEmptyDynArray(DynArray *array, const Type *type)
{
    array->type     = type;
    array->itemSize = array->type->base->size;

    static DynArrayDimensions dims = {.len = 0, .capacity = 0};
    array->data = (char *)(&dims) + sizeof(DynArrayDimensions);
}


static FORCE_INLINE void doAllocMap(HeapPages *pages, Map *map, const Type *type, Error *error)
{
    map->type      = type;
    map->root      = chunkAlloc(pages, type->base->size, type->base, NULL, false, error);
    map->root->len = 0;
}


static FORCE_INLINE void doRebalanceMapNodes(MapNode **parent, MapNode *child)
{
    // Rotate tree to swap parent and child nodes
    if (child == (*parent)->right)
    {
        (*parent)->right = child->left;
        child->left = (*parent);
    }
    else if (child == (*parent)->left)
    {
        (*parent)->left = child->right;
        child->right = (*parent);      
    }
    else
        return;

    (*parent) = child;
}


static FORCE_INLINE MapNode **doGetMapNode(Map *map, Slot key, bool createMissingNodes, HeapPages *pages, Error *error)
{
    if (UNLIKELY(!map || !map->root))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Map is null");

    MapNode **node = &map->root;

    while (*node)
    {
        int64_t keyDiff = 1;
        if ((*node)->key)
        {
            // Get key difference
            Slot nodeKey = {.ptrVal = (*node)->key};
            doDerefImpl(&nodeKey, typeMapKey(map->type)->kind, error);
            keyDiff = doCompare(key, nodeKey, typeMapKey(map->type), error);
        }

        MapNode **child = NULL;

        if (keyDiff > 0)
            child = &(*node)->right;
        else if (keyDiff < 0)
            child = &(*node)->left;
        else
            return node;

        // Rebalance as a randomized binary search tree (treap)
        if ((*node)->priority > 0 && (*child) && (*child)->priority > (*node)->priority)
            doRebalanceMapNodes(node, *child);
        else
            node = child;
    }

    if (createMissingNodes)
    {
        const Type *nodeType = map->type->base;
        *node = chunkAlloc(pages, nodeType->size, nodeType, NULL, false, error);
    }

    return node;
}


static MapNode *doCopyMapNode(Map *map, MapNode *node, Fiber *fiber, HeapPages *pages, Error *error)
{
    if (!node)
        return NULL;

    const Type *nodeType = map->type->base;
    MapNode *result = (MapNode *)chunkAlloc(pages, nodeType->size, nodeType, NULL, false, error);

    result->len = node->len;
    result->priority = node->priority;

    if (node->key)
    {
        const Type *keyType = typeMapKey(map->type);

        Slot srcKey = {.ptrVal = node->key};
        doDerefImpl(&srcKey, keyType->kind, error);

        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        result->key = chunkAlloc(pages, keyType->size, keyType->kind == TYPE_DYNARRAY ? NULL : keyType, NULL, false, error);

        if (typeGarbageCollected(keyType))
            doChangeRefCntImpl(fiber, pages, srcKey.ptrVal, keyType, TOK_PLUSPLUS);

        doAssignImpl(result->key, srcKey, keyType->kind, keyType->size, error);
    }

    if (node->data)
    {
        const Type *itemType = typeMapItem(map->type);

        Slot srcItem = {.ptrVal = node->data};
        doDerefImpl(&srcItem, itemType->kind, error);

        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        result->data = chunkAlloc(pages, itemType->size, itemType->kind == TYPE_DYNARRAY ? NULL : itemType, NULL, false, error);

        if (typeGarbageCollected(itemType))
            doChangeRefCntImpl(fiber, pages, srcItem.ptrVal, itemType, TOK_PLUSPLUS);

        doAssignImpl(result->data, srcItem, itemType->kind, itemType->size, error);
    }

    if (node->left)
        result->left = doCopyMapNode(map, node->left, fiber, pages, error);

    if (node->right)
        result->right = doCopyMapNode(map, node->right, fiber, pages, error);

    return result;
}


static void doGetMapKeysRecursively(const Map *map, const MapNode *node, void *keys, int *numKeys, Error *error)
{
    if (node->left)
        doGetMapKeysRecursively(map, node->left, keys, numKeys, error);

    if (node->key)
    {
        const Type *keyType = typeMapKey(map->type);
        void *destKey = (char *)keys + keyType->size * (*numKeys);

        Slot srcKey = {.ptrVal = node->key};
        doDerefImpl(&srcKey, keyType->kind, error);
        doAssignImpl(destKey, srcKey, keyType->kind, keyType->size, error);

        (*numKeys)++;
    }

    if (node->right)
        doGetMapKeysRecursively(map, node->right, keys, numKeys, error);
}


static FORCE_INLINE void doGetMapKeys(const Map *map, void *keys, Error *error)
{
    int numKeys = 0;
    doGetMapKeysRecursively(map, map->root, keys, &numKeys, error);
    if (UNLIKELY(numKeys != map->root->len))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Wrong number of map keys");
}


static FORCE_INLINE Fiber *doAllocFiber(Fiber *parent, const Closure *childClosure, const Type *childClosureType, HeapPages *pages, Error *error)
{
    if (UNLIKELY(!childClosure || childClosure->entryOffset <= 0))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Called function is not defined");

    // Copy whole fiber context
    Fiber *child = chunkAlloc(pages, sizeof(Fiber), NULL, NULL, false, error);

    *child = *parent;
    child->stack = chunkAlloc(pages, child->stackSize * sizeof(Slot), NULL, NULL, true, error);
    child->top = child->base = child->stack + child->stackSize - 1;

    child->parent = parent;

    const Signature *childClosureSig = &childClosureType->field[0]->type->sig;

    // Push upvalues
    child->top -= sizeof(Interface) / sizeof(Slot);
    *(Interface *)child->top = childClosure->upvalue;
    doChangeRefCntImpl(child, pages, child->top, childClosureSig->param[0]->type, TOK_PLUSPLUS);

    // Push 'return from fiber' signal instead of return address
    (--child->top)->intVal = RETURN_FROM_FIBER;

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


static int doFillReprBuf(const Slot *slot, const Type *type, char *buf, int maxLen, int depth, bool pretty, bool dereferenced, Storage *storage, Error *error)
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
                len += doFillReprBuf(&dataSlot, type->base, buf + len, maxLen, depth + 1, pretty, dereferenced, storage, error);
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

            char *itemPtr = slot->ptrVal;
            const int itemSize = type->base->size;

            for (int i = 0; i < type->numItems; i++)
            {
                Slot itemSlot = {.ptrVal = itemPtr};
                doDerefImpl(&itemSlot, type->base->kind, error);
                len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, depth + 1, pretty, dereferenced, storage, error);

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

            const DynArray *array = slot->ptrVal;
            if (array && array->data)
            {
                char *itemPtr = array->data;
                for (int i = 0; i < getDims(array)->len; i++)
                {
                    Slot itemSlot = {.ptrVal = itemPtr};
                    doDerefImpl(&itemSlot, type->base->kind, error);
                    len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, depth + 1, pretty, dereferenced, storage, error);

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

            Map *map = slot->ptrVal;
            if (map && map->root)
            {
                const Type *keyType = typeMapKey(map->type);
                const Type *itemType = typeMapItem(map->type);

                void *keys = storageAdd(storage, map->root->len * keyType->size);

                doGetMapKeys(map, keys, error);

                char *keyPtr = (char *)keys;
                for (int i = 0; i < map->root->len; i++)
                {
                    Slot keySlot = {.ptrVal = keyPtr};
                    doDerefImpl(&keySlot, keyType->kind, error);
                    len += doFillReprBuf(&keySlot, keyType, buf + len, maxLen, depth + 1, pretty, dereferenced, storage, error);

                    len += snprintf(buf + len, maxLen, ": ");

                    const MapNode *node = *doGetMapNode(map, keySlot, false, NULL, error);
                    if (UNLIKELY(!node))
                        error->runtimeHandler(error->context, ERR_RUNTIME, "Map node is null");

                    Slot itemSlot = {.ptrVal = node->data};
                    doDerefImpl(&itemSlot, itemType->kind, error);
                    len += doFillReprBuf(&itemSlot, itemType, buf + len, maxLen, depth + 1, pretty, dereferenced, storage, error);

                    if (i < map->root->len - 1)
                        len += doPrintIndented(buf + len, maxLen, depth, pretty, ' ');

                    keyPtr += keyType->size;
                }

                storageRemove(storage, keys);
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
                len += doFillReprBuf(&fieldSlot, type->field[i]->type, buf + len, maxLen, depth + 1, pretty, dereferenced, storage, error);

                if (i < type->numItems - 1)
                    len += doPrintIndented(buf + len, maxLen, depth, pretty, ' ');
            }

            len += doPrintIndented(buf + len, maxLen, depth, pretty, '}');
            break;
        }

        case TYPE_INTERFACE:
        {
            const Interface *interface = (Interface *)slot->ptrVal;
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

                len += doFillReprBuf(&selfSlot, interface->selfType->base, buf + len, maxLen, depth + 1, pretty, dereferenced, storage, error);

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
        default:                error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); break;
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

    const Type *type   = fiber->code[fiber->ip].type;
    TypeKind typeKind  = type->kind;

    if (UNLIKELY(!string && (!stream || (!fiber->fileSystemEnabled && !console))))
        error->runtimeHandler(error->context, ERR_RUNTIME, "printf destination is null");

    if (!format)
        format = doGetEmptyStr();

    int formatLen = -1, typeLetterPos = -1;
    TypeKind expectedTypeKind = TYPE_NONE;
    FormatStringTypeSize formatStringTypeSize = FORMAT_SIZE_NORMAL;

    if (UNLIKELY(!typeFormatStringValid(format, &formatLen, &typeLetterPos, &expectedTypeKind, &formatStringTypeSize)))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Invalid format string");

    if (UNLIKELY(!typeCompatiblePrintf(expectedTypeKind, type->kind, true)))
    {
        char typeBuf[DEFAULT_STR_LEN + 1];
        error->runtimeHandler(error->context, ERR_RUNTIME, "Incompatible types %s and %s in printf", typeKindSpelling(expectedTypeKind), typeSpelling(type, typeBuf));
    }

    // Check overflow
    if (expectedTypeKind != TYPE_VOID)
    {
        Const arg;
        if (typeKindReal(expectedTypeKind))
            arg.realVal = value.realVal;
        else
            arg.intVal = value.intVal;

        if (UNLIKELY(typeConvOverflow(expectedTypeKind, type->kind, arg)))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Overflow of %s", typeKindSpelling(expectedTypeKind));
    }

    char curFormatBuf[DEFAULT_STR_LEN + 1];
    char *curFormat = curFormatBuf;

    const bool isCurFormatBufInHeap = formatLen + 1 > (int)sizeof(curFormatBuf);
    if (isCurFormatBufInHeap)
        curFormat = storageAdd(fiber->vm->storage, formatLen + 1);

    memcpy(curFormat, format, formatLen);
    curFormat[formatLen] = 0;

    // Special case: %v formatter - convert argument of any type to its string representation
    char reprBuf[sizeof(StrDimensions) + DEFAULT_STR_LEN + 1];
    char *dimsAndRepr = reprBuf;

    bool isReprBufInHeap = false;

    const bool hasAnyTypeFormatter = expectedTypeKind == TYPE_INTERFACE && typeLetterPos >= 0 && typeLetterPos < formatLen;     // %v
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

        const int reprLen = doFillReprBuf(&value, type, NULL, 0, 0, pretty, dereferenced, fiber->vm->storage, error);  // Predict buffer length

        isReprBufInHeap = sizeof(StrDimensions) + reprLen + 1 > sizeof(reprBuf);
        if (isReprBufInHeap)
            dimsAndRepr = storageAdd(fiber->vm->storage, sizeof(StrDimensions) + reprLen + 1);

        StrDimensions dims = {.len = reprLen, .capacity = reprLen + 1};
        *(StrDimensions *)dimsAndRepr = dims;

        char *repr = dimsAndRepr + sizeof(StrDimensions);
        repr[reprLen] = 0;

        doFillReprBuf(&value, type, repr, reprLen + 1, 0, pretty, dereferenced, fiber->vm->storage, error);            // Fill buffer

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
        storageRemove(fiber->vm->storage, curFormat);

    if (isReprBufInHeap)
        storageRemove(fiber->vm->storage, dimsAndRepr);
}


static FORCE_INLINE void doBuiltinScanf(Fiber *fiber, HeapPages *pages, bool console, bool string, Error *error)
{
    void *stream       = console ? stdin : (void *)fiber->top[STACK_OFFSET_STREAM].ptrVal;
    const char *format = (const char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal;
    Slot value         = fiber->top[STACK_OFFSET_VALUE];

    const Type *type   = fiber->code[fiber->ip].type;
    TypeKind typeKind  = type->kind;

    if (UNLIKELY(!stream || (!fiber->fileSystemEnabled && !console && !string)))
        error->runtimeHandler(error->context, ERR_RUNTIME, "scanf source is null");

    if (!format)
        format = doGetEmptyStr();

    int formatLen = -1, typeLetterPos = -1;
    TypeKind expectedTypeKind = TYPE_NONE;
    FormatStringTypeSize formatStringTypeSize = FORMAT_SIZE_NORMAL;

    if (UNLIKELY(!typeFormatStringValid(format, &formatLen, &typeLetterPos, &expectedTypeKind, &formatStringTypeSize)))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Invalid format string");

    if (UNLIKELY(!typeCompatibleScanf(expectedTypeKind, typeKind, true)))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Incompatible types %s and %s in scanf", typeKindSpelling(expectedTypeKind), typeKindSpelling(typeKind));

    char curFormatBuf[DEFAULT_STR_LEN + 1];
    char *curFormat = curFormatBuf;

    const bool isCurFormatBufInHeap = formatLen + 2 + 1 > (int)sizeof(curFormatBuf);                   // + 2 for "%n"
    if (isCurFormatBufInHeap)
        curFormat = storageAdd(fiber->vm->storage, formatLen + 2 + 1);

    memcpy(curFormat, format, formatLen);
    curFormat[formatLen + 0] = '%';
    curFormat[formatLen + 1] = 'n';
    curFormat[formatLen + 2] = '\0';

    int len = 0, cnt = 0;

    if (typeKind == TYPE_VOID)
        cnt = fsscanf(string, stream, curFormat, &len);
    else
    {
        if (UNLIKELY(!value.ptrVal))
            error->runtimeHandler(error->context, ERR_RUNTIME, "scanf destination is null");

        // Strings need special handling, as the required buffer size is unknown
        if (typeKind == TYPE_STR)
        {
            char *src = fsscanfString(fiber->vm->storage, string, stream, &len);
            char **dest = (char **)value.ptrVal;

            // Decrease old string ref count
            Type destType = {.kind = TYPE_STR};
            doChangeRefCntImpl(fiber, pages, *dest, &destType, TOK_MINUSMINUS);

            // Allocate new string
            *dest = doAllocStr(pages, strlen(src), error);
            strcpy(*dest, src);
            storageRemove(fiber->vm->storage, src);

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
        storageRemove(fiber->vm->storage, curFormat);
}


// fn new(type: Type, size: int [, expr: type]): ^type
static FORCE_INLINE void doBuiltinNew(Fiber *fiber, HeapPages *pages, Error *error)
{
    const int size = (fiber->top++)->intVal;
    const Type *type = fiber->code[fiber->ip].type;

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
    const Type *type = fiber->code[fiber->ip].type;

    if (type->kind == TYPE_DYNARRAY)
    {
        DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
        int64_t len = (fiber->top++)->intVal;

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
        const Closure *childClosure = (fiber->top++)->ptrVal;

        Fiber *result = doAllocFiber(fiber, childClosure, type->base, pages, error);
        (--fiber->top)->ptrVal = result;
    }
    else
        error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type");
}


// fn makefromarr(src: [...]ItemType, len: int): []ItemType
static FORCE_INLINE void doBuiltinMakefromarr(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *dest = (fiber->top++)->ptrVal;
    const int64_t len = (fiber->top++)->intVal;
    const void *src = (fiber->top++)->ptrVal;

    const Type *destType = fiber->code[fiber->ip].type;

    doAllocDynArray(pages, dest, destType, len, error);
    memcpy(dest->data, src, getDims(dest)->len * dest->itemSize);

    // Increase result items' ref counts, as if they have been assigned one by one
    const Type staticArrayType = typeMakeDetachedArray(dest->type->base, getDims(dest)->len);
    doChangeRefCntImpl(fiber, pages, dest->data, &staticArrayType, TOK_PLUSPLUS);

    (--fiber->top)->ptrVal = dest;
}


// fn makefromstr(src: str): []char | []uint8
static FORCE_INLINE void doBuiltinMakefromstr(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *dest  = (fiber->top++)->ptrVal;
    const char *src = (fiber->top++)->ptrVal;

    const Type *destType = fiber->code[fiber->ip].type;

    if (!src)
        src = doGetEmptyStr();

    doAllocDynArray(pages, dest, destType, getStrDims(src)->len, error);
    memcpy(dest->data, src, getDims(dest)->len);

    (--fiber->top)->ptrVal = dest;
}


// fn maketoarr(src: []ItemType): [...]ItemType
static FORCE_INLINE void doBuiltinMaketoarr(Fiber *fiber, HeapPages *pages, Error *error)
{
    void *dest = (fiber->top++)->ptrVal;
    const DynArray *src = (fiber->top++)->ptrVal;

    const Type *destType = fiber->code[fiber->ip].type;

    if (UNLIKELY(!src))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    memset(dest, 0, destType->size);

    if (src->data)
    {
        if (UNLIKELY(getDims(src)->len > destType->numItems))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is too long");

        memcpy(dest, src->data, getDims(src)->len * src->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        doChangeRefCntImpl(fiber, pages, dest, destType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = dest;
}


// fn maketostr(src: char | []char | []uint8): str
static FORCE_INLINE void doBuiltinMaketostr(Fiber *fiber, HeapPages *pages, Error *error)
{
    char *dest = doGetEmptyStr();

    const Type *type = fiber->code[fiber->ip].type;
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
        const DynArray *src = (fiber->top++)->ptrVal;

        if (UNLIKELY(!src))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

        if (src->data)
        {
            int64_t len = 0;
            while (len < getDims(src)->len && ((const char *)src->data)[len])
                len++;

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

    if (UNLIKELY(!array))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    *result = *array;

    if (array->data)
    {
        doAllocDynArray(pages, result, array->type, getDims(array)->len, error);
        memmove((char *)result->data, (char *)array->data, getDims(array)->len * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        const Type staticArrayType = typeMakeDetachedArray(result->type->base, getDims(result)->len);
        doChangeRefCntImpl(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn copy(m: map [keyType] type): map [keyType] type
static FORCE_INLINE void doBuiltinCopyMap(Fiber *fiber, HeapPages *pages, Error *error)
{
    Map *result = (Map *)(fiber->top++)->ptrVal;
    Map *map    = (Map *)(fiber->top++)->ptrVal;

    if (UNLIKELY(!map))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Map is null");

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
    const Type *type  = fiber->code[fiber->ip].type;
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

    const Type *arrayType  = fiber->code[fiber->ip].type;

    if (UNLIKELY(!array))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    if (!array->data)
        doGetEmptyDynArray(array, arrayType);

    void *rhs = item;
    int64_t rhsLen = 1;

    if (!single)
    {
        DynArray *rhsArray = item;

        if (UNLIKELY(!rhsArray))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

        if (!rhsArray->data)
            doGetEmptyDynArray(rhsArray, arrayType);

        rhs = rhsArray->data;
        rhsLen = getDims(rhsArray)->len;
    }

    int64_t newLen = getDims(array)->len + rhsLen;

    if (newLen <= getDims(array)->capacity)
    {
        doChangeRefCntImpl(fiber, pages, array, array->type, TOK_PLUSPLUS);
        *result = *array;

        memmove((char *)result->data + getDims(array)->len * array->itemSize, (char *)rhs, rhsLen * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        const Type staticArrayType = typeMakeDetachedArray(result->type->base, rhsLen);
        doChangeRefCntImpl(fiber, pages, (char *)result->data + getDims(array)->len * array->itemSize, &staticArrayType, TOK_PLUSPLUS);

        getDims(result)->len = newLen;
    }
    else
    {
        doAllocDynArray(pages, result, array->type, newLen, error);

        memmove((char *)result->data, (char *)array->data, getDims(array)->len * array->itemSize);
        memmove((char *)result->data + getDims(array)->len * array->itemSize, (char *)rhs, rhsLen * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        const Type staticArrayType = typeMakeDetachedArray(result->type->base, newLen);
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

    const Type *arrayType  = fiber->code[fiber->ip].type;

    if (UNLIKELY(!array))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    if (!array->data)
        doGetEmptyDynArray(array, arrayType);

    if (UNLIKELY(index < 0 || index > getDims(array)->len))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Index %lld is out of range 0...%lld", index, getDims(array)->len);

    if (getDims(array)->len + 1 <= getDims(array)->capacity)
    {
        doChangeRefCntImpl(fiber, pages, array, array->type, TOK_PLUSPLUS);
        *result = *array;

        memmove((char *)result->data + (index + 1) * result->itemSize, (char *)result->data + index * result->itemSize, (getDims(array)->len - index) * result->itemSize);
        memmove((char *)result->data + index * result->itemSize, (char *)item, result->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        const Type staticArrayType = typeMakeDetachedArray(result->type->base, 1);
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
        const Type staticArrayType = typeMakeDetachedArray(result->type->base, getDims(result)->len);
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

    if (UNLIKELY(!array || !array->data))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    if (UNLIKELY(index < 0 || index > getDims(array)->len - 1))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Index %lld is out of range 0...%lld", index, getDims(array)->len - 1);

    doChangeRefCntImpl(fiber, pages, array, array->type, TOK_PLUSPLUS);
    *result = *array;

    // Decrease result item's ref count
    const Type staticArrayType = typeMakeDetachedArray(result->type->base, 1);
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
        error->runtimeHandler(error->context, ERR_RUNTIME, "Map is null");

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

        if (UNLIKELY(--map->root->len < 0))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Map length is negative");
    }

    doChangeRefCntImpl(fiber, pages, map->root, typeMapNodePtr(map->type), TOK_PLUSPLUS);
    result->type = map->type;
    result->root = map->root;

    (--fiber->top)->ptrVal = result;
}


static FORCE_INLINE void doBuiltinDelete(Fiber *fiber, HeapPages *pages, Error *error)
{
    const Type *type  = fiber->code[fiber->ip].type;
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

    const Type *argType  = fiber->code[fiber->ip].type;

    DynArray *array = NULL;
    const char *str = NULL;
    int64_t len = 0;

    if (result)
    {
        // Dynamic array
        array = (DynArray *)arg;

        if (UNLIKELY(!array))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

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

    if (UNLIKELY(startIndex < 0))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Index %lld is out of range 0...%lld", startIndex, len);

    if (UNLIKELY(endIndex < startIndex || endIndex > len))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Index %lld is out of range %lld...%lld", endIndex, startIndex, len);

    if (result)
    {
        // Dynamic array
        doAllocDynArray(pages, result, array->type, endIndex - startIndex, error);

        memcpy((char *)result->data, (char *)array->data + startIndex * result->itemSize, getDims(result)->len * result->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        const Type staticArrayType = typeMakeDetachedArray(result->type->base, getDims(result)->len);
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
    const Type *compareType;
} CompareContext;


static int qsortCompare(const void *a, const void *b, void *context)
{
    Fiber *fiber = ((CompareContext *)context)->fiber;
    const Closure *compare  = ((CompareContext *)context)->compare;
    const Type *compareType = ((CompareContext *)context)->compareType;

    const Signature *compareSig = &compareType->field[0]->type->sig;

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
    (--fiber->top)->intVal = RETURN_FROM_VM;

    // Call the compare function
    int ip = fiber->ip;
    fiber->ip = compare->entryOffset;
    vmLoop(fiber->vm);
    fiber->ip = ip;

    return fiber->reg[REG_RESULT].intVal;
}


static FORCE_INLINE void doBuiltinSort(Fiber *fiber, Error *error)
{
    const Type *compareType = (fiber->top++)->ptrVal;
    Closure *compare = (fiber->top++)->ptrVal;
    DynArray *array = (fiber->top++)->ptrVal;

    if (UNLIKELY(!array))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    if (UNLIKELY(!compare || compare->entryOffset <= 0))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Called function is not defined");

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
    const Type *itemType;
    int64_t offset;
    bool ascending;
    Error *error;
} FastCompareContext;


static int qsortFastCompare(const void *a, const void *b, void *context)
{
    FastCompareContext *fastCompareContext = (FastCompareContext *)context;

    const Type *itemType = fastCompareContext->itemType;
    const int sign = fastCompareContext->ascending ? 1 : -1;
    Error *error = fastCompareContext->error;

    Slot lhsItem = {.ptrVal = (char *)a + fastCompareContext->offset};
    Slot rhsItem = {.ptrVal = (char *)b + fastCompareContext->offset};

    doDerefImpl(&lhsItem, itemType->kind, error);
    doDerefImpl(&rhsItem, itemType->kind, error);

    const int64_t itemDiff = doCompare(lhsItem, rhsItem, itemType, error); 
    return itemDiff == 0 ? 0 : itemDiff > 0 ? sign : -sign;   
}


static FORCE_INLINE void doBuiltinSortfast(Fiber *fiber, Error *error)
{
    const int64_t offset = (fiber->top++)->intVal;
    const bool ascending = (bool)(fiber->top++)->intVal;
    DynArray *array = (DynArray *)(fiber->top++)->ptrVal;
    const Type *itemType = fiber->code[fiber->ip].type;

    if (UNLIKELY(!array))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    if (array->data && getDims(array)->len > 0)
    {
        FastCompareContext context = {itemType, offset, ascending, error};

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
            const DynArray *array = fiber->top->ptrVal;
            if (UNLIKELY(!array))
                error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

            fiber->top->intVal = array->data ? getDims(array)->len : 0;
            break;
        }
        case TYPE_STR:
        {
            const char *str = fiber->top->ptrVal;
            doCheckStr(str, error);
            fiber->top->intVal = str ? getStrDims(str)->len : 0;
            break;
        }
        case TYPE_MAP:
        {
            const Map *map = fiber->top->ptrVal;
            if (UNLIKELY(!map))
                error->runtimeHandler(error->context, ERR_RUNTIME, "Map is null");

            fiber->top->intVal =  map->root ? map->root->len : 0;
            break;
        }
        default:
            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); return;
    }
}


static FORCE_INLINE void doBuiltinCap(Fiber *fiber, Error *error)
{
    const DynArray *array = fiber->top->ptrVal;
    if (UNLIKELY(!array))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    fiber->top->intVal = array->data ? getDims(array)->capacity : 0;
}


static FORCE_INLINE void doBuiltinSizeofself(Fiber *fiber, Error *error)
{
    const Interface *interface = fiber->top->ptrVal;
    if (UNLIKELY(!interface))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Interface is null");

    int size = 0;
    if (interface->selfType)
        size = interface->selfType->base->size;

    fiber->top->intVal = size;
}


static FORCE_INLINE void doBuiltinSelfptr(Fiber *fiber, Error *error)
{
    Interface *interface = fiber->top->ptrVal;
    if (UNLIKELY(!interface))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Interface is null");

    fiber->top->ptrVal = interface->self;
}


static FORCE_INLINE void doBuiltinSelfhasptr(Fiber *fiber, Error *error)
{
    const Interface *interface = fiber->top->ptrVal;
    if (UNLIKELY(!interface))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Interface is null");

    bool hasPtr = false;
    if (interface->selfType)
        hasPtr = typeHasPtr(interface->selfType->base, true);

    fiber->top->intVal = hasPtr;
}


static FORCE_INLINE void doBuiltinSelftypeeq(Fiber *fiber, Error *error)
{
    const Interface *right = (fiber->top++)->ptrVal;
    const Interface *left  = (fiber->top++)->ptrVal;

    if (UNLIKELY(!left || !right))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Interface is null");

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
            const DynArray *array = fiber->top->ptrVal;
            isValid = array && array->data;
            break;
        }
        case TYPE_MAP:
        {
            const Map *map = fiber->top->ptrVal;
            isValid = map && map->root;
            break;
        }
        case TYPE_INTERFACE:
        {
            const Interface *interface = fiber->top->ptrVal;
            isValid = interface && interface->self;
            break;
        }
        case TYPE_FN:
        {
            const int entryOffset = fiber->top->intVal;
            isValid = entryOffset > 0;
            break;
        }
        case TYPE_CLOSURE:
        {
            const Closure *closure = fiber->top->ptrVal;
            isValid = closure && closure->entryOffset > 0;
            break;
        }
        case TYPE_FIBER:
        {
            const Fiber *child = fiber->top->ptrVal;
            isValid = child && child->alive;
            break;
        }
        default:
            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); return;
    }

    fiber->top->intVal = isValid;
}


// fn validkey(m: map [keyType] type, key: keyType): bool
static FORCE_INLINE void doBuiltinValidkey(Fiber *fiber, HeapPages *pages, Error *error)
{
    Slot key  = *fiber->top++;
    Map *map  = (fiber->top++)->ptrVal;

    if (UNLIKELY(!map))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Map is null");

    bool isValid = false;

    if (map->root)
    {
        const MapNode *node = *doGetMapNode(map, key, false, pages, error);
        isValid = node && node->data;
    }

    (--fiber->top)->intVal = isValid;
}


// fn keys(m: map [keyType] type): []keyType
static FORCE_INLINE void doBuiltinKeys(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (fiber->top++)->ptrVal;
    const Map *map = (fiber->top++)->ptrVal;

    const Type *resultType = fiber->code[fiber->ip].type;

    if (UNLIKELY(!map))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Map is null");

    doAllocDynArray(pages, result, resultType, map->root ? map->root->len : 0, error);

    if (map->root)
    {
        doGetMapKeys(map, result->data, error);

        // Increase result items' ref counts, as if they have been assigned one by one
        const Type staticArrayType = typeMakeDetachedArray(result->type->base, getDims(result)->len);
        doChangeRefCntImpl(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn resume([child: fiber])
static FORCE_INLINE void doBuiltinResume(Fiber *fiber, Fiber **newFiber, Error *error)
{
    Fiber *child = (fiber->top++)->ptrVal;
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
    const char *msg = (fiber->top++)->ptrVal;

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
    const int64_t slots = fiber->code[fiber->ip].operand.intVal;

    if (UNLIKELY(fiber->top - slots - fiber->stack < MEM_MIN_FREE_STACK))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Stack overflow");

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
    const int size = fiber->code[fiber->ip].operand.int32Val[1];
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


static FORCE_INLINE void doPushUpvalue(Fiber *fiber, Error *error)
{
    Closure *closure = (fiber->top++)->ptrVal;

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
    const Slot val = *fiber->top;
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
    const int64_t size = fiber->code[fiber->ip].operand.intVal;
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

    const Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;

    doAssignImpl(lhs, rhs, fiber->code[fiber->ip].typeKind, fiber->code[fiber->ip].operand.intVal, error);
    fiber->ip++;
}


static FORCE_INLINE void doAssignParam(Fiber *fiber, Error *error)
{
    const Slot rhs = *fiber->top;

    const int64_t paramSize = fiber->code[fiber->ip].operand.intVal;
    const int64_t paramSlots = align(paramSize, sizeof(Slot)) / sizeof(Slot);
    if (paramSlots != 1)
    {
        if (UNLIKELY(fiber->top - (paramSlots - 1) - fiber->stack < MEM_MIN_FREE_STACK))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Stack overflow");

        fiber->top -= paramSlots - 1;
    }

    doAssignImpl(fiber->top, rhs, fiber->code[fiber->ip].typeKind, paramSize, error);
    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCnt(Fiber *fiber, HeapPages *pages)
{
    void *ptr = fiber->top->ptrVal;
    const TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    const Type *type = fiber->code[fiber->ip].type;

    doChangeRefCntImpl(fiber, pages, ptr, type, tokKind);

    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCntGlobal(Fiber *fiber, HeapPages *pages, Error *error)
{
    const TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    const Type *type = fiber->code[fiber->ip].type;
    void *ptr = fiber->code[fiber->ip].operand.ptrVal;

    Slot slot = {.ptrVal = ptr};

    doDerefImpl(&slot, type->kind, error);
    doChangeRefCntImpl(fiber, pages, slot.ptrVal, type, tokKind);

    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCntLocal(Fiber *fiber, HeapPages *pages, Error *error)
{
    const TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    const Type *type = fiber->code[fiber->ip].type;
    const int offset = fiber->code[fiber->ip].operand.intVal;

    Slot slot = {.ptrVal = (int8_t *)fiber->base + offset};

    doDerefImpl(&slot, type->kind, error);
    doChangeRefCntImpl(fiber, pages, slot.ptrVal, type, tokKind);

    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCntAssign(Fiber *fiber, HeapPages *pages, Error *error)
{
    if (fiber->code[fiber->ip].inlineOpcode == OP_SWAP)
        doSwapImpl(fiber->top);

    const Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;
    const Type *type = fiber->code[fiber->ip].type;

    // Increase right-hand side ref count
    if (fiber->code[fiber->ip].tokKind != TOK_MINUSMINUS)      // "--" means that the right-hand side ref count should not be increased
        doChangeRefCntImpl(fiber, pages, rhs.ptrVal, type, TOK_PLUSPLUS);

    // Decrease left-hand side ref count
    Slot lhsDeref = {.ptrVal = lhs};
    doDerefImpl(&lhsDeref, type->kind, error);
    doChangeRefCntImpl(fiber, pages, lhsDeref.ptrVal, type, TOK_MINUSMINUS);

    doAssignImpl(lhs, rhs, type->kind, type->size, error);
    fiber->ip++;
}


static FORCE_INLINE void doUnary(Fiber *fiber, Error *error)
{
    const TokenKind op = fiber->code[fiber->ip].tokKind;
    const Type *type = fiber->code[fiber->ip].type;

    Slot *slot = fiber->top;

    if (typeReal(type))
    {
        switch (op)
        {
            case TOK_PLUS:  break;
            case TOK_MINUS: slot->realVal = -slot->realVal; break;
            
            default:        error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }
    else
    {
        switch (op)
        {
            case TOK_PLUS:       break;
            case TOK_MINUS:      slot->intVal = -slot->intVal; break;
            case TOK_NOT:        slot->intVal = !slot->intVal; break;
            case TOK_XOR:        slot->intVal = ~slot->intVal; break;

            case TOK_PLUSPLUS:
            {
                switch (type->kind)
                {
                    case TYPE_INT8:   (*(int8_t   *)slot->ptrVal)++; break;
                    case TYPE_INT16:  (*(int16_t  *)slot->ptrVal)++; break;
                    case TYPE_INT32:  (*(int32_t  *)slot->ptrVal)++; break;
                    case TYPE_INT:    (*(int64_t  *)slot->ptrVal)++; break;
                    case TYPE_UINT8:  (*(uint8_t  *)slot->ptrVal)++; break;
                    case TYPE_UINT16: (*(uint16_t *)slot->ptrVal)++; break;
                    case TYPE_UINT32: (*(uint32_t *)slot->ptrVal)++; break;
                    case TYPE_UINT:   (*(uint64_t *)slot->ptrVal)++; break;
                    
                    default:          error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); return;
                }
                fiber->top++;
                break;
            }

            case TOK_MINUSMINUS:
            {
                switch (type->kind)
                {
                    case TYPE_INT8:   (*(int8_t   *)slot->ptrVal)--; break;
                    case TYPE_INT16:  (*(int16_t  *)slot->ptrVal)--; break;
                    case TYPE_INT32:  (*(int32_t  *)slot->ptrVal)--; break;
                    case TYPE_INT:    (*(int64_t  *)slot->ptrVal)--; break;
                    case TYPE_UINT8:  (*(uint8_t  *)slot->ptrVal)--; break;
                    case TYPE_UINT16: (*(uint16_t *)slot->ptrVal)--; break;
                    case TYPE_UINT32: (*(uint32_t *)slot->ptrVal)--; break;
                    case TYPE_UINT:   (*(uint64_t *)slot->ptrVal)--; break;
                    
                    default:          error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal type"); return;
                }
                fiber->top++;
                break;
            }

            default: error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }
    fiber->ip++;
}


static FORCE_INLINE void doBinary(Fiber *fiber, HeapPages *pages, Error *error)
{
    const TokenKind op = fiber->code[fiber->ip].tokKind;
    const Type *type = fiber->code[fiber->ip].type;
    
    const Slot rhs = *fiber->top++;
    Slot *lhs = fiber->top;

    if (type->kind == TYPE_PTR)
    {
        switch (op)
        {
            case TOK_EQEQ:      lhs->intVal = lhs->ptrVal == rhs.ptrVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->ptrVal != rhs.ptrVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->ptrVal >  rhs.ptrVal; break;
            case TOK_LESS:      lhs->intVal = lhs->ptrVal <  rhs.ptrVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->ptrVal >= rhs.ptrVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->ptrVal <= rhs.ptrVal; break;            
            
            default:            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }
    else if (type->kind == TYPE_WEAKPTR)
    {
        switch (op)
        {
            case TOK_EQEQ:      lhs->intVal = lhs->weakPtrVal == rhs.weakPtrVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->weakPtrVal != rhs.weakPtrVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->weakPtrVal >  rhs.weakPtrVal; break;
            case TOK_LESS:      lhs->intVal = lhs->weakPtrVal <  rhs.weakPtrVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->weakPtrVal >= rhs.weakPtrVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->weakPtrVal <= rhs.weakPtrVal; break;             
            
            default:            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }        
    }
    else if (type->kind == TYPE_STR)
    {
        char *lhsStr = (char *)lhs->ptrVal;
        if (!lhsStr)
            lhsStr = doGetEmptyStr();

        char *rhsStr = (char *)rhs.ptrVal;
        if (!rhsStr)
            rhsStr = doGetEmptyStr();

        doCheckStr(lhsStr, error);
        doCheckStr(rhsStr, error);

        switch (op)
        {
            case TOK_PLUS:
            case TOK_PLUSEQ:
            {
                const int lhsLen = getStrDims(lhsStr)->len;
                const int rhsLen = getStrDims(rhsStr)->len;

                const bool inPlace = op == TOK_PLUSEQ && getStrDims(lhsStr)->capacity >= lhsLen + rhsLen + 1;

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

                lhs->ptrVal = buf;
                break;
            }

            case TOK_EQEQ:      lhs->intVal = strcmp(lhsStr, rhsStr) == 0; break;
            case TOK_NOTEQ:     lhs->intVal = strcmp(lhsStr, rhsStr) != 0; break;
            case TOK_GREATER:   lhs->intVal = strcmp(lhsStr, rhsStr)  > 0; break;
            case TOK_LESS:      lhs->intVal = strcmp(lhsStr, rhsStr)  < 0; break;
            case TOK_GREATEREQ: lhs->intVal = strcmp(lhsStr, rhsStr) >= 0; break;
            case TOK_LESSEQ:    lhs->intVal = strcmp(lhsStr, rhsStr) <= 0; break;
            
            default:            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }
    else if (type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY || type->kind == TYPE_STRUCT)
    {
        switch (op)
        {
            case TOK_EQEQ:      lhs->intVal = doCompare(*lhs, rhs, type, error) == 0; break;
            case TOK_NOTEQ:     lhs->intVal = doCompare(*lhs, rhs, type, error) != 0; break;
            case TOK_GREATER:   lhs->intVal = doCompare(*lhs, rhs, type, error)  > 0; break;
            case TOK_LESS:      lhs->intVal = doCompare(*lhs, rhs, type, error)  < 0; break;
            case TOK_GREATEREQ: lhs->intVal = doCompare(*lhs, rhs, type, error) >= 0; break;
            case TOK_LESSEQ:    lhs->intVal = doCompare(*lhs, rhs, type, error) <= 0; break;            
            
            default:            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }
    else if (typeReal(type))
    {
        switch (op)
        {
            case TOK_PLUS:  lhs->realVal += rhs.realVal; break;
            case TOK_MINUS: lhs->realVal -= rhs.realVal; break;
            case TOK_MUL:   lhs->realVal *= rhs.realVal; break;
            case TOK_DIV:
            {
                if (UNLIKELY(rhs.realVal == 0))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Division by zero");
                lhs->realVal /= rhs.realVal;
                break;
            }
            case TOK_MOD:
            {
                if (UNLIKELY(rhs.realVal == 0))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Division by zero");
                lhs->realVal = fmod(lhs->realVal, rhs.realVal);
                break;
            }

            case TOK_EQEQ:      lhs->intVal = lhs->realVal == rhs.realVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->realVal != rhs.realVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->realVal >  rhs.realVal; break;
            case TOK_LESS:      lhs->intVal = lhs->realVal <  rhs.realVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->realVal >= rhs.realVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->realVal <= rhs.realVal; break;
            
            default:            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }
    else if (type->kind == TYPE_UINT)
    {
        switch (op)
        {
            case TOK_PLUS:  lhs->uintVal += rhs.uintVal; break;
            case TOK_MINUS: lhs->uintVal -= rhs.uintVal; break;
            case TOK_MUL:   lhs->uintVal *= rhs.uintVal; break;
            case TOK_DIV:
            {
                if (UNLIKELY(rhs.uintVal == 0))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Division by zero");
                lhs->uintVal /= rhs.uintVal;
                break;
            }
            case TOK_MOD:
            {
                if (UNLIKELY(rhs.uintVal == 0))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Division by zero");
                lhs->uintVal %= rhs.uintVal;
                break;
            }

            case TOK_SHL:   lhs->uintVal <<= rhs.uintVal; break;
            case TOK_SHR:   lhs->uintVal >>= rhs.uintVal; break;
            case TOK_AND:   lhs->uintVal &= rhs.uintVal; break;
            case TOK_OR:    lhs->uintVal |= rhs.uintVal; break;
            case TOK_XOR:   lhs->uintVal ^= rhs.uintVal; break;

            case TOK_EQEQ:      lhs->intVal = lhs->uintVal == rhs.uintVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->uintVal != rhs.uintVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->uintVal >  rhs.uintVal; break;
            case TOK_LESS:      lhs->intVal = lhs->uintVal <  rhs.uintVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->uintVal >= rhs.uintVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->uintVal <= rhs.uintVal; break;

            default:            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }
    else  // All ordinal types except TYPE_UINT
    {
        switch (op)
        {
            case TOK_PLUS:  lhs->intVal += rhs.intVal; break;
            case TOK_MINUS: lhs->intVal -= rhs.intVal; break;
            case TOK_MUL:   lhs->intVal *= rhs.intVal; break;
            case TOK_DIV:
            {
                if (UNLIKELY(rhs.intVal == 0))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Division by zero");
                if (UNLIKELY(lhs->intVal == LLONG_MIN && rhs.intVal == -1))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Overflow of int");
                lhs->intVal /= rhs.intVal;
                break;
            }
            case TOK_MOD:
            {
                if (UNLIKELY(rhs.intVal == 0))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Division by zero");
                if (UNLIKELY(lhs->intVal == LLONG_MIN && rhs.intVal == -1))
                    error->runtimeHandler(error->context, ERR_RUNTIME, "Overflow of int");
                lhs->intVal %= rhs.intVal;
                break;
            }

            case TOK_SHL:   lhs->intVal <<= rhs.intVal; break;
            case TOK_SHR:   lhs->intVal >>= rhs.intVal; break;
            case TOK_AND:   lhs->intVal &= rhs.intVal; break;
            case TOK_OR:    lhs->intVal |= rhs.intVal; break;
            case TOK_XOR:   lhs->intVal ^= rhs.intVal; break;

            case TOK_EQEQ:      lhs->intVal = lhs->intVal == rhs.intVal; break;
            case TOK_NOTEQ:     lhs->intVal = lhs->intVal != rhs.intVal; break;
            case TOK_GREATER:   lhs->intVal = lhs->intVal >  rhs.intVal; break;
            case TOK_LESS:      lhs->intVal = lhs->intVal <  rhs.intVal; break;
            case TOK_GREATEREQ: lhs->intVal = lhs->intVal >= rhs.intVal; break;
            case TOK_LESSEQ:    lhs->intVal = lhs->intVal <= rhs.intVal; break;

            default:            error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        }
    }

    fiber->ip++;
}


static FORCE_INLINE void doGetArrayPtr(Fiber *fiber, Error *error)
{
    const int64_t itemSize = fiber->code[fiber->ip].operand.int32Val[0];
    int64_t len = fiber->code[fiber->ip].operand.int32Val[1];
    const int64_t index = (fiber->top++)->intVal;

    char *data = fiber->top->ptrVal;

    if (len >= 0)   // For arrays, nonnegative length must be explicitly provided
    {
        if (UNLIKELY(!data))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Array is null");
    }
    else            // For strings, negative length means that the actual string length is to be used
    {
        if (!data)
            data = doGetEmptyStr();
        doCheckStr(data, error);
        len = getStrDims(data)->len;
    }

    if (UNLIKELY(index < 0 || index > len - 1))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Index %lld is out of range 0...%lld", index, len - 1);

    fiber->top->ptrVal = data + itemSize * index;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doGetDynArrayPtr(Fiber *fiber, Error *error)
{
    const int64_t index = (fiber->top++)->intVal;
    const DynArray *array = (fiber->top++)->ptrVal;

    if (UNLIKELY(!array || !array->data))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Dynamic array is null");

    const int64_t itemSize = array->itemSize;
    const int64_t len = getDims(array)->len;

    if (UNLIKELY(index < 0 || index > len - 1))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Index %lld is out of range 0...%lld", index, len - 1);

    (--fiber->top)->ptrVal = (char *)array->data + itemSize * index;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doGetMapPtr(Fiber *fiber, HeapPages *pages, Error *error)
{
    const Slot key = *fiber->top++;
    Map *map = (fiber->top++)->ptrVal;
    const Type *mapType = fiber->code[fiber->ip].type;

    if (UNLIKELY(!map))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Map is null");

    if (!map->root)
        doAllocMap(pages, map, mapType, error);

    const Type *keyType = typeMapKey(map->type);
    const Type *itemType = typeMapItem(map->type);

    MapNode *node = *doGetMapNode(map, key, true, pages, error);
    if (!node->data)
    {
        node->priority = (int64_t)rand() + 1;
        
        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        node->key  = chunkAlloc(pages, keyType->size,  keyType->kind  == TYPE_DYNARRAY ? NULL : keyType,  NULL, false, error);
        node->data = chunkAlloc(pages, itemType->size, itemType->kind == TYPE_DYNARRAY ? NULL : itemType, NULL, false, error);

        // Increase key ref count
        if (typeGarbageCollected(keyType))
            doChangeRefCntImpl(fiber, pages, key.ptrVal, keyType, TOK_PLUSPLUS);

        doAssignImpl(node->key, key, keyType->kind, keyType->size, error);
        map->root->len++;
    }

    (--fiber->top)->ptrVal = node->data;
    fiber->ip++;
}


static FORCE_INLINE void doGetFieldPtr(Fiber *fiber, Error *error)
{
    const int64_t fieldOffset = fiber->code[fiber->ip].operand.intVal;

    if (UNLIKELY(!fiber->top->ptrVal))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Array or structure is null");

    fiber->top->ptrVal = (char *)fiber->top->ptrVal + fieldOffset;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doDerefImpl(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doAssertType(Fiber *fiber)
{
    const Interface *interface = (fiber->top++)->ptrVal;
    const Type *type = fiber->code[fiber->ip].type;

    (--fiber->top)->ptrVal = (interface->selfType && typeEquivalent(type, interface->selfType)) ? interface->self : NULL;
    fiber->ip++;
}


static FORCE_INLINE void doAssertRange(Fiber *fiber, Error *error)
{
    const TypeKind destTypeKind = fiber->code[fiber->ip].typeKind;
    const Type *srcType = fiber->code[fiber->ip].type;

    Const arg;
    if (typeKindReal(destTypeKind))
        arg.realVal = fiber->top->realVal;
    else
        arg.intVal = fiber->top->intVal;

    if (UNLIKELY(typeConvOverflow(destTypeKind, srcType->kind, arg)))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Overflow of %s", typeKindSpelling(destTypeKind));

    fiber->ip++;
}


static FORCE_INLINE void doWeakenPtr(Fiber *fiber, HeapPages *pages)
{
    void *ptr = fiber->top->ptrVal;
    uint64_t weakPtr = 0;

    const HeapPage *page = pageFind(pages, ptr);
    if (page)
    {
        const HeapChunk *chunk = pageGetChunk(page, ptr);
        if (UNLIKELY(chunk->isStack))
            pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "Pointer to a local variable cannot be weak");

        const bool isHeapPtr = true;
        const int pageId = page->id;
        const int pageOffset = (char *)ptr - page->data;

        weakPtr = ((uint64_t)isHeapPtr << 63) | ((uint64_t)pageId << 32) | pageOffset;
    }
    else
        weakPtr = (uint64_t)ptr;

    fiber->top->weakPtrVal = weakPtr;
    fiber->ip++;
}


static FORCE_INLINE void doStrengthenPtr(Fiber *fiber, HeapPages *pages)
{
    const uint64_t weakPtr = fiber->top->weakPtrVal;
    void *ptr = NULL;

    const bool isHeapPtr = (weakPtr >> 63) & 1;
    if (isHeapPtr)
    {
        const int pageId = (weakPtr >> 32) & 0x7FFFFFFF;
        HeapPage *page = pageFindById(pages, pageId);
        if (page)
        {
            const int pageOffset = weakPtr & 0x7FFFFFFF;
            ptr = page->data + pageOffset;

            const HeapChunk *chunk = pageGetChunk(page, ptr);
            if (UNLIKELY(chunk->isStack))
                pages->error->runtimeHandler(pages->error->context, ERR_RUNTIME, "Pointer to a local variable cannot be weak");

            if (chunk->refCnt == 0)
                ptr = NULL;
        }
    }
    else
        ptr = (void *)weakPtr;

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
    const int entryOffset = fiber->code[fiber->ip].operand.intVal;

    // Push return address and go to the entry point
    (--fiber->top)->intVal = fiber->ip + 1;
    fiber->ip = entryOffset;
}


static FORCE_INLINE void doCallIndirect(Fiber *fiber, Error *error)
{
    // For indirect calls, entry point address is below the parameters on the stack
    const int64_t paramSlots = fiber->code[fiber->ip].operand.intVal;
    const int64_t entryOffset = (fiber->top + paramSlots)->intVal;

    if (UNLIKELY(entryOffset == 0))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Called function is not defined");

    // Push return address and go to the entry point
    (--fiber->top)->intVal = fiber->ip + 1;
    fiber->ip = entryOffset;
}


static FORCE_INLINE void doCallExtern(Fiber *fiber, Error *error)
{
    const UmkaExternFunc fn = (UmkaExternFunc)fiber->code[fiber->ip].operand.ptrVal;

    fiber->reg[REG_RESULT].ptrVal = error->context;    // Upon entry, the result slot stores the Umka instance

    const int ip = fiber->ip;
    fn(&fiber->base[2].apiSlot, &fiber->reg[REG_RESULT].apiSlot);      // + 2 from base pointer for old base pointer and return address
    fiber->ip = ip;

    fiber->ip++;
}


static FORCE_INLINE void doCallBuiltin(Fiber *fiber, Fiber **newFiber, HeapPages *pages, Error *error)
{
    // Preserve instruction pointer, in case any of the standard calls end up calling Umka again
    const int ip = fiber->ip;

    const BuiltinFunc builtin = fiber->code[fiber->ip].operand.builtinVal;
    const TypeKind typeKind   = fiber->code[fiber->ip].typeKind;

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
        case BUILTIN_ABS:
        {
            if (UNLIKELY(fiber->top->intVal == LLONG_MIN))
                error->runtimeHandler(error->context, ERR_RUNTIME, "abs() domain error");
            fiber->top->intVal = llabs(fiber->top->intVal);
            break;
        }
        case BUILTIN_FABS:          fiber->top->realVal = fabs(fiber->top->realVal); break;
        case BUILTIN_SQRT:
        {
            if (UNLIKELY(fiber->top->realVal < 0))
                error->runtimeHandler(error->context, ERR_RUNTIME, "sqrt() domain error");
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
            if (UNLIKELY(x == 0 && y == 0))
                error->runtimeHandler(error->context, ERR_RUNTIME, "atan2() domain error");
            fiber->top->realVal = atan2(y, x);
            break;
        }
        case BUILTIN_EXP:           fiber->top->realVal = exp (fiber->top->realVal); break;
        case BUILTIN_LOG:
        {
            if (UNLIKELY(fiber->top->realVal <= 0))
                error->runtimeHandler(error->context, ERR_RUNTIME, "log() domain error");
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
        case BUILTIN_SORT:          doBuiltinSort(fiber, error); break;
        case BUILTIN_SORTFAST:      doBuiltinSortfast(fiber, error); break;
        case BUILTIN_LEN:           doBuiltinLen(fiber, error); break;
        case BUILTIN_CAP:           doBuiltinCap(fiber, error); break;
        case BUILTIN_SIZEOF:        error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;       // Done at compile time
        case BUILTIN_SIZEOFSELF:    doBuiltinSizeofself(fiber, error); break;
        case BUILTIN_SELFPTR:       doBuiltinSelfptr(fiber, error); break;
        case BUILTIN_SELFHASPTR:    doBuiltinSelfhasptr(fiber, error); break;
        case BUILTIN_SELFTYPEEQ:    doBuiltinSelftypeeq(fiber, error); break;
        case BUILTIN_TYPEPTR:       error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;       // Done at compile time
        case BUILTIN_VALID:         doBuiltinValid(fiber, error); break;

        // Maps
        case BUILTIN_VALIDKEY:      doBuiltinValidkey(fiber, pages, error); break;
        case BUILTIN_KEYS:          doBuiltinKeys(fiber, pages, error); break;

        // Fibers
        case BUILTIN_RESUME:        doBuiltinResume(fiber, newFiber, error); break;

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
    const int returnOffset = (fiber->top++)->intVal;

    if (returnOffset == RETURN_FROM_FIBER)
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


static FORCE_INLINE void doEnterFrame(Fiber *fiber, const UmkaHookFunc *hooks, Error *error)
{
    const ParamAndLocalVarLayout *layout = fiber->code[fiber->ip].operand.ptrVal;

    // Allocate stack frame
    if (UNLIKELY(fiber->top - layout->localVarSlots - fiber->stack < MEM_MIN_FREE_STACK))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Stack overflow");

    // Push old stack frame base pointer, set new one
    (--fiber->top)->ptrVal = fiber->base;
    fiber->base = fiber->top;

    // Push stack frame ref count
    (--fiber->top)->intVal = 0;

    // Push parameter layout table pointer
    (--fiber->top)->ptrVal = (ParamLayout *)layout->paramLayout;

    // Move stack top
    fiber->top -= layout->localVarSlots;

    // Zero the whole stack frame
    memset(fiber->top, 0, layout->localVarSlots * sizeof(Slot));

    // Call 'call' hook, if any
    doHook(fiber, hooks, UMKA_HOOK_CALL);

    fiber->ip++;
}


static FORCE_INLINE void doLeaveFrame(Fiber *fiber, const UmkaHookFunc *hooks, Error *error)
{
    // Check stack frame ref count
    const int64_t stackFrameRefCnt = fiber->base[-1].intVal;
    if (UNLIKELY(stackFrameRefCnt != 0))
        error->runtimeHandler(error->context, ERR_RUNTIME, "Pointer to a local variable escapes from the function");

    // Call 'return' hook, if any
    doHook(fiber, hooks, UMKA_HOOK_RETURN);

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
    const UmkaHookFunc *hooks = vm->hooks;
    Error *error = vm->error;

    while (1)
    {
        if (UNLIKELY(fiber->top - fiber->stack < MEM_MIN_FREE_STACK))
            error->runtimeHandler(error->context, ERR_RUNTIME, "Stack overflow");

        switch (fiber->code[fiber->ip].opcode)
        {
            case OP_PUSH:                           doPush(fiber, error);                         break;
            case OP_PUSH_ZERO:                      doPushZero(fiber, error);                     break;
            case OP_PUSH_LOCAL_PTR:                 doPushLocalPtr(fiber);                        break;
            case OP_PUSH_LOCAL_PTR_ZERO:            doPushLocalPtrZero(fiber);                    break;
            case OP_PUSH_LOCAL:                     doPushLocal(fiber, error);                    break;
            case OP_PUSH_REG:                       doPushReg(fiber);                             break;
            case OP_PUSH_UPVALUE:                   doPushUpvalue(fiber, error);                  break;
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

                if (!fiber->alive || fiber->ip == RETURN_FROM_VM)
                    return;

                break;
            }
            case OP_ENTER_FRAME:                    doEnterFrame(fiber, hooks, error);            break;
            case OP_LEAVE_FRAME:                    doLeaveFrame(fiber, hooks, error);            break;
            case OP_HALT:                           doHalt(vm);                                   return;

            default: error->runtimeHandler(error->context, ERR_RUNTIME, "Illegal instruction"); return;
        } // switch
    }
}


void vmRun(VM *vm, UmkaFuncContext *fn)
{
    if (UNLIKELY(!vm->fiber->alive))
        vm->error->runtimeHandler(vm->error->context, ERR_RUNTIME, "Cannot run a dead fiber");

    if (fn)
    {
        // Calling individual function
        if (UNLIKELY(fn->entryOffset <= 0))
            vm->error->runtimeHandler(vm->error->context, ERR_RUNTIME, "Called function is not defined");

        // Push parameters
        int numParamSlots = 0;
        if (fn->params)
        {
            const ParamLayout *paramLayout = fn->params[-4].ptrVal;   // For -4, see the stack layout diagram in umka_vm.c
            numParamSlots = paramLayout->numParamSlots;

            if (paramLayout->numResultParams > 0)
            {
                if (UNLIKELY(!fn->result || !fn->result->ptrVal))
                    vm->error->runtimeHandler(vm->error->context, ERR_RUNTIME, "Storage for structured result is not specified");
                fn->params[paramLayout->firstSlotIndex[paramLayout->numParams - 1]].ptrVal = fn->result->ptrVal;
            }
        }
        else
            numParamSlots = sizeof(Interface) / sizeof(Slot);    // Only upvalue

        vm->fiber->top -= numParamSlots;

        UmkaStackSlot empty = {0};
        for (int i = 0; i < numParamSlots; i++)
            vm->fiber->top[i].apiSlot = fn->params ? fn->params[i] : empty;

        // Push 'return from VM' signal as return address
        (--vm->fiber->top)->intVal = RETURN_FROM_VM;

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
        *(fn->result) = vm->fiber->reg[REG_RESULT].apiSlot;
}


bool vmAlive(VM *vm)
{
    return vm->mainFiber->alive;
}


void vmKill(VM *vm)
{
    vm->mainFiber->alive = false;
}


int vmAsm(int ip, const Instruction *code, const DebugInfo *debugPerInstr, char *buf, int size)
{
    const Instruction *instr = &code[ip];
    const DebugInfo *debug = &debugPerInstr[ip];

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

    if (instr->typeKind != TYPE_NONE && (!instr->type || instr->opcode == OP_ASSERT_RANGE))
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
        case OP_PUSH_REG:
        case OP_POP_REG:                chars += snprintf(buf + chars, nonneg(size - chars), " %s",  regSpelling[instr->operand.intVal]); break;
        case OP_PUSH_ZERO:
        case OP_PUSH_LOCAL_PTR:
        case OP_PUSH_LOCAL:
        case OP_POP:
        case OP_ZERO:
        case OP_ASSIGN:
        case OP_ASSIGN_PARAM:
        case OP_CHANGE_REF_CNT_LOCAL:
        case OP_GET_FIELD_PTR:
        case OP_GOTO:
        case OP_GOTO_IF:
        case OP_GOTO_IF_NOT:
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
        case OP_ENTER_FRAME:
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


void vmSetHook(VM *vm, UmkaHookEvent event, UmkaHookFunc hook)
{
    vm->hooks[event] = hook;
}


void *vmAllocData(VM *vm, int size, UmkaExternFunc onFree)
{
    return chunkAlloc(&vm->pages, size, NULL, onFree, false, vm->error);
}


void vmIncRef(VM *vm, void *ptr, const Type *type)
{
    doChangeRefCntImpl(vm->fiber, &vm->pages, ptr, type, TOK_PLUSPLUS);
}


void vmDecRef(VM *vm, void *ptr, const Type *type)
{
    doChangeRefCntImpl(vm->fiber, &vm->pages, ptr, type, TOK_MINUSMINUS);
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


void vmMakeDynArray(VM *vm, DynArray *array, const Type *type, int len)
{
    if (!array)
        return;

    doChangeRefCntImpl(vm->fiber, &vm->pages, array, type, TOK_MINUSMINUS);
    doAllocDynArray(&vm->pages, array, type, len, vm->error);
}


void *vmMakeStruct(VM *vm, const Type *type)
{
    return chunkAlloc(&vm->pages, type->size, type, NULL, false, vm->error);
}


int64_t vmGetMemUsage(VM *vm)
{
    return vm->pages.totalSize;
}


const char *vmBuiltinSpelling(BuiltinFunc builtin)
{
    return builtinSpelling[builtin];
}

