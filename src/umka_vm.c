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


static const char *opcodeSpelling [] =
{
    "NOP",
    "PUSH",
    "PUSH_ZERO",
    "PUSH_LOCAL_PTR",
    "PUSH_LOCAL",
    "PUSH_REG",
    "PUSH_STRUCT",
    "PUSH_UPVALUE",
    "POP",
    "POP_REG",
    "DUP",
    "SWAP",
    "ZERO",
    "DEREF",
    "ASSIGN",
    "CHANGE_REF_CNT",
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
    "narrow",
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
    "fiberspawn",
    "fibercall",
    "fiberalive",
    "exit",
    "error"
};


// Memory management

static void pageInit(HeapPages *pages, Fiber *fiber, Error *error)
{
    pages->first = pages->last = NULL;
    pages->freeId = 1;
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
                pages->error->runtimeHandler(pages->error->context, "Dangling pointer at %p", ptr);

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
    if (returnOffset == VM_FIBER_KILL_SIGNAL)
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
                pages->error->runtimeHandler(pages->error->context, "Illegal stack pointer");
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
        error->runtimeHandler(error->context, "Illegal block size");

    HeapPage *page = pageFindForAlloc(pages, chunkSize);
    if (!page)
    {
        int numChunks = VM_MIN_HEAP_PAGE / chunkSize;
        if (numChunks == 0)
            numChunks = 1;

        page = pageAdd(pages, numChunks, chunkSize);
        if (!page)
            error->runtimeHandler(error->context, "No memory");
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
        pages->error->runtimeHandler(pages->error->context, "Wrong reference count for pointer at %p", ptr);

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


// I/O functions

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


// Virtual machine

void vmInit(VM *vm, int stackSize, bool fileSystemEnabled, Error *error)
{
    vm->fiber = vm->mainFiber = malloc(sizeof(Fiber));
    vm->fiber->refCntChangeCandidates = &vm->refCntChangeCandidates;
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
       vm->error->runtimeHandler(vm->error->context, "No fiber stack");

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


static FORCE_INLINE void doCheckStr(const char *str, Error *error)
{
#ifdef UMKA_STR_DEBUG
    if (!str)
        return;

    const StrDimensions *dims = getStrDims(str);
    if (dims->len != strlen(str) || dims->capacity < dims->len + 1)
        error->runtimeHandler(error->context, "Invalid string: %s", str);
#endif
}


static FORCE_INLINE void doHook(Fiber *fiber, HookFunc *hooks, HookEvent event)
{
    if (!hooks || !hooks[event])
        return;

    const DebugInfo *debug = &fiber->debugPerInstr[fiber->ip];
    hooks[event](debug->fileName, debug->fnName, debug->line);
}


static FORCE_INLINE void doBasicSwap(Slot *slot)
{
    Slot val = slot[0];
    slot[0] = slot[1];
    slot[1] = val;
}


static FORCE_INLINE void doBasicDeref(Slot *slot, TypeKind typeKind, Error *error)
{
    if (!slot->ptrVal)
        error->runtimeHandler(error->context, "Pointer is null");

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

        default:                error->runtimeHandler(error->context, "Illegal type"); return;
    }
}


static FORCE_INLINE void doBasicAssign(void *lhs, Slot rhs, TypeKind typeKind, int structSize, Error *error)
{
    if (!lhs)
        error->runtimeHandler(error->context, "Pointer is null");

    Const rhsConstant = {.intVal = rhs.intVal};
    if (typeOverflow(typeKind, rhsConstant))
        error->runtimeHandler(error->context, "Overflow of %s", typeKindSpelling(typeKind));

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
        case TYPE_CLOSURE:      memcpy(lhs, rhs.ptrVal, structSize); break;
        case TYPE_FIBER:        *(void *        *)lhs = rhs.ptrVal;  break;
        case TYPE_FN:           *(int64_t       *)lhs = rhs.intVal;  break;

        default:                error->runtimeHandler(error->context, "Illegal type"); return;
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


static FORCE_INLINE void doBasicChangeRefCnt(Fiber *fiber, HeapPages *pages, void *ptr, Type *type, TokenKind tokKind)
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

            case TYPE_WEAKPTR:
                break;

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
                        pages->error->runtimeHandler(pages->error->context, "Cannot destroy a fiber that did not return");

                    // Only one ref is left. Defer processing the parent and traverse the children before removing the ref
                    HeapPage *stackPage = pageFind(pages, ((Fiber *)ptr)->stack, true);
                    if (!stackPage)
                        pages->error->runtimeHandler(pages->error->context, "No fiber stack");

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
        case TYPE_FN:
        {
            // keyBytes must point to a pre-allocated 8-byte buffer
            doBasicAssign(*keyBytes, key, keyType->kind, 0, error);
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


static FORCE_INLINE MapNode *doGetMapNode(Map *map, Slot key, bool createMissingNodes, HeapPages *pages, Error *error, MapNode ***nodePtrInParent)
{
    if (!map || !map->root)
        error->runtimeHandler(error->context, "Map is null");

    Slot keyBytesBuffer = {0};
    char *keyBytes = (char *)&keyBytesBuffer;
    int keySize = 0;

    doGetMapKeyBytes(key, typeMapKey(map->type), error, &keyBytes, &keySize);

    if (!keyBytes)
        error->runtimeHandler(error->context, "Map key is null");

    if (keySize == 0)
        error->runtimeHandler(error->context, "Map key has zero length");

    MapNode *node = map->root;

    for (int64_t bitPos = 0; bitPos < keySize * 8; bitPos++)
    {
        const bool bit = getBit(keyBytes, bitPos);

        MapNode **child = bit ? &node->left : &node->right;
        if (!(*child))
        {
            if (!createMissingNodes)
                return NULL;

            Type *nodeType = map->type->base;
            *child = (MapNode *)chunkAlloc(pages, typeSizeNoCheck(nodeType), nodeType, NULL, false, error);
        }

        if (nodePtrInParent)
            *nodePtrInParent = child;

        node = *child;
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
        doBasicDeref(&srcKey, keyType->kind, error);

        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        result->key = chunkAlloc(pages, typeSizeNoCheck(keyType), keyType->kind == TYPE_DYNARRAY ? NULL : keyType, NULL, false, error);

        if (typeGarbageCollected(keyType))
            doBasicChangeRefCnt(fiber, pages, srcKey.ptrVal, keyType, TOK_PLUSPLUS);

        doBasicAssign(result->key, srcKey, keyType->kind, keySize, error);
    }

    if (node->data)
    {
        Type *itemType = typeMapItem(map->type);
        int itemSize = typeSizeNoCheck(itemType);

        Slot srcItem = {.ptrVal = node->data};
        doBasicDeref(&srcItem, itemType->kind, error);

        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        result->data = chunkAlloc(pages, typeSizeNoCheck(itemType), itemType->kind == TYPE_DYNARRAY ? NULL : itemType, NULL, false, error);

        if (typeGarbageCollected(itemType))
            doBasicChangeRefCnt(fiber, pages, srcItem.ptrVal, itemType, TOK_PLUSPLUS);

        doBasicAssign(result->data, srcItem, itemType->kind, itemSize, error);
    }

    if (node->left)
        result->left = doCopyMapNode(map, node->left, fiber, pages, error);

    if (node->right)
        result->right = doCopyMapNode(map, node->right, fiber, pages, error);

    return result;
}


static void doGetMapKeysRecursively(Map *map, MapNode *node, void *keys, int *numKeys, Error *error)
{
    if (node->key)
    {
        Type *keyType = typeMapKey(map->type);
        int keySize = typeSizeNoCheck(keyType);
        void *destKey = (char *)keys + keySize * (*numKeys);

        Slot srcKey = {.ptrVal = node->key};
        doBasicDeref(&srcKey, keyType->kind, error);
        doBasicAssign(destKey, srcKey, keyType->kind, keySize, error);

        (*numKeys)++;
    }

    if (node->left)
        doGetMapKeysRecursively(map, node->left, keys, numKeys, error);

    if (node->right)
        doGetMapKeysRecursively(map, node->right, keys, numKeys, error);
}


static FORCE_INLINE void doGetMapKeys(Map *map, void *keys, Error *error)
{
    int numKeys = 0;
    doGetMapKeysRecursively(map, map->root, keys, &numKeys, error);
    if (numKeys != map->root->len)
        error->runtimeHandler(error->context, "Wrong number of map keys");
}


static int doFillReprBuf(Slot *slot, Type *type, char *buf, int maxLen, int maxDepth, Error *error)
{
    int len = 0;
    if (maxDepth == 0)
    {
        len = snprintf(buf, maxLen, "...");
        return len;
    }

    switch (type->kind)
    {
        case TYPE_VOID:     len = snprintf(buf, maxLen, "void");                                                             break;
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:   len = snprintf(buf, maxLen, "%lld", (long long int)slot->intVal);                                break;
        case TYPE_UINT:     len = snprintf(buf, maxLen, "%llu", (unsigned long long int)slot->uintVal);                      break;
        case TYPE_BOOL:     len = snprintf(buf, maxLen, slot->intVal ? "true" : "false");                                    break;
        case TYPE_CHAR:
        {
            const char *format = (unsigned char)slot->intVal >= ' ' ? "'%c'" : "0x%02X";
            len = snprintf(buf, maxLen, format, (unsigned char)slot->intVal);
            break;
        }
        case TYPE_REAL32:
        case TYPE_REAL:     len = snprintf(buf, maxLen, "%lg", slot->realVal);                                               break;
        case TYPE_PTR:      len = snprintf(buf, maxLen, "%p", slot->ptrVal);                                                 break;
        case TYPE_WEAKPTR:  len = snprintf(buf, maxLen, "%llx", (unsigned long long int)slot->weakPtrVal);                   break;
        case TYPE_STR:
        {
            doCheckStr((char *)slot->ptrVal, error);
            len = snprintf(buf, maxLen, "\"%s\"", slot->ptrVal ? (char *)slot->ptrVal : "");
            break;
        }
        case TYPE_ARRAY:
        {
            len += snprintf(buf, maxLen, "[");

            char *itemPtr = (char *)slot->ptrVal;
            int itemSize = typeSizeNoCheck(type->base);

            for (int i = 0; i < type->numItems; i++)
            {
                Slot itemSlot = {.ptrVal = itemPtr};
                doBasicDeref(&itemSlot, type->base->kind, error);
                len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, maxDepth - 1, error);
                if (i < type->numItems - 1)
                    len += snprintf(buf + len, maxLen, " ");
                itemPtr += itemSize;
            }

            len += snprintf(buf + len, maxLen, "]");
            break;
        }

        case TYPE_DYNARRAY:
        {
            len += snprintf(buf, maxLen, "[");

            DynArray *array = (DynArray *)slot->ptrVal;
            if (array && array->data)
            {
                char *itemPtr = array->data;
                for (int i = 0; i < getDims(array)->len; i++)
                {
                    Slot itemSlot = {.ptrVal = itemPtr};
                    doBasicDeref(&itemSlot, type->base->kind, error);
                    len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, maxDepth - 1, error);
                    if (i < getDims(array)->len - 1)
                        len += snprintf(buf + len, maxLen, " ");
                    itemPtr += array->itemSize;
                }
            }

            len += snprintf(buf + len, maxLen, "]");
            break;
        }

        case TYPE_MAP:
        {
            len += snprintf(buf, maxLen, "{");

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
                    doBasicDeref(&keySlot, keyType->kind, error);
                    len += doFillReprBuf(&keySlot, keyType, buf + len, maxLen, maxDepth - 1, error);

                    len += snprintf(buf + len, maxLen, ": ");

                    MapNode *node = doGetMapNode(map, keySlot, false, NULL, error, NULL);
                    if (!node)
                        error->runtimeHandler(error->context, "Map node is null");

                    Slot itemSlot = {.ptrVal = node->data};
                    doBasicDeref(&itemSlot, itemType->kind, error);
                    len += doFillReprBuf(&itemSlot, itemType, buf + len, maxLen, maxDepth - 1, error);

                    if (i < map->root->len - 1)
                        len += snprintf(buf + len, maxLen, " ");

                    keyPtr += keySize;
                }

                free(keys);
            }

            len += snprintf(buf + len, maxLen, "}");
            break;
        }


        case TYPE_STRUCT:
        case TYPE_CLOSURE:
        {
            len += snprintf(buf, maxLen, "{");
            bool skipNames = typeExprListStruct(type);

            for (int i = 0; i < type->numItems; i++)
            {
                Slot fieldSlot = {.ptrVal = (char *)slot->ptrVal + type->field[i]->offset};
                doBasicDeref(&fieldSlot, type->field[i]->type->kind, error);
                if (!skipNames)
                    len += snprintf(buf + len, maxLen, "%s: ", type->field[i]->name);
                len += doFillReprBuf(&fieldSlot, type->field[i]->type, buf + len, maxLen, maxDepth - 1, error);
                if (i < type->numItems - 1)
                    len += snprintf(buf + len, maxLen, " ");
            }

            len += snprintf(buf + len, maxLen, "}");
            break;
        }

        case TYPE_INTERFACE:
        {
            Interface *interface = (Interface *)slot->ptrVal;
            if (interface->self)
            {
                Slot selfSlot = {.ptrVal = interface->self};
                doBasicDeref(&selfSlot, interface->selfType->base->kind, error);
                len += doFillReprBuf(&selfSlot, interface->selfType->base, buf + len, maxLen, maxDepth - 1, error);
            }
            else
                len += snprintf(buf, maxLen, "null");
            break;
        }

        case TYPE_FIBER:    len = snprintf(buf, maxLen, "fiber @ %p", slot->ptrVal);                break;
        case TYPE_FN:       len = snprintf(buf, maxLen, "fn @ %lld", (long long int)slot->intVal);  break;
        default:            break;
    }

    return len;
}


