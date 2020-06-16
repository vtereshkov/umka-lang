#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "umka_vm.h"

//#define DEBUG_REF_CNT


static char *opcodeSpelling [] =
{
    "NOP",
    "PUSH",
    "PUSH_LOCAL_PTR",
    "PUSH_REG",
    "PUSH_STRUCT",
    "POP",
    "POP_REG",
    "DUP",
    "SWAP",
    "DEREF",
    "ASSIGN",
    "SWAP_ASSIGN",
    "ASSIGN_OFS",
    "SWAP_ASSIGN_OFS",
    "CHANGE_REF_CNT",
    "CHANGE_REF_CNT_ASSIGN",
    "UNARY",
    "BINARY",
    "GET_ARRAY_PTR",
    "GET_DYNARRAY_PTR",
    "GET_FIELD_PTR",
    "ASSERT_TYPE",
    "GOTO",
    "GOTO_IF",
    "CALL",
    "CALL_EXTERN",
    "CALL_BUILTIN",
    "RETURN",
    "ENTER_FRAME",
    "LEAVE_FRAME",
    "HALT"
};


static char *builtinSpelling [] =
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
    "fabs",
    "sqrt",
    "sin",
    "cos",
    "atan",
    "exp",
    "log",
    "new",
    "make",
    "makefrom",
    "append",
    "delete",
    "len",
    "sizeof",
    "fiberspawn",
    "fibercall",
    "fiberalive",
    "error"
};


// Memory management

static void pageInit(HeapPages *pages)
{
    pages->first = pages->last = NULL;
}


static void pageFree(HeapPages *pages)
{
    HeapPage *page = pages->first;
    while (page)
    {
        HeapPage *next = page->next;
        if (page->ptr)
        {
            printf("Memory leak at %p (%d refs)\n", page->ptr, page->refCnt);
            free(page->ptr);
        }
        free(page);
        page = next;
    }
}


static HeapPage *pageAdd(HeapPages *pages, int size)
{
    HeapPage *page = malloc(sizeof(HeapPage));

    page->ptr = malloc(size);
    memset(page->ptr, 0, size);
    page->size = size;
    page->occupied = 0;
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

#ifdef DEBUG_REF_CNT
    printf("Add page at %p\n", page->ptr);
#endif

    return pages->last;
}


