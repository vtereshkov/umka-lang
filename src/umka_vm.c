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
    "UNARY",
    "BINARY",
    "GET_ARRAY_PTR",
    "GET_DYNARRAY_PTR",
    "GET_FIELD_PTR",
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
    "len",
    "sizeof",
    "fiberspawn",
    "fiberfree",
    "fibercall",
    "fiberalive"
};


// Heap chunks

static void chunkInit(HeapChunks *chunks)
{
    chunks->first = chunks->last = NULL;
}


static void chunkFree(HeapChunks *chunks)
{
    HeapChunk *chunk = chunks->first;
    while (chunk)
    {
        HeapChunk *next = chunk->next;
        if (chunk->ptr)
        {
            printf("Memory leak at %p (%d bytes, %d refs)\n", chunk->ptr, chunk->size, chunk->refCnt);
            free(chunk->ptr);
        }
        free(chunk);
        chunk = next;
    }
}


static HeapChunk *chunkAdd(HeapChunks *chunks, int size)
{
    HeapChunk *chunk = malloc(sizeof(HeapChunk));

#ifdef DEBUG_REF_CNT
    printf("+");
#endif
    chunk->ptr = malloc(size);
    memset(chunk->ptr, 0, size);
    chunk->size = size;
    chunk->refCnt = 1;
    chunk->extraRefCnt = 0;
    chunk->prev = chunks->last;
    chunk->next = NULL;

    // Add to list
    if (!chunks->first)
        chunks->first = chunks->last = chunk;
    else
    {
        chunks->last->next = chunk;
        chunks->last = chunk;
    }
    return chunks->last;
}


static void chunkRemove(HeapChunks *chunks, HeapChunk *chunk)
{
    if (chunk == chunks->first)
        chunks->first = chunk->next;

    if (chunk == chunks->last)
        chunks->last = chunk->prev;

    if (chunk->prev)
        chunk->prev->next = chunk->next;

    if (chunk->next)
        chunk->next->prev = chunk->prev;

    free(chunk->ptr);
    free(chunk);
}


static HeapChunk *chunkFind(HeapChunks *chunks, void *ptr)
{
    for (HeapChunk *chunk = chunks->first; chunk; chunk = chunk->next)
        if (ptr >= chunk->ptr && ptr < chunk->ptr + chunk->size)
            return chunk;
    return NULL;
}


static bool chunkTryIncCnt(HeapChunks *chunks, void *ptr)
{
    HeapChunk *chunk = chunkFind(chunks, ptr);
    if (chunk)
    {
#ifdef DEBUG_REF_CNT
        printf("+");
#endif // DEBUG_REF_CNT

        chunk->refCnt++;
        return true;
    }
    return false;
}


static bool chunkTryDecCnt(HeapChunks *chunks, void *ptr)
{
    HeapChunk *chunk = chunkFind(chunks, ptr);
    if (chunk)
    {
#ifdef DEBUG_REF_CNT
        printf("-");
#endif // DEBUG_REF_CNT

        if (--chunk->refCnt == 0)
            chunkRemove(chunks, chunk);
        return true;
    }
    return false;
}


// Static copies of some external functions that allow inlining

static int alignRuntime(int size, int alignment)
{
    return ((size + (alignment - 1)) / alignment) * alignment;
}


static bool typeGarbageCollectedRuntime(Type *type)
{
    return type->kind == TYPE_PTR    || type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY ||
           type->kind == TYPE_STRUCT || type->kind == TYPE_INTERFACE;
}


// Virtual machine

void vmInit(VM *vm, Instruction *code, int stackSize, ErrorFunc error)
{
    vm->fiber = malloc(sizeof(Fiber));

    vm->fiber->code = code;
    vm->fiber->ip = 0;

    vm->fiber->stack = malloc(stackSize * sizeof(Slot));
    vm->fiber->top = vm->fiber->base = vm->fiber->stack + stackSize - 1;
    vm->fiber->stackSize = stackSize;

    vm->fiber->alive = true;

    chunkInit(&vm->chunks);
    vm->error = error;
}