static FORCE_INLINE void doCheckFormatString(const char *format, int *formatLen, int *typeLetterPos, TypeKind *typeKind, Error *error)
{
    enum {SIZE_SHORT_SHORT, SIZE_SHORT, SIZE_NORMAL, SIZE_LONG, SIZE_LONG_LONG} size;
    *typeKind = TYPE_VOID;
    int i = 0;

    while (format[i])
    {
        size = SIZE_NORMAL;
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
                size = SIZE_SHORT;
                i++;

                if (format[i] == 'h')
                {
                    size = SIZE_SHORT_SHORT;
                    i++;
                }
            }
            else if (format[i] == 'l')
            {
                size = SIZE_LONG;
                i++;

                if (format[i] == 'l')
                {
                    size = SIZE_LONG_LONG;
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
                    switch (size)
                    {
                        case SIZE_SHORT_SHORT:  *typeKind = TYPE_INT8;      break;
                        case SIZE_SHORT:        *typeKind = TYPE_INT16;     break;
                        case SIZE_NORMAL:
                        case SIZE_LONG:         *typeKind = TYPE_INT32;     break;
                        case SIZE_LONG_LONG:    *typeKind = TYPE_INT;       break;
                    }
                    break;
                }
                case 'u':
                case 'x':
                case 'X':
                {
                    switch (size)
                    {
                        case SIZE_SHORT_SHORT:  *typeKind = TYPE_UINT8;      break;
                        case SIZE_SHORT:        *typeKind = TYPE_UINT16;     break;
                        case SIZE_NORMAL:
                        case SIZE_LONG:         *typeKind = TYPE_UINT32;     break;
                        case SIZE_LONG_LONG:    *typeKind = TYPE_UINT;       break;
                    }
                    break;
                }
                case 'f':
                case 'F':
                case 'e':
                case 'E':
                case 'g':
                case 'G': *typeKind = (size == SIZE_NORMAL) ? TYPE_REAL32 : TYPE_REAL;      break;
                case 's': *typeKind = TYPE_STR;                                             break;
                case 'c': *typeKind = TYPE_CHAR;                                            break;
                case 'v': *typeKind = TYPE_INTERFACE;  /* Actually any type */              break;

                default : error->runtimeHandler(error->context, "Illegal type character %c in format string", format[i]);
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
        default:                error->runtimeHandler(error->context, "Illegal type"); break;
    }

    return len;
}