static void pageRemove(HeapPages *pages, HeapPage *page)
{
#ifdef DEBUG_REF_CNT
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


static HeapPage *pageFind(HeapPages *pages, void *ptr)
{
    for (HeapPage *page = pages->first; page; page = page->next)
        if (ptr >= page->ptr && ptr < page->ptr + page->occupied)
        {
            HeapChunkHeader *chunk = ptr - sizeof(HeapChunkHeader);
            if (chunk->magic == VM_HEAP_CHUNK_MAGIC)
                return page;
            return NULL;
        }

    return NULL;
}


static void *chunkAlloc(HeapPages *pages, int size, ErrorFunc error)
{
    // Page layout: header, data, header, data...
    int chunkSize = sizeof(HeapChunkHeader) + size;
    int pageSize = chunkSize > VM_MIN_HEAP_PAGE ? chunkSize : VM_MIN_HEAP_PAGE;

    if (!pages->last || pages->last->occupied + chunkSize > pages->last->size)
        pageAdd(pages, pageSize);

    HeapChunkHeader *chunk = pages->last->ptr + pages->last->occupied;
    chunk->magic = VM_HEAP_CHUNK_MAGIC;
    chunk->refCnt = 1;
    chunk->size = size;

    pages->last->occupied += chunkSize;
    pages->last->refCnt++;

#ifdef DEBUG_REF_CNT
    printf("Add chunk at %p\n", (void *)chunk + sizeof(HeapChunkHeader));
#endif

    return (void *)chunk + sizeof(HeapChunkHeader);
}


static void chunkChangeRefCnt(HeapPages *pages, HeapPage *page, void *ptr, int delta)
{
    HeapChunkHeader *chunk = ptr - sizeof(HeapChunkHeader);

    // TODO: double-check the suspicious condition
    if (chunk->refCnt > 0)
    {
        chunk->refCnt += delta;
        page->refCnt += delta;

#ifdef DEBUG_REF_CNT
        printf("%p: delta: %d  chunk: %d  page: %d\n", ptr, delta, chunk->refCnt, page->refCnt);
#endif
    }

    if (page->refCnt == 0)
        pageRemove(pages, page);
}


// Static copies of some external functions that allow inlining

static int alignRuntime(int size, int alignment)
{
    return ((size + (alignment - 1)) / alignment) * alignment;
}


static bool typeGarbageCollectedRuntime(Type *type)
{
    return type->kind == TYPE_PTR    || type->kind == TYPE_ARRAY     || type->kind == TYPE_DYNARRAY ||
           type->kind == TYPE_STRUCT || type->kind == TYPE_INTERFACE || type->kind == TYPE_FIBER;
}


// Virtual machine

void vmInit(VM *vm, int stackSize, ErrorFunc error)
{
    vm->fiber = malloc(sizeof(Fiber));
    vm->fiber->stack = malloc(stackSize * sizeof(Slot));
    vm->fiber->stackSize = stackSize;
    vm->fiber->alive = true;
    pageInit(&vm->pages);
    vm->error = error;
}


void vmFree(VM *vm)
{
    pageFree(&vm->pages);
    free(vm->fiber->stack);
    free(vm->fiber);
}


void vmReset(VM *vm, Instruction *code)
{
    vm->fiber->code = code;
    vm->fiber->ip = 0;
    vm->fiber->top = vm->fiber->base = vm->fiber->stack + vm->fiber->stackSize - 1;
}


static void doBasicDeref(Slot *slot, TypeKind typeKind, ErrorFunc error)
{
    if (!slot->ptrVal)
        error("Pointer is null");

    switch (typeKind)
    {
        case TYPE_INT8:         slot->intVal  = *(int8_t   *)slot->ptrVal; break;
        case TYPE_INT16:        slot->intVal  = *(int16_t  *)slot->ptrVal; break;
        case TYPE_INT32:        slot->intVal  = *(int32_t  *)slot->ptrVal; break;
        case TYPE_INT:          slot->intVal  = *(int64_t  *)slot->ptrVal; break;
        case TYPE_UINT8:        slot->intVal  = *(uint8_t  *)slot->ptrVal; break;
        case TYPE_UINT16:       slot->intVal  = *(uint16_t *)slot->ptrVal; break;
        case TYPE_UINT32:       slot->intVal  = *(uint32_t *)slot->ptrVal; break;
        case TYPE_BOOL:         slot->intVal  = *(bool     *)slot->ptrVal; break;
        case TYPE_CHAR:         slot->intVal  = *(char     *)slot->ptrVal; break;
        case TYPE_REAL32:       slot->realVal = *(float    *)slot->ptrVal; break;
        case TYPE_REAL:         slot->realVal = *(double   *)slot->ptrVal; break;
        case TYPE_PTR:          slot->ptrVal  = *(void *   *)slot->ptrVal; break;
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_STR:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_FIBER:        slot->intVal  =  (int64_t   )slot->ptrVal; break;  // Always represented by pointer, not dereferenced
        case TYPE_FN:           slot->ptrVal  = *(void *   *)slot->ptrVal; break;

        default:                error("Illegal type"); return;
    }
}


static void doBasicAssign(void *lhs, Slot rhs, TypeKind typeKind, int structSize, ErrorFunc error)
{
    if (!lhs)
        error("Pointer is null");

    switch (typeKind)
    {
        case TYPE_INT8:         *(int8_t   *)lhs = rhs.intVal; break;
        case TYPE_INT16:        *(int16_t  *)lhs = rhs.intVal; break;
        case TYPE_INT32:        *(int32_t  *)lhs = rhs.intVal; break;
        case TYPE_INT:          *(int64_t  *)lhs = rhs.intVal; break;
        case TYPE_UINT8:        *(uint8_t  *)lhs = rhs.intVal; break;
        case TYPE_UINT16:       *(uint16_t *)lhs = rhs.intVal; break;
        case TYPE_UINT32:       *(uint32_t *)lhs = rhs.intVal; break;
        case TYPE_BOOL:         *(bool     *)lhs = rhs.intVal; break;
        case TYPE_CHAR:         *(char     *)lhs = rhs.intVal; break;
        case TYPE_REAL32:       *(float    *)lhs = rhs.realVal; break;
        case TYPE_REAL:         *(double   *)lhs = rhs.realVal; break;
        case TYPE_PTR:          *(void *   *)lhs = rhs.ptrVal; break;
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_FIBER:        memcpy(lhs, rhs.ptrVal, structSize); break;
        case TYPE_STR:          strcpy(lhs, rhs.ptrVal); break;
        case TYPE_FN:           *(void *   *)lhs = rhs.ptrVal; break;

        default:                error("Illegal type"); return;
    }
}


static void doBasicChangeRefCnt(Fiber *fiber, HeapPages *pages, void *ptr, Type *type, TokenKind tokKind, ErrorFunc error)
{
    // Update ref counts for pointers (including static/dynamic array items and structure/interface fields) if allocated dynamically
    // Among garbage collected types, all types except the pointer type are represented by pointers by default
    // RTTI is required for lists, trees, etc., since the propagation depth for the root ref count is unknown at compile time

    switch (type->kind)
    {
        case TYPE_PTR:
        {
            HeapPage *page = pageFind(pages, ptr);
            if (page && !type->weak)
            {
                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, ptr, 1);
                else
                {
                    // Traverse children only before removing the last remaining ref
                    HeapChunkHeader *chunk = ptr - sizeof(HeapChunkHeader);
                    if (chunk->refCnt == 1 && typeGarbageCollectedRuntime(type->base))
                    {
                        void *data = ptr;
                        if (type->base->kind == TYPE_PTR)
                            data = *(void **)data;

                        doBasicChangeRefCnt(fiber, pages, data, type->base, tokKind, error);
                    }
                    chunkChangeRefCnt(pages, page, ptr, -1);
                }
            }
            break;
        }

        case TYPE_ARRAY:
        {
            if (typeGarbageCollectedRuntime(type->base))
            {
                void *itemPtr = ptr;
                int itemSize = typeSizeRuntime(type->base);

                for (int i = 0; i < type->numItems; i++)
                {
                    void *item = itemPtr;
                    if (type->base->kind == TYPE_PTR)
                        item = *(void **)item;

                    doBasicChangeRefCnt(fiber, pages, item, type->base, tokKind, error);
                    itemPtr += itemSize;
                }
            }
            break;
        }

        case TYPE_DYNARRAY:
        {
            DynArray *array = (DynArray *)ptr;
            HeapPage *page = pageFind(pages, array->data);
            if (page)
            {
                if (tokKind == TOK_PLUSPLUS)
                    chunkChangeRefCnt(pages, page, array->data, 1);
                else
                {
                    // Traverse children only before removing the last remaining ref
                    HeapChunkHeader *chunk = array->data - sizeof(HeapChunkHeader);
                    if (chunk->refCnt == 1 && typeGarbageCollectedRuntime(type->base))
                    {
                        void *itemPtr = array->data;

                        for (int i = 0; i < array->len; i++)
                        {
                            void *item = itemPtr;
                            if (type->base->kind == TYPE_PTR)
                                item = *(void **)item;

                            doBasicChangeRefCnt(fiber, pages, item, type->base, tokKind, error);
                            itemPtr += array->itemSize;
                        }
                    }
                    chunkChangeRefCnt(pages, page, array->data, -1);
                }
            }
            break;
        }

        case TYPE_STRUCT:
        {
            for (int i = 0; i < type->numItems; i++)
            {
                if (typeGarbageCollectedRuntime(type->field[i]->type))
                {
                    void *field = ptr + type->field[i]->offset;
                    if (type->field[i]->type->kind == TYPE_PTR)
                        field = *(void **)field;

                    doBasicChangeRefCnt(fiber, pages, field, type->field[i]->type, tokKind, error);
                }
            }
            break;
        }

        case TYPE_INTERFACE:
        {
            // Interface layout: __self, __selftype, methods
            void *__self = *(void **)ptr;
            Type *__selftype = *(Type **)(ptr + type->field[1]->offset);

            if (__self)
                doBasicChangeRefCnt(fiber, pages, __self, __selftype, tokKind, error);
            break;
        }

        case TYPE_FIBER:
        {
            HeapPage *page = pageFind(pages, ptr);
            if (page)
            {
                // Don't use ref counting for the fiber stack, otherwise every local variable will also be ref-counted
                HeapChunkHeader *chunk = ptr - sizeof(HeapChunkHeader);
                if (chunk->refCnt == 1 && tokKind == TOK_MINUSMINUS)
                    free(((Fiber *)ptr)->stack);
            }
            break;
        }

        default: break;
    }
}