void vmFree(VM *vm)
{
    chunkFree(&vm->chunks);
    free(vm->fiber->stack);
    free(vm->fiber);
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
        case TYPE_INTERFACE:    slot->intVal  =  (int64_t   )slot->ptrVal; break;  // Always represented by pointer, not dereferenced
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
        case TYPE_INTERFACE:    memcpy(lhs, rhs.ptrVal, structSize); break;
        case TYPE_STR:          strcpy(lhs, rhs.ptrVal); break;
        case TYPE_FN:           *(void *   *)lhs = rhs.ptrVal; break;

        default:                error("Illegal type"); return;
    }
}


static void doBasicChangeRefCnt(Fiber *fiber, HeapChunks *chunks, void *ptr, Type *type, int extraRefCnt, bool root, TokenKind tokKind, ErrorFunc error)
{
    // Update ref counts for pointers (including static/dynamic array items and structure/interface fields) if allocated dynamically
    // Ref count is the total number of ways through which the node is accessible, not just a number of immediate refs
    // Among garbage collected types, all types except the pointer type are represented by pointers by default

    // RTTI is required for lists, trees, etc., since the propagation depth for the root ref count is unknown at compile time
    // Root ref count is propagated by means of an "extra ref" count equal to the parent ref count minus one ref through which we reached the child
    // "Extra ref" count is updated just before decreasing ref counts through the whole data structure

    // TODO: consider structures like a -> c <- b

    switch (type->kind)
    {
        case TYPE_PTR:
        {
            if (tokKind == TOK_PLUSPLUS)
                chunkTryIncCnt(chunks, ptr);
            else
            {
                int newExtraRefCnt = extraRefCnt;
                HeapChunk *chunk = chunkFind(chunks, ptr);
                if (chunk)
                {
                    if (!root)
                    {
                        int deltaExtraRefCnt = extraRefCnt - chunk->extraRefCnt;
                        chunk->extraRefCnt = extraRefCnt;
#ifdef DEBUG_REF_CNT
                        if (deltaExtraRefCnt < 0)
                            printf("Extra ref count delta < 0\n");
                        for (int i = 0; i < deltaExtraRefCnt; i++)
                            printf("*");
                        printf("|");
#endif // DEBUG_REF_CNT
                        chunk->refCnt += deltaExtraRefCnt;
                    }

                    newExtraRefCnt = chunk->refCnt - 1;

                    if (ptr && typeGarbageCollectedRuntime(type->base))
                    {
                        void *data = ptr;
                        if (type->base->kind == TYPE_PTR)
                            data = *(void **)data;

                        doBasicChangeRefCnt(fiber, chunks, data, type->base, newExtraRefCnt, false, tokKind, error);
                    }

                    if (!root && chunk->extraRefCnt > 0)
                        chunk->extraRefCnt--;

                    chunkTryDecCnt(chunks, ptr);
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

                    doBasicChangeRefCnt(fiber, chunks, item, type->base, extraRefCnt, root, tokKind, error);
                    itemPtr += itemSize;
                }
            }
            break;
        }

        case TYPE_DYNARRAY:
        {
            DynArray *array = (DynArray *)ptr;

            if (typeGarbageCollectedRuntime(type->base))
            {
                void *itemPtr = array->data;

                for (int i = 0; i < array->len; i++)
                {
                    void *item = itemPtr;
                    if (type->base->kind == TYPE_PTR)
                        item = *(void **)item;

                    doBasicChangeRefCnt(fiber, chunks, item, type->base, extraRefCnt, root, tokKind, error);
                    itemPtr += array->itemSize;
                }
            }

            if (tokKind == TOK_PLUSPLUS)
                chunkTryIncCnt(chunks, array->data);
            else
                chunkTryDecCnt(chunks, array->data);
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

                    doBasicChangeRefCnt(fiber, chunks, field, type->field[i]->type, extraRefCnt, root, tokKind, error);
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
                doBasicChangeRefCnt(fiber, chunks, __self, __selftype, extraRefCnt, root, tokKind, error);
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


// fn make([] type (actually itemSize: int), len: int): [var] type
static void doBuiltinMake(Fiber *fiber, HeapChunks *chunks)
{
    DynArray *result = (fiber->top++)->ptrVal;
    result->len      = (fiber->top++)->intVal;
    result->itemSize = (fiber->top++)->intVal;
    result->data     = chunkAdd(chunks, result->len * result->itemSize)->ptr;

    (--fiber->top)->ptrVal = result;
}


// fn makefrom(array: [] type, itemSize: int, len: int): [var] type
static void doBuiltinMakeFrom(Fiber *fiber, HeapChunks *chunks)
{
    doBuiltinMake(fiber, chunks);

    DynArray *dest = (fiber->top++)->ptrVal;
    void *src      = (fiber->top++)->ptrVal;

    memcpy(dest->data, src, dest->len * dest->itemSize);
    (--fiber->top)->ptrVal = dest;
}


// fn append(array: [var] type, item: ^type): [var] type
static void doBuiltinAppend(Fiber *fiber, HeapChunks *chunks, ErrorFunc error)
{
    DynArray *result = (fiber->top++)->ptrVal;
    void *item       = (fiber->top++)->ptrVal;
    DynArray *array  = (fiber->top++)->ptrVal;

    if (!array->data)
        error("Dynamic array is not initialized");

    result->len      = array->len + 1;
    result->itemSize = array->itemSize;
    result->data     = chunkAdd(chunks, result->len * result->itemSize)->ptr;

    memcpy(result->data, array->data, array->len * array->itemSize);
    memcpy(result->data + (result->len - 1) * result->itemSize, item, result->itemSize);

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


// type FiberFunc = fn(parent: ^void, anyParam: ^type)
// fn fiberspawn(childFunc: FiberFunc, anyParam: ^type)
// fn fiberfree(child: ^void)
// fn fibercall(child: ^void)
// fn fiberalive(child: ^void)
static void doBuiltinFiber(Fiber *fiber, Fiber **newFiber, BuiltinFunc builtin, ErrorFunc error)
{
    switch (builtin)
    {
        case BUILTIN_FIBERSPAWN:
        {
            void *anyParam = (fiber->top++)->ptrVal;
            int childEntryOffset = (fiber->top++)->intVal;

            // Copy whole fiber context
            Fiber *child = malloc(sizeof(Fiber));
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
            break;
        }
        case BUILTIN_FIBERFREE:
        {
            // TODO: use garbage collection for fibers and eliminate fiberfree()
            Fiber *ptr = (fiber->top++)->ptrVal;
            free(ptr->stack);
            free(ptr);
            break;
        }
        case BUILTIN_FIBERCALL:
        {
            *newFiber = (fiber->top++)->ptrVal;
            if (!(*newFiber)->alive)
                error("Fiber is dead");
            break;
        }
        case BUILTIN_FIBERALIVE:
        {
            Fiber *child = fiber->top->ptrVal;
            fiber->top->intVal = child->alive;
            break;
        }
        default: error("Illegal instruction"); return;

    }
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


static void doChangeRefCnt(Fiber *fiber, HeapChunks *chunks, ErrorFunc error)
{
    void *ptr         = fiber->top->ptrVal;
    TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    Type *type        = fiber->code[fiber->ip].operand.ptrVal;

    doBasicChangeRefCnt(fiber, chunks, ptr, type, 0, true, tokKind, error);

    if (fiber->code[fiber->ip].inlinePop)
        fiber->top++;

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


static void doCallBuiltin(Fiber *fiber, Fiber **newFiber, HeapChunks *chunks, ErrorFunc error)
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
        case BUILTIN_NEW:           fiber->top->ptrVal = chunkAdd(chunks, fiber->top->intVal)->ptr; break;
        case BUILTIN_MAKE:          doBuiltinMake(fiber, chunks); break;
        case BUILTIN_MAKEFROM:      doBuiltinMakeFrom(fiber, chunks); break;
        case BUILTIN_APPEND:        doBuiltinAppend(fiber, chunks, error); break;
        case BUILTIN_LEN:           doBuiltinLen(fiber, error); break;
        case BUILTIN_SIZEOF:        error("Illegal instruction"); return;   // Done at compile time

        // Fibers
        case BUILTIN_FIBERSPAWN:
        case BUILTIN_FIBERFREE:
        case BUILTIN_FIBERCALL:
        case BUILTIN_FIBERALIVE:    doBuiltinFiber(fiber, newFiber, builtin, error); break;
    }
    fiber->ip++;
}


static void doReturn(Fiber *fiber, Fiber **newFiber)
{
    // Pop return address, remove parameters and entry point address from stack and go back
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


static void fiberStep(Fiber *fiber, Fiber **newFiber, HeapChunks *chunks, ErrorFunc error)
{
    if (fiber->top - fiber->stack < VM_MIN_FREE_STACK)
        error("Stack overflow");

    switch (fiber->code[fiber->ip].opcode)
    {
        case OP_PUSH:               doPush(fiber, error);                          break;
        case OP_PUSH_LOCAL_PTR:     doPushLocalPtr(fiber, error);                  break;
        case OP_PUSH_REG:           doPushReg(fiber);                              break;
        case OP_PUSH_STRUCT:        doPushStruct(fiber, error);                    break;
        case OP_POP:                doPop(fiber);                                  break;
        case OP_POP_REG:            doPopReg(fiber);                               break;
        case OP_DUP:                doDup(fiber);                                  break;
        case OP_SWAP:               doSwap(fiber);                                 break;
        case OP_DEREF:              doDeref(fiber, error);                         break;
        case OP_ASSIGN:             doAssign(fiber, error);                        break;
        case OP_SWAP_ASSIGN:        doSwapAssign(fiber, error);                    break;
        case OP_ASSIGN_OFS:         doAssignOfs(fiber);                            break;
        case OP_SWAP_ASSIGN_OFS:    doSwapAssignOfs(fiber);                        break;
        case OP_CHANGE_REF_CNT:     doChangeRefCnt(fiber, chunks, error);          break;
        case OP_UNARY:              doUnary(fiber, error);                         break;
        case OP_BINARY:             doBinary(fiber, error);                        break;
        case OP_GET_ARRAY_PTR:      doGetArrayPtr(fiber, error);                   break;
        case OP_GET_DYNARRAY_PTR:   doGetDynArrayPtr(fiber, error);                break;
        case OP_GET_FIELD_PTR:      doGetFieldPtr(fiber, error);                   break;
        case OP_GOTO:               doGoto(fiber);                                 break;
        case OP_GOTO_IF:            doGotoIf(fiber);                               break;
        case OP_CALL:               doCall(fiber, error);                          break;
        case OP_CALL_EXTERN:        doCallExtern(fiber);                           break;
        case OP_CALL_BUILTIN:       doCallBuiltin(fiber, newFiber, chunks, error); break;
        case OP_RETURN:             doReturn(fiber, newFiber);                     break;
        case OP_ENTER_FRAME:        doEnterFrame(fiber, error);                    break;
        case OP_LEAVE_FRAME:        doLeaveFrame(fiber);                           break;

        default: error("Illegal instruction"); return;
    } // switch
}


void vmRun(VM *vm)
{
    while (vm->fiber->alive && vm->fiber->code[vm->fiber->ip].opcode != OP_HALT)
    {
#ifdef DEBUG_REF_CNT
        printf(" %d", vm->fiber->ip);
#endif // DEBUG_REF_CNT

        Fiber *newFiber = NULL;
        fiberStep(vm->fiber, &newFiber, &vm->chunks, vm->error);
        if (newFiber)
            vm->fiber = newFiber;
    }
}


int vmAsm(int ip, Instruction *instr, char *buf)
{
    int chars = sprintf(buf, "%09d %6d %16s", ip, instr->debug.line, opcodeSpelling[instr->opcode]);

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
        case OP_ENTER_FRAME:    chars += sprintf(buf + chars, " %lld", (long long int)instr->operand.intVal); break;
        case OP_CALL_EXTERN:    chars += sprintf(buf + chars, " %p",   instr->operand.ptrVal); break;
        case OP_CALL_BUILTIN:   chars += sprintf(buf + chars, " %s",   builtinSpelling[instr->operand.builtinVal]); break;
        case OP_CHANGE_REF_CNT:
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