static FORCE_INLINE void doBuiltinPrintf(Fiber *fiber, HeapPages *pages, bool console, bool string, Error *error)
{
    enum {STACK_OFFSET_COUNT = 4, STACK_OFFSET_STREAM = 3, STACK_OFFSET_FORMAT = 2, STACK_OFFSET_VALUE = 1, STACK_OFFSET_TYPE = 0};

    const int prevLen  = fiber->top[STACK_OFFSET_COUNT].intVal;
    void *stream       = console ? stdout : fiber->top[STACK_OFFSET_STREAM].ptrVal;
    const char *format = (const char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal;
    Slot value         = fiber->top[STACK_OFFSET_VALUE];
    Type *type         = (Type *)fiber->top[STACK_OFFSET_TYPE].ptrVal;

    TypeKind typeKind  = type->kind;

    if (!string && (!stream || (!fiber->fileSystemEnabled && !console)))
        error->runtimeHandler(error->context, "printf() destination is null");

    if (!format)
        format = doGetEmptyStr();

    int formatLen = -1, typeLetterPos = -1;
    TypeKind expectedTypeKind = TYPE_NONE;
    doCheckFormatString(format, &formatLen, &typeLetterPos, &expectedTypeKind, error);

    const bool hasAnyTypeFormatter = expectedTypeKind == TYPE_INTERFACE && typeLetterPos >= 0 && typeLetterPos < formatLen;     // %v

    if (type->kind != expectedTypeKind &&
        !(type->kind != TYPE_VOID && expectedTypeKind == TYPE_INTERFACE) &&
        !(typeKindInteger(type->kind) && typeKindInteger(expectedTypeKind)) &&
        !(typeKindReal(type->kind)    && typeKindReal(expectedTypeKind)))
    {
        error->runtimeHandler(error->context, "Incompatible types %s and %s in printf()", typeKindSpelling(expectedTypeKind), typeKindSpelling(type->kind));
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
            error->runtimeHandler(error->context, "Overflow of %s", typeKindSpelling(expectedTypeKind));
    }

    char curFormatBuf[DEFAULT_STR_LEN + 1];
    bool isCurFormatBufInHeap = formatLen + 1 > sizeof(curFormatBuf);
    char *curFormat = isCurFormatBufInHeap ? malloc(formatLen + 1) : &curFormatBuf;

    memcpy(curFormat, format, formatLen);
    curFormat[formatLen] = 0;

    // Special case: %v formatter - convert argument of any type to its string representation
    char reprBuf[sizeof(StrDimensions) + DEFAULT_STR_LEN + 1];
    char isReprBufInHeap = false;
    char *dimsAndRepr = NULL;

    if (hasAnyTypeFormatter)
    {
        curFormat[typeLetterPos] = 's';

        enum {MAX_NESTING = 20};
        const int reprLen = doFillReprBuf(&value, type, NULL, 0, MAX_NESTING, error);  // Predict buffer length

        isReprBufInHeap = sizeof(StrDimensions) + reprLen + 1 > sizeof(reprBuf);
        dimsAndRepr = isReprBufInHeap ? malloc(sizeof(StrDimensions) + reprLen + 1) : &reprBuf;

        StrDimensions dims = {.len = reprLen, .capacity = reprLen + 1};
        *(StrDimensions *)dimsAndRepr = dims;

        char *repr = dimsAndRepr + sizeof(StrDimensions);
        repr[reprLen] = 0;

        doFillReprBuf(&value, type, repr, reprLen + 1, MAX_NESTING, error);            // Fill buffer

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
            doBasicChangeRefCnt(fiber, pages, stream, &strType, TOK_MINUSMINUS);

            stream = newStream;
        }

        len = doPrintSlot(true, (char *)stream + prevLen, len + 1, curFormat, value, typeKind, error);
    }
    else
        len = doPrintSlot(false, stream, INT_MAX, curFormat, value, typeKind, error);

    fiber->top[STACK_OFFSET_FORMAT].ptrVal = (char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal + formatLen;
    fiber->top[STACK_OFFSET_COUNT].intVal += len;
    fiber->top[STACK_OFFSET_STREAM].ptrVal = stream;

    if (isCurFormatBufInHeap)
        free(curFormat);

    if (isReprBufInHeap)
        free(dimsAndRepr);
}


static FORCE_INLINE void doBuiltinScanf(Fiber *fiber, HeapPages *pages, bool console, bool string, Error *error)
{
    enum {STACK_OFFSET_COUNT = 3, STACK_OFFSET_STREAM = 2, STACK_OFFSET_FORMAT = 1, STACK_OFFSET_VALUE = 0};

    void *stream       = console ? stdin : (void *)fiber->top[STACK_OFFSET_STREAM].ptrVal;
    const char *format = (const char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal;
    TypeKind typeKind  = fiber->code[fiber->ip].typeKind;

    if (!stream || (!fiber->fileSystemEnabled && !console && !string))
        error->runtimeHandler(error->context, "scanf() source is null");

    if (!format)
        format = doGetEmptyStr();

    int formatLen = -1, typeLetterPos = -1;
    TypeKind expectedTypeKind = TYPE_NONE;
    doCheckFormatString(format, &formatLen, &typeLetterPos, &expectedTypeKind, error);

    if (typeKind != expectedTypeKind || expectedTypeKind == TYPE_INTERFACE)
    {
        error->runtimeHandler(error->context, "Incompatible types %s and %s in scanf()", typeKindSpelling(expectedTypeKind), typeKindSpelling(typeKind));
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
        if (!fiber->top->ptrVal)
            error->runtimeHandler(error->context, "scanf() destination is null");

        // Strings need special handling, as the required buffer size is unknown
        if (typeKind == TYPE_STR)
        {
            char *src = fsscanfString(string, stream, &len);
            char **dest = (char **)fiber->top->ptrVal;

            // Decrease old string ref count
            Type destType = {.kind = TYPE_STR};
            doBasicChangeRefCnt(fiber, pages, *dest, &destType, TOK_MINUSMINUS);

            // Allocate new string
            *dest = doAllocStr(pages, strlen(src), error);
            strcpy(*dest, src);
            free(src);

            cnt = (*dest)[0] ? 1 : 0;
        }
        else
            cnt = fsscanf(string, stream, curFormat, (void *)fiber->top->ptrVal, &len);
    }

    fiber->top[STACK_OFFSET_FORMAT].ptrVal = (char *)fiber->top[STACK_OFFSET_FORMAT].ptrVal + formatLen;
    fiber->top[STACK_OFFSET_COUNT].intVal += cnt;
    if (string)
        fiber->top[STACK_OFFSET_STREAM].ptrVal = (char *)fiber->top[STACK_OFFSET_STREAM].ptrVal + len;

    if (isCurFormatBufInHeap)
        free(curFormat);
}


// fn new(type: Type, size: int [, expr: type]): ^type
static FORCE_INLINE void doBuiltinNew(Fiber *fiber, HeapPages *pages, Error *error)
{
    int size     = (fiber->top++)->intVal;
    Type *type   = (Type *)(fiber->top++)->ptrVal;

    // For dynamic arrays, we mark with type the data chunk, not the header chunk
    if (type && type->kind == TYPE_DYNARRAY)
        type = NULL;

    void *result = chunkAlloc(pages, size, type, NULL, false, error);

    (--fiber->top)->ptrVal = result;
}


// fn make(type: Type [, len: int]): type
static FORCE_INLINE void doBuiltinMake(Fiber *fiber, HeapPages *pages, Error *error)
{
    void *result = (fiber->top++)->ptrVal;
    int len      = (fiber->top++)->intVal;
    Type *type   = (Type *)(fiber->top++)->ptrVal;

    if (type->kind == TYPE_DYNARRAY)
        doAllocDynArray(pages, (DynArray *)result, type, len, error);
    else // TYPE_MAP
        doAllocMap(pages, (Map *)result, type, error);

    (--fiber->top)->ptrVal = result;
}


// fn makefromarr(src: [...]ItemType, type: Type, len: int): type
static FORCE_INLINE void doBuiltinMakefromarr(Fiber *fiber, HeapPages *pages, Error *error)
{
    doBuiltinMake(fiber, pages, error);

    DynArray *dest = (DynArray *)(fiber->top++)->ptrVal;
    void *src      = (fiber->top++)->ptrVal;

    memcpy(dest->data, src, getDims(dest)->len * dest->itemSize);

    // Increase result items' ref counts, as if they have been assigned one by one
    Type staticArrayType = {.kind = TYPE_ARRAY, .base = dest->type->base, .numItems = getDims(dest)->len, .next = NULL};
    doBasicChangeRefCnt(fiber, pages, dest->data, &staticArrayType, TOK_PLUSPLUS);

    (--fiber->top)->ptrVal = dest;
}


// fn makefromstr(src: str, type: Type): []char
static FORCE_INLINE void doBuiltinMakefromstr(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *dest   = (DynArray   *)(fiber->top++)->ptrVal;
    Type *destType   = (Type       *)(fiber->top++)->ptrVal;
    const char *src  = (const char *)(fiber->top++)->ptrVal;

    if (!src)
        src = doGetEmptyStr();

    doAllocDynArray(pages, dest, destType, getStrDims(src)->len, error);
    memcpy(dest->data, src, getDims(dest)->len);

    (--fiber->top)->ptrVal = dest;
}


// fn maketoarr(src: []ItemType, type: Type): [...]ItemType
static FORCE_INLINE void doBuiltinMaketoarr(Fiber *fiber, HeapPages *pages, Error *error)
{
    void *dest     = (fiber->top++)->ptrVal;
    Type *destType = (Type     *)(fiber->top++)->ptrVal;
    DynArray *src  = (DynArray *)(fiber->top++)->ptrVal;

    if (!src)
        error->runtimeHandler(error->context, "Dynamic array is null");

    memset(dest, 0, typeSizeNoCheck(destType));

    if (src->data)
    {
        if (getDims(src)->len > destType->numItems)
            error->runtimeHandler(error->context, "Dynamic array is too long");

        memcpy(dest, src->data, getDims(src)->len * src->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        doBasicChangeRefCnt(fiber, pages, dest, destType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = dest;
}


// fn maketostr(src: char | []char): str
static FORCE_INLINE void doBuiltinMaketostr(Fiber *fiber, HeapPages *pages, TypeKind typeKind, Error *error)
{
    char *dest = doGetEmptyStr();

    if (typeKind == TYPE_CHAR)
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
            error->runtimeHandler(error->context, "Dynamic array is null");

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
        error->runtimeHandler(error->context, "Dynamic array is null");

    *result = *array;

    if (array->data)
    {
        doAllocDynArray(pages, result, array->type, getDims(array)->len, error);
        memmove((char *)result->data, (char *)array->data, getDims(array)->len * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = getDims(result)->len, .next = NULL};
        doBasicChangeRefCnt(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn copy(m: map [keyType] type): map [keyType] type
static FORCE_INLINE void doBuiltinCopyMap(Fiber *fiber, HeapPages *pages, Error *error)
{
    Map *result = (Map *)(fiber->top++)->ptrVal;
    Map *map    = (Map *)(fiber->top++)->ptrVal;

    if (!map)
        error->runtimeHandler(error->context, "Map is null");

    if (!map->root)
        *result = *map;
    else
    {
        result->type = map->type;
        result->root = doCopyMapNode(map, map->root, fiber, pages, error);
    }

    (--fiber->top)->ptrVal = result;
}


static FORCE_INLINE void doBuiltinCopy(Fiber *fiber, HeapPages *pages, TypeKind typeKind, Error *error)
{
    if (typeKind == TYPE_DYNARRAY)
        doBuiltinCopyDynArray(fiber, pages, error);
    else
        doBuiltinCopyMap(fiber, pages, error);
}


// fn append(array: [] type, item: (^type | [] type), single: bool, type: Type): [] type
static FORCE_INLINE void doBuiltinAppend(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    Type *arrayType  = (Type     *)(fiber->top++)->ptrVal;
    bool single      = (bool      )(fiber->top++)->intVal;
    void *item       = (fiber->top++)->ptrVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    if (!array)
        error->runtimeHandler(error->context, "Dynamic array is null");

    if (!array->data)
        doGetEmptyDynArray(array, arrayType);

    void *rhs = item;
    int rhsLen = 1;

    if (!single)
    {
        DynArray *rhsArray = item;

        if (!rhsArray)
            error->runtimeHandler(error->context, "Dynamic array is null");

        if (!rhsArray->data)
            doGetEmptyDynArray(rhsArray, arrayType);

        rhs = rhsArray->data;
        rhsLen = getDims(rhsArray)->len;
    }

    int newLen = getDims(array)->len + rhsLen;

    if (newLen <= getDims(array)->capacity)
    {
        doBasicChangeRefCnt(fiber, pages, array, array->type, TOK_PLUSPLUS);
        *result = *array;

        memmove((char *)result->data + getDims(array)->len * array->itemSize, (char *)rhs, rhsLen * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = rhsLen, .next = NULL};
        doBasicChangeRefCnt(fiber, pages, (char *)result->data + getDims(array)->len * array->itemSize, &staticArrayType, TOK_PLUSPLUS);

        getDims(result)->len = newLen;
    }
    else
    {
        doAllocDynArray(pages, result, array->type, newLen, error);

        memmove((char *)result->data, (char *)array->data, getDims(array)->len * array->itemSize);
        memmove((char *)result->data + getDims(array)->len * array->itemSize, (char *)rhs, rhsLen * array->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = newLen, .next = NULL};
        doBasicChangeRefCnt(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}


// fn insert(array: [] type, index: int, item: type, type: Type): [] type
static FORCE_INLINE void doBuiltinInsert(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    Type *arrayType  = (Type     *)(fiber->top++)->ptrVal;
    void *item       = (fiber->top++)->ptrVal;
    int64_t index    = (fiber->top++)->intVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    if (!array)
        error->runtimeHandler(error->context, "Dynamic array is null");

    if (!array->data)
        doGetEmptyDynArray(array, arrayType);

    if (index < 0 || index > getDims(array)->len)
        error->runtimeHandler(error->context, "Index %lld is out of range 0...%lld", index, getDims(array)->len);

    if (getDims(array)->len + 1 <= getDims(array)->capacity)
    {
        doBasicChangeRefCnt(fiber, pages, array, array->type, TOK_PLUSPLUS);
        *result = *array;

        memmove((char *)result->data + (index + 1) * result->itemSize, (char *)result->data + index * result->itemSize, (getDims(array)->len - index) * result->itemSize);
        memmove((char *)result->data + index * result->itemSize, (char *)item, result->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = 1, .next = NULL};
        doBasicChangeRefCnt(fiber, pages, (char *)result->data + index * result->itemSize, &staticArrayType, TOK_PLUSPLUS);

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
        doBasicChangeRefCnt(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
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
        error->runtimeHandler(error->context, "Dynamic array is null");

    if (index < 0 || index > getDims(array)->len - 1)
        error->runtimeHandler(error->context, "Index %lld is out of range 0...%lld", index, getDims(array)->len - 1);

    doBasicChangeRefCnt(fiber, pages, array, array->type, TOK_PLUSPLUS);
    *result = *array;

    // Decrease result item's ref count
    Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = 1, .next = NULL};
    doBasicChangeRefCnt(fiber, pages, (char *)result->data + index * result->itemSize, &staticArrayType, TOK_MINUSMINUS);

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
        error->runtimeHandler(error->context, "Map is null");

    MapNode **nodePtrInParent = NULL;
    MapNode *node = doGetMapNode(map, key, false, pages, error, &nodePtrInParent);

    if (node)
    {
        doBasicChangeRefCnt(fiber, pages, *nodePtrInParent, typeMapNodePtr(map->type), TOK_MINUSMINUS);
        *nodePtrInParent = NULL;
        if (--map->root->len < 0)
            error->runtimeHandler(error->context, "Map length is negative");
    }

    doBasicChangeRefCnt(fiber, pages, map->root, typeMapNodePtr(map->type), TOK_PLUSPLUS);
    result->type = map->type;
    result->root = map->root;

    (--fiber->top)->ptrVal = result;
}


static FORCE_INLINE void doBuiltinDelete(Fiber *fiber, HeapPages *pages, TypeKind typeKind, Error *error)
{
    if (typeKind == TYPE_DYNARRAY)
        doBuiltinDeleteDynArray(fiber, pages, error);
    else
        doBuiltinDeleteMap(fiber, pages, error);
}


// fn slice(array: [] type | str, startIndex [, endIndex]: int): [] type | str
static FORCE_INLINE void doBuiltinSlice(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result   = (DynArray *)(fiber->top++)->ptrVal;
    Type *argType      = (Type     *)(fiber->top++)->ptrVal;
    int64_t endIndex   = (fiber->top++)->intVal;
    int64_t startIndex = (fiber->top++)->intVal;
    void *arg          = (fiber->top++)->ptrVal;

    DynArray *array = NULL;
    const char *str = NULL;
    int64_t len = 0;

    if (result)
    {
        // Dynamic array
        array = (DynArray *)arg;

        if (!array)
            error->runtimeHandler(error->context, "Dynamic array is null");

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
        error->runtimeHandler(error->context, "Index %lld is out of range 0...%lld", startIndex, len);

    if (endIndex < startIndex || endIndex > len)
        error->runtimeHandler(error->context, "Index %lld is out of range %lld...%lld", endIndex, startIndex, len);

    if (result)
    {
        // Dynamic array
        doAllocDynArray(pages, result, array->type, endIndex - startIndex, error);

        memcpy((char *)result->data, (char *)array->data + startIndex * result->itemSize, getDims(result)->len * result->itemSize);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = getDims(result)->len, .next = NULL};
        doBasicChangeRefCnt(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);

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


static FORCE_INLINE void doBuiltinLen(Fiber *fiber, Error *error)
{
    switch (fiber->code[fiber->ip].typeKind)
    {
        // Done at compile time for arrays
        case TYPE_DYNARRAY:
        {
            const DynArray *array = (DynArray *)(fiber->top->ptrVal);
            if (!array)
                error->runtimeHandler(error->context, "Dynamic array is null");

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
                error->runtimeHandler(error->context, "Map is null");

            fiber->top->intVal =  map->root ? map->root->len : 0;
            break;
        }
        default:
            error->runtimeHandler(error->context, "Illegal type"); return;
    }
}


static FORCE_INLINE void doBuiltinCap(Fiber *fiber, Error *error)
{
    const DynArray *array = (DynArray *)(fiber->top->ptrVal);
    if (!array)
        error->runtimeHandler(error->context, "Dynamic array is null");

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
            isValid = closure->entryOffset > 0;
            break;
        }
        case TYPE_FIBER:
        {
            Fiber *child = (Fiber *)fiber->top->ptrVal;
            isValid = child;
            break;
        }
        default:
            error->runtimeHandler(error->context, "Illegal type"); return;
    }

    fiber->top->intVal = isValid;
}


// fn validkey(m: map [keyType] type, key: keyType): bool
static FORCE_INLINE void doBuiltinValidkey(Fiber *fiber, HeapPages *pages, Error *error)
{
    Slot key  = *fiber->top++;
    Map *map  = (Map *)(fiber->top++)->ptrVal;

    if (!map)
        error->runtimeHandler(error->context, "Map is null");

    bool isValid = false;

    if (map->root)
    {
        MapNode *node = doGetMapNode(map, key, false, pages, error, NULL);
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
        error->runtimeHandler(error->context, "Map is null");

    doAllocDynArray(pages, result, resultType, map->root ? map->root->len : 0, error);

    if (map->root)
    {
        doGetMapKeys(map, result->data, error);

        // Increase result items' ref counts, as if they have been assigned one by one
        Type staticArrayType = {.kind = TYPE_ARRAY, .base = result->type->base, .numItems = getDims(result)->len, .next = NULL};
        doBasicChangeRefCnt(fiber, pages, result->data, &staticArrayType, TOK_PLUSPLUS);
    }

    (--fiber->top)->ptrVal = result;
}



// type FiberFunc = fn(parent: fiber, anyParam: ^type)
// fn fiberspawn(childFunc: FiberFunc, anyParam: ^type): fiber
static FORCE_INLINE void doBuiltinFiberspawn(Fiber *fiber, HeapPages *pages, Error *error)
{
    void *anyParam = (fiber->top++)->ptrVal;
    int childEntryOffset = (fiber->top++)->intVal;

    // Copy whole fiber context
    Fiber *child = chunkAlloc(pages, sizeof(Fiber), NULL, NULL, false, error);

    *child = *fiber;
    child->stack = chunkAlloc(pages, child->stackSize * sizeof(Slot), NULL, NULL, true, error);
    child->top = child->base = child->stack + child->stackSize - 1;

    // Call child fiber function
    for (int i = 0; i < sizeof(Interface) / sizeof(Slot); i++)
        (--child->top)->intVal = 0;                     // Push dummy upvalues

    (--child->top)->ptrVal = fiber;                     // Push parent fiber pointer
    (--child->top)->ptrVal = anyParam;                  // Push arbitrary pointer parameter
    (--child->top)->intVal = VM_FIBER_KILL_SIGNAL;      // Push fiber kill signal instead of return address
    child->ip = childEntryOffset;                       // Call

    // Return child fiber pointer to parent fiber as result
    (--fiber->top)->ptrVal = child;
}


// fn fibercall(child: fiber)
static FORCE_INLINE void doBuiltinFibercall(Fiber *fiber, Fiber **newFiber, HeapPages *pages, Error *error)
{
    *newFiber = (Fiber *)(fiber->top++)->ptrVal;
    if (!(*newFiber) || !(*newFiber)->alive)
        error->runtimeHandler(error->context, "Fiber is null");
}


// fn fiberalive(child: fiber)
static FORCE_INLINE void doBuiltinFiberalive(Fiber *fiber, HeapPages *pages, Error *error)
{
    Fiber *child = (Fiber *)fiber->top->ptrVal;
    if (!child)
        error->runtimeHandler(error->context, "Fiber is null");

    fiber->top->intVal = child->alive;
}


static FORCE_INLINE void doPush(Fiber *fiber, Error *error)
{
    (--fiber->top)->intVal = fiber->code[fiber->ip].operand.intVal;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doPushZero(Fiber *fiber, Error *error)
{
    int slots = fiber->code[fiber->ip].operand.intVal;

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error->runtimeHandler(error->context, "Stack overflow");

    fiber->top -= slots;
    memset(fiber->top, 0, slots * sizeof(Slot));

    fiber->ip++;
}


static FORCE_INLINE void doPushLocalPtr(Fiber *fiber)
{
    // Local variable addresses are offsets (in bytes) from the stack/heap frame base pointer
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->code[fiber->ip].operand.intVal;
    fiber->ip++;
}


static FORCE_INLINE void doPushLocal(Fiber *fiber, Error *error)
{
    // Local variable addresses are offsets (in bytes) from the stack/heap frame base pointer
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->code[fiber->ip].operand.intVal;
    doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);
    fiber->ip++;
}


static FORCE_INLINE void doPushReg(Fiber *fiber)
{
    (--fiber->top)->intVal = fiber->reg[fiber->code[fiber->ip].operand.intVal].intVal;
    fiber->ip++;
}


static FORCE_INLINE void doPushStruct(Fiber *fiber, Error *error)
{
    void *src = (fiber->top++)->ptrVal;
    int size = fiber->code[fiber->ip].operand.intVal;
    int slots = align(size, sizeof(Slot)) / sizeof(Slot);

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error->runtimeHandler(error->context, "Stack overflow");

    fiber->top -= slots;
    memcpy(fiber->top, src, size);

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
    doBasicSwap(fiber->top);
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
    doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);
    fiber->ip++;
}


static FORCE_INLINE void doAssign(Fiber *fiber, Error *error)
{
    if (fiber->code[fiber->ip].inlineOpcode == OP_SWAP)
        doBasicSwap(fiber->top);

    Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;

    doBasicAssign(lhs, rhs, fiber->code[fiber->ip].typeKind, fiber->code[fiber->ip].operand.intVal, error);
    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCnt(Fiber *fiber, HeapPages *pages)
{
    void *ptr         = fiber->top->ptrVal;
    TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    Type *type        = (Type *)fiber->code[fiber->ip].operand.ptrVal;

    doBasicChangeRefCnt(fiber, pages, ptr, type, tokKind);

    if (fiber->code[fiber->ip].inlineOpcode == OP_POP)
        fiber->top++;

    fiber->ip++;
}


static FORCE_INLINE void doChangeRefCntAssign(Fiber *fiber, HeapPages *pages, Error *error)
{
    if (fiber->code[fiber->ip].inlineOpcode == OP_SWAP)
        doBasicSwap(fiber->top);

    Slot rhs   = *fiber->top++;
    void *lhs  = (fiber->top++)->ptrVal;
    Type *type = (Type *)fiber->code[fiber->ip].operand.ptrVal;

    // Increase right-hand side ref count
    if (fiber->code[fiber->ip].tokKind != TOK_MINUSMINUS)      // "--" means that the right-hand side ref count should not be increased
        doBasicChangeRefCnt(fiber, pages, rhs.ptrVal, type, TOK_PLUSPLUS);

    // Decrease left-hand side ref count
    Slot lhsDeref = {.ptrVal = lhs};
    doBasicDeref(&lhsDeref, type->kind, error);
    doBasicChangeRefCnt(fiber, pages, lhsDeref.ptrVal, type, TOK_MINUSMINUS);

    doBasicAssign(lhs, rhs, type->kind, typeSizeNoCheck(type), error);
    fiber->ip++;
}


static FORCE_INLINE void doUnary(Fiber *fiber, Error *error)
{
    if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  break;
            case TOK_MINUS: fiber->top->realVal = -fiber->top->realVal; break;
            default:        error->runtimeHandler(error->context, "Illegal instruction"); return;
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
                    default:          error->runtimeHandler(error->context, "Illegal type"); return;
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
                    default:          error->runtimeHandler(error->context, "Illegal type"); return;
                }
            break;
            }

            default: error->runtimeHandler(error->context, "Illegal instruction"); return;
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

            default:            error->runtimeHandler(error->context, "Illegal instruction"); return;
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
                    doBasicChangeRefCnt(fiber, pages, buf, &strType, TOK_PLUSPLUS);
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

            default:            error->runtimeHandler(error->context, "Illegal instruction"); return;
        }
    }
    else if (fiber->code[fiber->ip].typeKind == TYPE_ARRAY || fiber->code[fiber->ip].typeKind == TYPE_STRUCT)
    {
        const int structSize = fiber->code[fiber->ip].operand.intVal;

        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_EQEQ:      fiber->top->intVal = memcmp(fiber->top->ptrVal, rhs.ptrVal, structSize) == 0; break;
            case TOK_NOTEQ:     fiber->top->intVal = memcmp(fiber->top->ptrVal, rhs.ptrVal, structSize) != 0; break;

            default:            error->runtimeHandler(error->context, "Illegal instruction"); return;
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
                    error->runtimeHandler(error->context, "Division by zero");
                fiber->top->realVal /= rhs.realVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.realVal == 0)
                    error->runtimeHandler(error->context, "Division by zero");
                fiber->top->realVal = fmod(fiber->top->realVal, rhs.realVal);
                break;
            }

            case TOK_EQEQ:      fiber->top->intVal = fiber->top->realVal == rhs.realVal; break;
            case TOK_NOTEQ:     fiber->top->intVal = fiber->top->realVal != rhs.realVal; break;
            case TOK_GREATER:   fiber->top->intVal = fiber->top->realVal >  rhs.realVal; break;
            case TOK_LESS:      fiber->top->intVal = fiber->top->realVal <  rhs.realVal; break;
            case TOK_GREATEREQ: fiber->top->intVal = fiber->top->realVal >= rhs.realVal; break;
            case TOK_LESSEQ:    fiber->top->intVal = fiber->top->realVal <= rhs.realVal; break;

            default:            error->runtimeHandler(error->context, "Illegal instruction"); return;
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
                    error->runtimeHandler(error->context, "Division by zero");
                fiber->top->uintVal /= rhs.uintVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.uintVal == 0)
                    error->runtimeHandler(error->context, "Division by zero");
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

            default:            error->runtimeHandler(error->context, "Illegal instruction"); return;
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
                    error->runtimeHandler(error->context, "Division by zero");
                fiber->top->intVal /= rhs.intVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.intVal == 0)
                    error->runtimeHandler(error->context, "Division by zero");
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

            default:            error->runtimeHandler(error->context, "Illegal instruction"); return;
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
            error->runtimeHandler(error->context, "Array is null");
    }
    else            // For strings, negative length means that the actual string length is to be used
    {
        if (!data)
            data = doGetEmptyStr();
        doCheckStr(data, error);
        len = getStrDims(data)->len;
    }

    if (index < 0 || index > len - 1)
        error->runtimeHandler(error->context, "Index %d is out of range 0...%d", index, len - 1);

    fiber->top->ptrVal = data + itemSize * index;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doGetDynArrayPtr(Fiber *fiber, Error *error)
{
    int index       = (fiber->top++)->intVal;
    DynArray *array = (DynArray *)(fiber->top++)->ptrVal;

    if (!array || !array->data)
        error->runtimeHandler(error->context, "Dynamic array is null");

    int itemSize    = array->itemSize;
    int len         = getDims(array)->len;

    if (index < 0 || index > len - 1)
        error->runtimeHandler(error->context, "Index %d is out of range 0...%d", index, len - 1);

    (--fiber->top)->ptrVal = (char *)array->data + itemSize * index;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doGetMapPtr(Fiber *fiber, HeapPages *pages, Error *error)
{
    Slot key  = *fiber->top++;
    Map *map  = (Map *)(fiber->top++)->ptrVal;

    Type *mapType = (Type *)fiber->code[fiber->ip].operand.ptrVal;

    if (!map)
        error->runtimeHandler(error->context, "Map is null");

    if (!map->root)
        doAllocMap(pages, map, mapType, error);

    Type *keyType = typeMapKey(map->type);
    Type *itemType = typeMapItem(map->type);

    MapNode *node = doGetMapNode(map, key, true, pages, error, NULL);
    if (!node->data)
    {
        // When allocating dynamic arrays, we mark with type the data chunk, not the header chunk
        node->key  = chunkAlloc(pages, typeSizeNoCheck(keyType),  keyType->kind  == TYPE_DYNARRAY ? NULL : keyType,  NULL, false, error);
        node->data = chunkAlloc(pages, typeSizeNoCheck(itemType), itemType->kind == TYPE_DYNARRAY ? NULL : itemType, NULL, false, error);

        // Increase key ref count
        if (typeGarbageCollected(keyType))
            doBasicChangeRefCnt(fiber, pages, key.ptrVal, keyType, TOK_PLUSPLUS);

        doBasicAssign(node->key, key, keyType->kind, typeSizeNoCheck(keyType), error);
        map->root->len++;
    }

    (--fiber->top)->ptrVal = node->data;
    fiber->ip++;
}


static FORCE_INLINE void doGetFieldPtr(Fiber *fiber, Error *error)
{
    int fieldOffset = fiber->code[fiber->ip].operand.intVal;

    if (!fiber->top->ptrVal)
        error->runtimeHandler(error->context, "Array or structure is null");

    fiber->top->ptrVal = (char *)fiber->top->ptrVal + fieldOffset;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static FORCE_INLINE void doAssertType(Fiber *fiber)
{
    Interface *interface  = (Interface *)(fiber->top++)->ptrVal;
    Type *type            = (Type *)fiber->code[fiber->ip].operand.ptrVal;

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
        error->runtimeHandler(error->context, "Overflow of %s", typeKindSpelling(typeKind));

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
        error->runtimeHandler(error->context, "Called function is not defined");

    // Push return address and go to the entry point
    (--fiber->top)->intVal = fiber->ip + 1;
    fiber->ip = entryOffset;
}


static FORCE_INLINE void doCallExtern(Fiber *fiber, Error *error)
{
    ExternFunc fn = (ExternFunc)fiber->code[fiber->ip].operand.ptrVal;
    fiber->reg[VM_REG_RESULT].ptrVal = error->context;    // Upon entry, the result slot stores the Umka instance
    fn(fiber->base + 2, &fiber->reg[VM_REG_RESULT]);      // + 2 for old base pointer and return address
    fiber->ip++;
}


static FORCE_INLINE void doCallBuiltin(Fiber *fiber, Fiber **newFiber, HeapPages *pages, Error *error)
{
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
        case BUILTIN_NARROW:
        {
            Slot rhs = *fiber->top;
            doBasicAssign(fiber->top, rhs, typeKind, 0, error);
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
                error->runtimeHandler(error->context, "sqrt() domain error");
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
                error->runtimeHandler(error->context, "atan2() domain error");
            fiber->top->realVal = atan2(y, x);
            break;
        }
        case BUILTIN_EXP:           fiber->top->realVal = exp (fiber->top->realVal); break;
        case BUILTIN_LOG:
        {
            if (fiber->top->realVal <= 0)
                error->runtimeHandler(error->context, "log() domain error");
            fiber->top->realVal = log(fiber->top->realVal);
            break;
        }

        // Memory
        case BUILTIN_NEW:           doBuiltinNew(fiber, pages, error); break;
        case BUILTIN_MAKE:          doBuiltinMake(fiber, pages, error); break;
        case BUILTIN_MAKEFROMARR:   doBuiltinMakefromarr(fiber, pages, error); break;
        case BUILTIN_MAKEFROMSTR:   doBuiltinMakefromstr(fiber, pages, error); break;
        case BUILTIN_MAKETOARR:     doBuiltinMaketoarr(fiber, pages, error); break;
        case BUILTIN_MAKETOSTR:     doBuiltinMaketostr(fiber, pages, typeKind, error); break;
        case BUILTIN_COPY:          doBuiltinCopy(fiber, pages, typeKind, error); break;
        case BUILTIN_APPEND:        doBuiltinAppend(fiber, pages, error); break;
        case BUILTIN_INSERT:        doBuiltinInsert(fiber, pages, error); break;
        case BUILTIN_DELETE:        doBuiltinDelete(fiber, pages, typeKind, error); break;
        case BUILTIN_SLICE:         doBuiltinSlice(fiber, pages, error); break;
        case BUILTIN_LEN:           doBuiltinLen(fiber, error); break;
        case BUILTIN_CAP:           doBuiltinCap(fiber, error); break;
        case BUILTIN_SIZEOF:        error->runtimeHandler(error->context, "Illegal instruction"); return;       // Done at compile time
        case BUILTIN_SIZEOFSELF:    doBuiltinSizeofself(fiber, error); break;
        case BUILTIN_SELFHASPTR:    doBuiltinSelfhasptr(fiber, error); break;
        case BUILTIN_SELFTYPEEQ:    doBuiltinSelftypeeq(fiber, error); break;
        case BUILTIN_TYPEPTR:       error->runtimeHandler(error->context, "Illegal instruction"); return;       // Done at compile time
        case BUILTIN_VALID:         doBuiltinValid(fiber, error); break;

        // Maps
        case BUILTIN_VALIDKEY:      doBuiltinValidkey(fiber, pages, error); break;
        case BUILTIN_KEYS:          doBuiltinKeys(fiber, pages, error); break;

        // Fibers
        case BUILTIN_FIBERSPAWN:    doBuiltinFiberspawn(fiber, pages, error); break;
        case BUILTIN_FIBERCALL:     doBuiltinFibercall(fiber, newFiber, pages, error); break;
        case BUILTIN_FIBERALIVE:    doBuiltinFiberalive(fiber, pages, error); break;

        // Misc
        case BUILTIN_EXIT:          fiber->alive = false; break;
        case BUILTIN_ERROR:         error->runtimeHandler(error->context, "%s", (char *)fiber->top->ptrVal); return;
    }
    fiber->ip++;
}


static FORCE_INLINE void doReturn(Fiber *fiber, Fiber **newFiber)
{
    // Pop return address
    int returnOffset = (fiber->top++)->intVal;

    if (returnOffset == VM_FIBER_KILL_SIGNAL)
    {
        // For fiber function, kill the fiber, extract the parent fiber pointer and switch to it
        fiber->alive = false;
        *newFiber = (Fiber *)(fiber->top + 1)->ptrVal;
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
        error->runtimeHandler(error->context, "Stack overflow");

    // Push old stack frame base pointer, set new one
    (--fiber->top)->ptrVal = fiber->base;
    fiber->base = fiber->top;

    // Push stack frame ref count
    (--fiber->top)->intVal = 0;

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
        error->runtimeHandler(error->context, "Pointer to a local variable escapes from the function");

    // Call 'return' hook, if any
    doHook(fiber, hooks, HOOK_RETURN);

    // Restore stack top
    fiber->top = fiber->base;

    // Pop old stack/heap frame base pointer
    fiber->base = (Slot *)(fiber->top++)->ptrVal;

    fiber->ip++;
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
            error->runtimeHandler(error->context, "Stack overflow");

        switch (fiber->code[fiber->ip].opcode)
        {
            case OP_PUSH:                           doPush(fiber, error);                         break;
            case OP_PUSH_ZERO:                      doPushZero(fiber, error);                     break;
            case OP_PUSH_LOCAL_PTR:                 doPushLocalPtr(fiber);                        break;
            case OP_PUSH_LOCAL:                     doPushLocal(fiber, error);                    break;
            case OP_PUSH_REG:                       doPushReg(fiber);                             break;
            case OP_PUSH_STRUCT:                    doPushStruct(fiber, error);                   break;
            case OP_PUSH_UPVALUE:                   doPushUpvalue(fiber, pages, error);           break;
            case OP_POP:                            doPop(fiber);                                 break;
            case OP_POP_REG:                        doPopReg(fiber);                              break;
            case OP_DUP:                            doDup(fiber);                                 break;
            case OP_SWAP:                           doSwap(fiber);                                break;
            case OP_ZERO:                           doZero(fiber);                                break;
            case OP_DEREF:                          doDeref(fiber, error);                        break;
            case OP_ASSIGN:                         doAssign(fiber, error);                       break;
            case OP_CHANGE_REF_CNT:                 doChangeRefCnt(fiber, pages);                 break;
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
                if (fiber->top->intVal == 0)
                    return;

                Fiber *newFiber = NULL;
                doReturn(fiber, &newFiber);

                if (newFiber)
                    fiber = vm->fiber = vm->pages.fiber = newFiber;

                if (!fiber->alive)
                    return;

                break;
            }
            case OP_ENTER_FRAME:                    doEnterFrame(fiber, pages, hooks, error);     break;
            case OP_LEAVE_FRAME:                    doLeaveFrame(fiber, pages, hooks, error);     break;
            case OP_HALT:                           vm->terminatedNormally = true;                return;

            default: error->runtimeHandler(error->context, "Illegal instruction"); return;
        } // switch
    }
}


void vmRun(VM *vm, int entryOffset, int numParamSlots, Slot *params, Slot *result)
{
    if (entryOffset < 0)
        vm->error->runtimeHandler(vm->error->context, "Called function is not defined");

    if (!vm->fiber->alive)
        vm->error->runtimeHandler(vm->error->context, "Cannot run a dead fiber");

    // Individual function call
    if (entryOffset > 0)
    {
        // Push parameters
        const int numDummyUpvaluesSlots = sizeof(Interface) / sizeof(Slot);
        vm->fiber->top -= numDummyUpvaluesSlots + numParamSlots;

        for (int i = 0; i < numParamSlots; i++)
            vm->fiber->top[i] = params[i];
        for (int i = numParamSlots; i < numDummyUpvaluesSlots + numParamSlots; i++)
            vm->fiber->top[i].intVal = 0;

        // Push null return address and go to the entry point
        (--vm->fiber->top)->intVal = 0;
        vm->fiber->ip = entryOffset;
    }

    // Main loop
    vmLoop(vm);

    // Save result
    if (entryOffset > 0 && result)
        *result = vm->fiber->reg[VM_REG_RESULT];
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

    if (instr->typeKind != TYPE_NONE)
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
        case OP_PUSH_STRUCT:
        case OP_POP:
        case OP_POP_REG:
        case OP_ZERO:
        case OP_ASSIGN:
        case OP_BINARY:
        case OP_GET_FIELD_PTR:
        case OP_GOTO:
        case OP_GOTO_IF:
        case OP_ENTER_FRAME:
        case OP_CALL_INDIRECT:
        case OP_RETURN:                 chars += snprintf(buf + chars, nonneg(size - chars), " %lld",  (long long int)instr->operand.intVal); break;
        case OP_CALL:
        {
            const char *fnName = debugPerInstr[instr->operand.intVal].fnName;
            chars += snprintf(buf + chars, nonneg(size - chars), " %s (%lld)", fnName, (long long int)instr->operand.intVal);
            break;
        }
        case OP_GET_ARRAY_PTR:          chars += snprintf(buf + chars, nonneg(size - chars), " %d %d", (int)instr->operand.int32Val[0], (int)instr->operand.int32Val[1]); break;
        case OP_CALL_EXTERN:            chars += snprintf(buf + chars, nonneg(size - chars), " %p",    instr->operand.ptrVal); break;
        case OP_CALL_BUILTIN:           chars += snprintf(buf + chars, nonneg(size - chars), " %s",    builtinSpelling[instr->operand.builtinVal]); break;
        case OP_CHANGE_REF_CNT:
        case OP_CHANGE_REF_CNT_ASSIGN:
        case OP_GET_MAP_PTR:
        case OP_ASSERT_TYPE:
        {
            char typeBuf[DEFAULT_STR_LEN + 1];
            chars += snprintf(buf + chars, nonneg(size - chars), " %s", typeSpelling((Type *)instr->operand.ptrVal, typeBuf));
            break;
        }
        default: break;
    }

    if (instr->inlineOpcode == OP_DEREF)
        chars += snprintf(buf + chars, nonneg(size - chars), "; DEREF");

    else if (instr->inlineOpcode == OP_POP)
        chars += snprintf(buf + chars, nonneg(size - chars), "; POP");

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

    const MapNode *node = doGetMapNode(map, key, false, NULL, vm->error, NULL);
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

    doBasicChangeRefCnt(vm->fiber, &vm->pages, array, type, TOK_MINUSMINUS);
    doAllocDynArray(&vm->pages, array, type, len, vm->error);
}


const char *vmBuiltinSpelling(BuiltinFunc builtin)
{
    return builtinSpelling[builtin];
}