static void doConvertFormatStringToC(char *formatC, char **format, ErrorFunc error) // "{d}" -> "%d"
{
    bool leftBraceFound = false, rightBraceFound = false, slashFound = false;
    while (**format)
    {
        if (**format == '/' && !slashFound)
        {
            slashFound = true;
            (*format)++;
        }
        else if (**format == '{' && !slashFound)
        {
            *formatC = '%';
            leftBraceFound = true;
            slashFound = false;
            (*format)++;
            formatC++;
        }
        else if (**format == '}' && !slashFound)
        {
            if (!leftBraceFound)
            {
                error("Illegal format string");
                return;
            }
            rightBraceFound = true;
            slashFound = false;
            (*format)++;
            break;
        }
        else
        {
            *formatC++ = *(*format)++;
            slashFound = false;
        }
    }
    *formatC = 0;

    if (leftBraceFound && !rightBraceFound)
    {
        error("Illegal format string");
        return;
    }
}


static void doBuiltinPrintf(Fiber *fiber, bool console, bool string, ErrorFunc error)
{
    void *stream = console ? stdout : fiber->reg[VM_IO_STREAM_REG].ptrVal;
    char *format = fiber->reg[VM_IO_FORMAT_REG].ptrVal;

    char *formatC = malloc(strlen(format) + 1);
    doConvertFormatStringToC(formatC, &format, error);

    // Proxy to C fprintf()/sprintf()
    int len = 0;
    if (fiber->code[fiber->ip].typeKind == TYPE_VOID)
    {
        if (string) len = sprintf((char *)stream, formatC);
        else        len = fprintf((FILE *)stream, formatC);
    }
    else if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
    {
        if (string) len = sprintf((char *)stream, formatC, fiber->top->realVal);
        else        len = fprintf((FILE *)stream, formatC, fiber->top->realVal);
    }
    else
    {
        if (string) len = sprintf((char *)stream, formatC, fiber->top->intVal);
        else        len = fprintf((FILE *)stream, formatC, fiber->top->intVal);
    }

    fiber->reg[VM_IO_FORMAT_REG].ptrVal = format;
    fiber->reg[VM_IO_COUNT_REG].intVal += len;
    if (string)
        fiber->reg[VM_IO_STREAM_REG].ptrVal += len;

    free(formatC);
}


static void doBuiltinScanf(Fiber *fiber, bool console, bool string, ErrorFunc error)
{
    void *stream = console ? stdin : fiber->reg[VM_IO_STREAM_REG].ptrVal;
    char *format = fiber->reg[VM_IO_FORMAT_REG].ptrVal;

    char *formatC = malloc(strlen(format) + 2 + 1);     // + 2 for "%n"
    doConvertFormatStringToC(formatC, &format, error);
    strcat(formatC, "%n");

    // Proxy to C fscanf()/sscanf()
    int len = 0, cnt = 0;
    if (fiber->code[fiber->ip].typeKind == TYPE_VOID)
    {
        if (string) cnt = sscanf((char *)stream, formatC, &len);
        else        cnt = fscanf((FILE *)stream, formatC, &len);
    }
    else
    {
        if (string) cnt = sscanf((char *)stream, formatC, fiber->top->ptrVal, &len);
        else        cnt = fscanf((FILE *)stream, formatC, fiber->top->ptrVal, &len);
    }

    fiber->reg[VM_IO_FORMAT_REG].ptrVal = format;
    fiber->reg[VM_IO_COUNT_REG].intVal += cnt;
    if (string)
        fiber->reg[VM_IO_STREAM_REG].ptrVal += len;

    free(formatC);
}


// fn make([...] type (actually itemSize: int), len: int): [] type
static void doBuiltinMake(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    DynArray *result = (fiber->top++)->ptrVal;
    result->len      = (fiber->top++)->intVal;
    result->itemSize = (fiber->top++)->intVal;
    result->data     = chunkAlloc(pages, result->len * result->itemSize, error);

    (--fiber->top)->ptrVal = result;
}


// fn makefrom(array: [...] type, itemSize: int, len: int): [] type
static void doBuiltinMakeFrom(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    doBuiltinMake(fiber, pages, error);

    DynArray *dest = (fiber->top++)->ptrVal;
    void *src      = (fiber->top++)->ptrVal;

    memcpy(dest->data, src, dest->len * dest->itemSize);
    (--fiber->top)->ptrVal = dest;
}


// fn append(array: [] type, item: ^type): [] type
static void doBuiltinAppend(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    DynArray *result = (fiber->top++)->ptrVal;
    void *item       = (fiber->top++)->ptrVal;
    DynArray *array  = (fiber->top++)->ptrVal;

    if (!array->data)
        error("Dynamic array is not initialized");

    result->len      = array->len + 1;
    result->itemSize = array->itemSize;
    result->data     = chunkAlloc(pages, result->len * result->itemSize, error);

    memcpy(result->data, array->data, array->len * array->itemSize);
    memcpy(result->data + (result->len - 1) * result->itemSize, item, result->itemSize);

    (--fiber->top)->ptrVal = result;
}


// fn delete(array: [] type, index: int): [] type
static void doBuiltinDelete(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    DynArray *result = (fiber->top++)->ptrVal;
    int index        = (fiber->top++)->intVal;
    DynArray *array  = (fiber->top++)->ptrVal;

    if (!array->data)
        error("Dynamic array is not initialized");

    result->len      = array->len - 1;
    result->itemSize = array->itemSize;
    result->data     = chunkAlloc(pages, result->len * result->itemSize, error);

    memcpy(result->data, array->data, index * array->itemSize);
    memcpy(result->data + index * result->itemSize, array->data + (index + 1) * result->itemSize, (result->len - index) * result->itemSize);

    (--fiber->top)->ptrVal = result;
}


static void doBuiltinLen(Fiber *fiber, ErrorFunc error)
{
    switch (fiber->code[fiber->ip].typeKind)
    {
        // Done at compile time for arrays
        case TYPE_DYNARRAY: fiber->top->intVal = ((DynArray *)(fiber->top->ptrVal))->len; break;
        case TYPE_STR:      fiber->top->intVal = strlen(fiber->top->ptrVal); break;
        default:            error("Illegal type"); return;
    }
}


// type FiberFunc = fn(parent: ^fiber, anyParam: ^type)
// fn fiberspawn(childFunc: FiberFunc, anyParam: ^type): ^fiber
static void doBuiltinFiberspawn(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    void *anyParam = (fiber->top++)->ptrVal;
    int childEntryOffset = (fiber->top++)->intVal;

    // Copy whole fiber context
    Fiber *child = chunkAlloc(pages, sizeof(Fiber), error);
    *child = *fiber;

    child->stack = malloc(fiber->stackSize * sizeof(Slot));
    *(child->stack) = *(fiber->stack);

    child->top  = child->stack + (fiber->top  - fiber->stack);
    child->base = child->stack + (fiber->base - fiber->stack);

    // Call child fiber function
    (--child->top)->ptrVal = fiber;                  // Push parent fiber pointer
    (--child->top)->ptrVal = anyParam;               // Push arbitrary pointer parameter
    (--child->top)->intVal = VM_FIBER_KILL_SIGNAL;   // Push fiber kill signal instead of return address
    child->ip = childEntryOffset;                    // Call

    // Return child fiber pointer to parent fiber as result
    (--fiber->top)->ptrVal = child;
}


// fn fibercall(child: ^fiber)
static void doBuiltinFibercall(Fiber *fiber, Fiber **newFiber, HeapPages *pages, ErrorFunc error)
{
    *newFiber = (fiber->top++)->ptrVal;
    if (!(*newFiber)->alive)
        error("Fiber is dead");
}


// fn fiberalive(child: ^fiber)
static void doBuiltinFiberalive(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    Fiber *child = fiber->top->ptrVal;
    fiber->top->intVal = child->alive;
}


static void doPush(Fiber *fiber, ErrorFunc error)
{
    (--fiber->top)->intVal = fiber->code[fiber->ip].operand.intVal;

    if (fiber->code[fiber->ip].inlineDeref)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doPushLocalPtr(Fiber *fiber, ErrorFunc error)
{
    // Local variable addresses are offsets (in bytes) from the stack frame base pointer
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->code[fiber->ip].operand.intVal;

    if (fiber->code[fiber->ip].inlineDeref)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doPushReg(Fiber *fiber)
{
    (--fiber->top)->intVal = fiber->reg[fiber->code[fiber->ip].operand.intVal].intVal;
    fiber->ip++;
}


static void doPushStruct(Fiber *fiber, ErrorFunc error)
{
    void *src = (fiber->top++)->ptrVal;
    int size  = fiber->code[fiber->ip].operand.intVal;
    int slots = alignRuntime(size, sizeof(Slot)) / sizeof(Slot);

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error("Stack overflow");

    fiber->top -= slots;
    memcpy(fiber->top, src, size);

    fiber->ip++;
}


static void doPop(Fiber *fiber)
{
    fiber->top++;
    fiber->ip++;
}


static void doPopReg(Fiber *fiber)
{
    fiber->reg[fiber->code[fiber->ip].operand.intVal].intVal = (fiber->top++)->intVal;
    fiber->ip++;
}


static void doDup(Fiber *fiber)
{
    Slot val = *fiber->top;
    *(--fiber->top) = val;
    fiber->ip++;
}


static void doSwap(Fiber *fiber)
{
    Slot val = *fiber->top;
    *fiber->top = *(fiber->top + 1);
    *(fiber->top + 1) = val;
    fiber->ip++;
}


static void doDeref(Fiber *fiber, ErrorFunc error)
{
    doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);
    fiber->ip++;
}


static void doAssign(Fiber *fiber, ErrorFunc error)
{
    Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;
    doBasicAssign(lhs, rhs, fiber->code[fiber->ip].typeKind, fiber->code[fiber->ip].operand.intVal, error);
    fiber->ip++;
}


static void doSwapAssign(Fiber *fiber, ErrorFunc error)
{
    void *lhs = (fiber->top++)->ptrVal;
    Slot rhs = *fiber->top++;
    doBasicAssign(lhs, rhs, fiber->code[fiber->ip].typeKind, fiber->code[fiber->ip].operand.intVal, error);
    fiber->ip++;
}


static void doAssignOfs(Fiber *fiber)
{
    Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;
    int offset = fiber->code[fiber->ip].operand.intVal;

    *(Slot *)(lhs + offset) = rhs;
    fiber->ip++;
}


static void doSwapAssignOfs(Fiber *fiber)
{
    void *lhs = (fiber->top++)->ptrVal;
    Slot rhs = *fiber->top++;
    int offset = fiber->code[fiber->ip].operand.intVal;

    *(Slot *)(lhs + offset) = rhs;
    fiber->ip++;
}


static void doChangeRefCnt(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    void *ptr         = fiber->top->ptrVal;
    TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    Type *type        = fiber->code[fiber->ip].operand.ptrVal;

    doBasicChangeRefCnt(fiber, pages, ptr, type, tokKind, error);

    if (fiber->code[fiber->ip].inlinePop)
        fiber->top++;

    fiber->ip++;
}


static void doChangeRefCntAssign(Fiber *fiber, HeapPages *pages, ErrorFunc error)
{
    Slot rhs   = *fiber->top++;
    void *lhs  = (fiber->top++)->ptrVal;
    Type *type = fiber->code[fiber->ip].operand.ptrVal;

    // Increase right-hand side ref count
    doBasicChangeRefCnt(fiber, pages, rhs.ptrVal, type, TOK_PLUSPLUS, error);

    // Decrease left-hand side ref count
    Slot lhsDeref = {.ptrVal = lhs};
    doBasicDeref(&lhsDeref, type->kind, error);
    doBasicChangeRefCnt(fiber, pages, lhsDeref.ptrVal, type, TOK_MINUSMINUS, error);

    doBasicAssign(lhs, rhs, type->kind, typeSizeRuntime(type), error);
    fiber->ip++;
}


static void doUnary(Fiber *fiber, ErrorFunc error)
{
    if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_MINUS: fiber->top->realVal = -fiber->top->realVal; break;
            default:        error("Illegal instruction"); return;
        }
    else
        switch (fiber->code[fiber->ip].tokKind)
        {
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
                    case TYPE_CHAR:   (*(char     *)ptr)++; break;
                    case TYPE_PTR:    (*(void *   *)ptr)++; break;
                    // Structured, boolean and real types are not incremented/decremented
                    default:          error("Illegal type"); return;
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
                    case TYPE_CHAR:   (*(char     *)ptr)--; break;
                    case TYPE_PTR:    (*(void *   *)ptr)--; break;
                    // Structured, boolean and real types are not incremented/decremented
                    default:          error("Illegal type"); return;
                }
            break;
            }

            default: error("Illegal instruction"); return;
        }
    fiber->ip++;
}


static void doBinary(Fiber *fiber, ErrorFunc error)
{
    Slot rhs = *fiber->top++;

    if (fiber->code[fiber->ip].typeKind == TYPE_STR)
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:
            {
                int bufOffset = fiber->code[fiber->ip].operand.intVal;
                void *buf = (int8_t *)fiber->base + bufOffset;
                strcpy(buf, fiber->top->ptrVal);
                strcat(buf, rhs.ptrVal);
                fiber->top->ptrVal = buf;
                break;
            }

            case TOK_EQEQ:      fiber->top->intVal = strcmp(fiber->top->ptrVal, rhs.ptrVal) == 0; break;
            case TOK_NOTEQ:     fiber->top->intVal = strcmp(fiber->top->ptrVal, rhs.ptrVal) != 0; break;
            case TOK_GREATER:   fiber->top->intVal = strcmp(fiber->top->ptrVal, rhs.ptrVal)  > 0; break;
            case TOK_LESS:      fiber->top->intVal = strcmp(fiber->top->ptrVal, rhs.ptrVal)  < 0; break;
            case TOK_GREATEREQ: fiber->top->intVal = strcmp(fiber->top->ptrVal, rhs.ptrVal) >= 0; break;
            case TOK_LESSEQ:    fiber->top->intVal = strcmp(fiber->top->ptrVal, rhs.ptrVal) <= 0; break;

            default:            error("Illegal instruction"); return;
        }
    else if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  fiber->top->realVal += rhs.realVal; break;
            case TOK_MINUS: fiber->top->realVal -= rhs.realVal; break;
            case TOK_MUL:   fiber->top->realVal *= rhs.realVal; break;
            case TOK_DIV:
            {
                if (rhs.realVal == 0)
                {
                    error("Division by zero");
                    return;
                }
                fiber->top->realVal /= rhs.realVal;
                break;
            }

            case TOK_EQEQ:      fiber->top->intVal = fiber->top->realVal == rhs.realVal; break;
            case TOK_NOTEQ:     fiber->top->intVal = fiber->top->realVal != rhs.realVal; break;
            case TOK_GREATER:   fiber->top->intVal = fiber->top->realVal >  rhs.realVal; break;
            case TOK_LESS:      fiber->top->intVal = fiber->top->realVal <  rhs.realVal; break;
            case TOK_GREATEREQ: fiber->top->intVal = fiber->top->realVal >= rhs.realVal; break;
            case TOK_LESSEQ:    fiber->top->intVal = fiber->top->realVal <= rhs.realVal; break;

            default:            error("Illegal instruction"); return;
        }
    else
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  fiber->top->intVal += rhs.intVal; break;
            case TOK_MINUS: fiber->top->intVal -= rhs.intVal; break;
            case TOK_MUL:   fiber->top->intVal *= rhs.intVal; break;
            case TOK_DIV:
            {
                if (rhs.intVal == 0)
                {
                    error("Division by zero");
                    return;
                }
                fiber->top->intVal /= rhs.intVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.intVal == 0)
                {
                    error("Division by zero");
                    return;
                }
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

            default:            error("Illegal instruction"); return;
        }
    fiber->ip++;
}


static void doGetArrayPtr(Fiber *fiber, ErrorFunc error)
{
    int itemSize = fiber->code[fiber->ip].operand.intVal;
    int len      = (fiber->top++)->intVal;
    int index    = (fiber->top++)->intVal;

    if (index < 0 || index > len - 1)
        error("Index is out of range");

    fiber->top->ptrVal += itemSize * index;

    if (fiber->code[fiber->ip].inlineDeref)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doGetDynArrayPtr(Fiber *fiber, ErrorFunc error)
{
    int index       = (fiber->top++)->intVal;

    DynArray *array = (fiber->top++)->ptrVal;
    int itemSize    = array->itemSize;
    int len         = array->len;

    if (!array->data)
        error("Dynamic array is not initialized");

    if (index < 0 || index > len - 1)
        error("Index is out of range");

    (--fiber->top)->ptrVal = array->data + itemSize * index;

    if (fiber->code[fiber->ip].inlineDeref)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doGetFieldPtr(Fiber *fiber, ErrorFunc error)
{
    int fieldOffset = fiber->code[fiber->ip].operand.intVal;
    fiber->top->ptrVal += fieldOffset;

    if (fiber->code[fiber->ip].inlineDeref)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doAssertType(Fiber *fiber)
{
    void *interface = (fiber->top++)->ptrVal;
    Type *type = fiber->code[fiber->ip].operand.ptrVal;

    // Interface layout: __self, __selftype, methods
    void *__self = *(void **)interface;
    Type *__selftype = *(Type **)(interface + sizeof(__self));

    (--fiber->top)->ptrVal = (__selftype && typeEquivalent(type, __selftype)) ? __self : NULL;
    fiber->ip++;
}


static void doGoto(Fiber *fiber)
{
    fiber->ip = fiber->code[fiber->ip].operand.intVal;
}


static void doGotoIf(Fiber *fiber)
{
    if ((fiber->top++)->intVal)
        fiber->ip = fiber->code[fiber->ip].operand.intVal;
    else
        fiber->ip++;
}


static void doCall(Fiber *fiber, ErrorFunc error)
{
    // All calls are indirect, entry point address is below the parameters
    int paramSlots = fiber->code[fiber->ip].operand.intVal;
    int entryOffset = (fiber->top + paramSlots)->intVal;

    if (entryOffset == 0)
        error("Called function is not defined");

    // Push return address and go to the entry point
    (--fiber->top)->intVal = fiber->ip + 1;
    fiber->ip = entryOffset;
}


static void doCallExtern(Fiber *fiber)
{
    ExternFunc fn = fiber->code[fiber->ip].operand.ptrVal;
    fn(fiber->top + 1, &fiber->reg[VM_RESULT_REG_0]);       // + 1 for return address
    fiber->ip++;
}


static void doCallBuiltin(Fiber *fiber, Fiber **newFiber, HeapPages *pages, ErrorFunc error)
{
    BuiltinFunc builtin = fiber->code[fiber->ip].operand.builtinVal;
    switch (builtin)
    {
        // I/O
        case BUILTIN_PRINTF:        doBuiltinPrintf(fiber, true, false, error); break;
        case BUILTIN_FPRINTF:       doBuiltinPrintf(fiber, false, false, error); break;
        case BUILTIN_SPRINTF:       doBuiltinPrintf(fiber, false, true, error); break;
        case BUILTIN_SCANF:         doBuiltinScanf(fiber, true, false, error); break;
        case BUILTIN_FSCANF:        doBuiltinScanf(fiber, false, false, error); break;
        case BUILTIN_SSCANF:        doBuiltinScanf(fiber, false, true, error); break;

        // Math
        case BUILTIN_REAL:          fiber->top->realVal = fiber->top->intVal; break;
        case BUILTIN_REAL_LHS:      (fiber->top + 1)->realVal = (fiber->top + 1)->intVal; break;
        case BUILTIN_ROUND:         fiber->top->intVal = (int64_t)round(fiber->top->realVal); break;
        case BUILTIN_TRUNC:         fiber->top->intVal = (int64_t)trunc(fiber->top->realVal); break;
        case BUILTIN_FABS:          fiber->top->realVal = fabs(fiber->top->realVal); break;
        case BUILTIN_SQRT:
        {
            if (fiber->top->realVal < 0)
            {
                error("sqrt() domain error");
                return;
            }
            fiber->top->realVal = sqrt(fiber->top->realVal);
            break;
        }
        case BUILTIN_SIN:           fiber->top->realVal = sin (fiber->top->realVal); break;
        case BUILTIN_COS:           fiber->top->realVal = cos (fiber->top->realVal); break;
        case BUILTIN_ATAN:          fiber->top->realVal = atan(fiber->top->realVal); break;
        case BUILTIN_EXP:           fiber->top->realVal = exp (fiber->top->realVal); break;
        case BUILTIN_LOG:
        {
            if (fiber->top->realVal <= 0)
            {
                error("log() domain error");
                return;
            }
            fiber->top->realVal = sqrt(fiber->top->realVal);
            break;
        }

        // Memory
        case BUILTIN_NEW:           fiber->top->ptrVal = chunkAlloc(pages, fiber->top->intVal, error); break;
        case BUILTIN_MAKE:          doBuiltinMake(fiber, pages, error); break;
        case BUILTIN_MAKEFROM:      doBuiltinMakeFrom(fiber, pages, error); break;
        case BUILTIN_APPEND:        doBuiltinAppend(fiber, pages, error); break;
        case BUILTIN_DELETE:        doBuiltinDelete(fiber, pages, error); break;
        case BUILTIN_LEN:           doBuiltinLen(fiber, error); break;
        case BUILTIN_SIZEOF:        error("Illegal instruction"); return;   // Done at compile time

        // Fibers
        case BUILTIN_FIBERSPAWN:    doBuiltinFiberspawn(fiber, pages, error); break;
        case BUILTIN_FIBERCALL:     doBuiltinFibercall(fiber, newFiber, pages, error); break;
        case BUILTIN_FIBERALIVE:    doBuiltinFiberalive(fiber, pages, error); break;

        // Misc
        case BUILTIN_ERROR:         error(fiber->top->ptrVal); return;
    }
    fiber->ip++;
}


static void doReturn(Fiber *fiber, Fiber **newFiber)
{
    // Pop return address
    int returnOffset = (fiber->top++)->intVal;

    if (returnOffset == VM_FIBER_KILL_SIGNAL)
    {
        // For fiber function, kill the fiber, extract the parent fiber pointer and switch to it
        fiber->alive = false;
        *newFiber = (fiber->top + 1)->ptrVal;
    }
    else
    {
        // For conventional function, remove parameters and entry point address from stack and go back
        fiber->top += fiber->code[fiber->ip].operand.intVal + 1;
        fiber->ip = returnOffset;
    }
}


static void doEnterFrame(Fiber *fiber, ErrorFunc error)
{
    // Push old stack frame base pointer, move new one to stack top, shift stack top by local variables' size
    int size = fiber->code[fiber->ip].operand.intVal;
    int slots = alignRuntime(size, sizeof(Slot)) / sizeof(Slot);

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error("Stack overflow");

    (--fiber->top)->ptrVal = fiber->base;
    fiber->base = fiber->top;
    fiber->top -= slots;

    // Zero all local variables
    if (slots == 1)
        fiber->top->intVal = 0;
    else if (slots > 0)
        memset(fiber->top, 0, slots * sizeof(Slot));

    // Push I/O registers
    *(--fiber->top) = fiber->reg[VM_IO_STREAM_REG];
    *(--fiber->top) = fiber->reg[VM_IO_FORMAT_REG];
    *(--fiber->top) = fiber->reg[VM_IO_COUNT_REG];

    fiber->ip++;
}


static void doLeaveFrame(Fiber *fiber)
{
    // Pop I/O registers
    fiber->reg[VM_IO_COUNT_REG]  = *(fiber->top++);
    fiber->reg[VM_IO_FORMAT_REG] = *(fiber->top++);
    fiber->reg[VM_IO_STREAM_REG] = *(fiber->top++);

    // Restore stack top, pop old stack frame base pointer
    fiber->top = fiber->base;
    fiber->base = (fiber->top++)->ptrVal;

    fiber->ip++;
}


static void vmLoop(VM *vm)
{
    Fiber *fiber = vm->fiber;
    HeapPages *pages = &vm->pages;
    ErrorFunc error = vm->error;

    while (1)
    {
        if (fiber->top - fiber->stack < VM_MIN_FREE_STACK)
            error("Stack overflow");

        switch (fiber->code[fiber->ip].opcode)
        {
            case OP_PUSH:                   doPush(fiber, error);                          break;
            case OP_PUSH_LOCAL_PTR:         doPushLocalPtr(fiber, error);                  break;
            case OP_PUSH_REG:               doPushReg(fiber);                              break;
            case OP_PUSH_STRUCT:            doPushStruct(fiber, error);                    break;
            case OP_POP:                    doPop(fiber);                                  break;
            case OP_POP_REG:                doPopReg(fiber);                               break;
            case OP_DUP:                    doDup(fiber);                                  break;
            case OP_SWAP:                   doSwap(fiber);                                 break;
            case OP_DEREF:                  doDeref(fiber, error);                         break;
            case OP_ASSIGN:                 doAssign(fiber, error);                        break;
            case OP_SWAP_ASSIGN:            doSwapAssign(fiber, error);                    break;
            case OP_ASSIGN_OFS:             doAssignOfs(fiber);                            break;
            case OP_SWAP_ASSIGN_OFS:        doSwapAssignOfs(fiber);                        break;
            case OP_CHANGE_REF_CNT:         doChangeRefCnt(fiber, pages, error);           break;
            case OP_CHANGE_REF_CNT_ASSIGN:  doChangeRefCntAssign(fiber, pages, error);     break;
            case OP_UNARY:                  doUnary(fiber, error);                         break;
            case OP_BINARY:                 doBinary(fiber, error);                        break;
            case OP_GET_ARRAY_PTR:          doGetArrayPtr(fiber, error);                   break;
            case OP_GET_DYNARRAY_PTR:       doGetDynArrayPtr(fiber, error);                break;
            case OP_GET_FIELD_PTR:          doGetFieldPtr(fiber, error);                   break;
            case OP_ASSERT_TYPE:            doAssertType(fiber);                           break;
            case OP_GOTO:                   doGoto(fiber);                                 break;
            case OP_GOTO_IF:                doGotoIf(fiber);                               break;
            case OP_CALL:                   doCall(fiber, error);                          break;
            case OP_CALL_EXTERN:            doCallExtern(fiber);                           break;
            case OP_CALL_BUILTIN:
            {
                Fiber *newFiber = NULL;
                doCallBuiltin(fiber, &newFiber, pages, error);

                if (newFiber)
                    fiber = vm->fiber = newFiber;

                break;
            }
            case OP_RETURN:
            {
                if (fiber->top->intVal == 0)
                    return;

                Fiber *newFiber = NULL;
                doReturn(fiber, &newFiber);

                if (newFiber)
                    fiber = vm->fiber = newFiber;

                if (!fiber->alive)
                    return;

                break;
            }
            case OP_ENTER_FRAME:            doEnterFrame(fiber, error);                    break;
            case OP_LEAVE_FRAME:            doLeaveFrame(fiber);                           break;
            case OP_HALT:                   return;

            default: error("Illegal instruction"); return;
        } // switch
    }
}


void vmRun(VM *vm, int entryOffset, int numParamSlots, Slot *params, Slot *result)
{
    if (entryOffset < 0)
        vm->error("Called function is not defined");

    // Individual function call
    if (entryOffset > 0)
    {
        // Push parameters
        vm->fiber->top -= numParamSlots;
        for (int i = 0; i < numParamSlots; i++)
            vm->fiber->top[i] = params[i];

        // Push null return address and go to the entry point
        (--vm->fiber->top)->intVal = 0;
        vm->fiber->ip = entryOffset;
    }

    // Main loop
    vmLoop(vm);

    // Save result
    if (entryOffset > 0 && result)
        *result = vm->fiber->reg[VM_RESULT_REG_0];
}


int vmAsm(int ip, Instruction *instr, char *buf)
{
    int chars = sprintf(buf, "%09d %6d %22s", ip, instr->debug.line, opcodeSpelling[instr->opcode]);

    if (instr->tokKind != TOK_NONE)
        chars += sprintf(buf + chars, " %s", lexSpelling(instr->tokKind));

    if (instr->typeKind != TYPE_NONE)
        chars += sprintf(buf + chars, " %s", typeKindSpelling(instr->typeKind));

    switch (instr->opcode)
    {
        case OP_PUSH:
        {
            if (instr->typeKind == TYPE_REAL)
                chars += sprintf(buf + chars, " %.8lf", instr->operand.realVal);
            else if (instr->typeKind == TYPE_PTR)
                chars += sprintf(buf + chars, " %p", instr->operand.ptrVal);
            else
                chars += sprintf(buf + chars, " %lld", (long long int)instr->operand.intVal);
            break;
        }
        case OP_PUSH_LOCAL_PTR:
        case OP_PUSH_REG:
        case OP_PUSH_STRUCT:
        case OP_POP_REG:
        case OP_ASSIGN:
        case OP_SWAP_ASSIGN:
        case OP_ASSIGN_OFS:
        case OP_SWAP_ASSIGN_OFS:
        case OP_BINARY:
        case OP_GET_ARRAY_PTR:
        case OP_GET_FIELD_PTR:
        case OP_GOTO:
        case OP_GOTO_IF:
        case OP_CALL:
        case OP_RETURN:
        case OP_ENTER_FRAME:            chars += sprintf(buf + chars, " %lld", (long long int)instr->operand.intVal); break;
        case OP_CALL_EXTERN:            chars += sprintf(buf + chars, " %p",   instr->operand.ptrVal); break;
        case OP_CALL_BUILTIN:           chars += sprintf(buf + chars, " %s",   builtinSpelling[instr->operand.builtinVal]); break;
        case OP_CHANGE_REF_CNT:
        case OP_CHANGE_REF_CNT_ASSIGN:
        case OP_ASSERT_TYPE:
        {
            char typeBuf[DEFAULT_STR_LEN + 1];
            chars += sprintf(buf + chars, " %s", typeSpelling(instr->operand.ptrVal, typeBuf));
            break;
        }
        default: break;
    }

    if (instr->inlineDeref)
        chars += sprintf(buf + chars, "; DEREF");

    if (instr->inlinePop)
        chars += sprintf(buf + chars, "; POP");

    return chars;
}
