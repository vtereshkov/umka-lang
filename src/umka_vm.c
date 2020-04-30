#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "umka_vm.h"


static char *spelling [] =
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
    "ASSIGN_OFS",
    "TRY_INC_REF_CNT",
    "TRY_DEC_REF_CNT",
    "UNARY",
    "BINARY",
    "GET_ARRAY_PTR",
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


// Heap chunks

void chunkInit(HeapChunks *chunks)
{
    chunks->first = chunks->last = NULL;
}


void chunkFree(HeapChunks *chunks)
{
    HeapChunk *chunk = chunks->first;
    while (chunk)
    {
        HeapChunk *next = chunk->next;
        if (chunk->ptr)
        {
            printf("Memory leak at %08p with %d references\n", chunk->ptr, chunk->refCnt);
            free(chunk->ptr);
        }
        free(chunk);
        chunk = next;
    }
}


HeapChunk *chunkAdd(HeapChunks *chunks, int size)
{
    HeapChunk *chunk = malloc(sizeof(HeapChunk));

    chunk->ptr = malloc(size);
    chunk->size = size;
    chunk->refCnt = 0;
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


HeapChunk *chunkFind(HeapChunks *chunks, void *ptr)
{
    for (HeapChunk *chunk = chunks->first; chunk; chunk = chunk->next)
        if (ptr >= chunk->ptr && ptr < chunk->ptr + chunk->size)
            return chunk;
    return NULL;
}


bool chunkTryIncCnt(HeapChunks *chunks, void *ptr)
{
    HeapChunk *chunk = chunkFind(chunks, ptr);
    if (chunk)
    {
        chunk->refCnt++;
        return true;
    }
    return false;
}


bool chunkTryDecCnt(HeapChunks *chunks, void *ptr)
{
    HeapChunk *chunk = chunkFind(chunks, ptr);
    if (chunk)
    {
        if (--chunk->refCnt == 0)
        {
            free(chunk->ptr);
            chunk->ptr = NULL;
        }
        return true;
    }
    return false;
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


static void doPrintf(Fiber *fiber, bool console, bool string, ErrorFunc error)
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


static void doScanf(Fiber *fiber, bool console, bool string, ErrorFunc error)
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


static void doFiberBuiltin(Fiber *fiber, Fiber **newFiber, BuiltinFunc builtin, ErrorFunc error)
{
    switch (builtin)
    {
        case BUILTIN_FIBERSPAWN:
        {
            // type FiberFunc = fn(parent: ^void, anyParam: ^type)
            // fn fiberspawn(childFunc: FiberFunc, anyParam: ^type)

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


static void doPush(Fiber *fiber)
{
    (--fiber->top)->intVal = fiber->code[fiber->ip].operand.intVal;
    fiber->ip++;
}


static void doPushLocalPtr(Fiber *fiber)
{
    // Local variable addresses are offsets (in bytes) from the stack frame base pointer
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->code[fiber->ip].operand.intVal;
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
    int slots = align(size, sizeof(Slot)) / sizeof(Slot);

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
    void *ptr = fiber->top->ptrVal;
    switch (fiber->code[fiber->ip].typeKind)
    {
        case TYPE_INT8:         fiber->top->intVal  = *(int8_t   *)ptr; break;
        case TYPE_INT16:        fiber->top->intVal  = *(int16_t  *)ptr; break;
        case TYPE_INT32:        fiber->top->intVal  = *(int32_t  *)ptr; break;
        case TYPE_INT:          fiber->top->intVal  = *(int64_t  *)ptr; break;
        case TYPE_UINT8:        fiber->top->intVal  = *(uint8_t  *)ptr; break;
        case TYPE_UINT16:       fiber->top->intVal  = *(uint16_t *)ptr; break;
        case TYPE_UINT32:       fiber->top->intVal  = *(uint32_t *)ptr; break;
        case TYPE_BOOL:         fiber->top->intVal  = *(bool     *)ptr; break;
        case TYPE_CHAR:         fiber->top->intVal  = *(char     *)ptr; break;
        case TYPE_REAL32:       fiber->top->realVal = *(float    *)ptr; break;
        case TYPE_REAL:         fiber->top->realVal = *(double   *)ptr; break;
        case TYPE_PTR:          fiber->top->ptrVal  = *(void *   *)ptr; break;
        case TYPE_ARRAY:
        case TYPE_STR:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:    fiber->top->intVal =   (int64_t   )ptr; break;  // Always represented by pointer, not dereferenced
        case TYPE_FN:           fiber->top->ptrVal  = *(void *   *)ptr; break;

        default:                error("Illegal type"); return;
    }
    fiber->ip++;
}


static void doAssign(Fiber *fiber, ErrorFunc error)
{
    Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;
    switch (fiber->code[fiber->ip].typeKind)
    {
        case TYPE_INT8:   *(int8_t   *)lhs = rhs.intVal; break;
        case TYPE_INT16:  *(int16_t  *)lhs = rhs.intVal; break;
        case TYPE_INT32:  *(int32_t  *)lhs = rhs.intVal; break;
        case TYPE_INT:    *(int64_t  *)lhs = rhs.intVal; break;
        case TYPE_UINT8:  *(uint8_t  *)lhs = rhs.intVal; break;
        case TYPE_UINT16: *(uint16_t *)lhs = rhs.intVal; break;
        case TYPE_UINT32: *(uint32_t *)lhs = rhs.intVal; break;
        case TYPE_BOOL:   *(bool     *)lhs = rhs.intVal; break;
        case TYPE_CHAR:   *(char     *)lhs = rhs.intVal; break;
        case TYPE_REAL32: *(float    *)lhs = rhs.realVal; break;
        case TYPE_REAL:   *(double   *)lhs = rhs.realVal; break;
        case TYPE_PTR:    *(void *   *)lhs = rhs.ptrVal; break;
        case TYPE_ARRAY:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        {
            int size = fiber->code[fiber->ip].operand.intVal;
            memcpy(lhs, rhs.ptrVal, size);
            break;
        }
        case TYPE_STR:    strcpy(lhs, rhs.ptrVal); break;
        case TYPE_FN:     *(void *   *)lhs = rhs.ptrVal; break;

        default:          error("Illegal type"); return;
    }
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


static void doTryIncRefCnt(Fiber *fiber, HeapChunks *chunks)
{
    void *ptr = fiber->top->ptrVal;
    chunkTryIncCnt(chunks, ptr);
    fiber->ip++;
}


static void doTryDecRefCnt(Fiber *fiber, HeapChunks *chunks)
{
    void *ptr = fiber->top->ptrVal;
    chunkTryDecCnt(chunks, ptr);
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
    int len = (fiber->top++)->intVal;
    int index = (fiber->top++)->intVal;

    if (index < 0 || index > len - 1)
        error("Index is out of range");

    fiber->top->ptrVal += itemSize * index;
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


static void doCall(Fiber *fiber)
{
    // All calls are indirect, entry point address is below the parameters
    int paramSlots = fiber->code[fiber->ip].operand.intVal;
    int entryOffset = (fiber->top + paramSlots)->intVal;

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
        case BUILTIN_PRINTF:        doPrintf(fiber, true, false, error); break;
        case BUILTIN_FPRINTF:       doPrintf(fiber, false, false, error); break;
        case BUILTIN_SPRINTF:       doPrintf(fiber, false, true, error); break;
        case BUILTIN_SCANF:         doScanf(fiber, true, false, error); break;
        case BUILTIN_FSCANF:        doScanf(fiber, false, false, error); break;
        case BUILTIN_SSCANF:        doScanf(fiber, false, true, error); break;

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
        case BUILTIN_SIZEOF:        error("Illegal instruction"); return;   // Does not produce VM instructions

        // Fibers
        case BUILTIN_FIBERSPAWN:
        case BUILTIN_FIBERFREE:
        case BUILTIN_FIBERCALL:
        case BUILTIN_FIBERALIVE:    doFiberBuiltin(fiber, newFiber, builtin, error); break;
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
    int slots = align(size, sizeof(Slot)) / sizeof(Slot);

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error("Stack overflow");

    (--fiber->top)->ptrVal = fiber->base;
    fiber->base = fiber->top;
    fiber->top -= slots;

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


void fiberStep(Fiber *fiber, Fiber **newFiber, HeapChunks *chunks, ErrorFunc error)
{
    if (fiber->top - fiber->stack < VM_MIN_FREE_STACK)
        error("Stack overflow");

    switch (fiber->code[fiber->ip].opcode)
    {
        case OP_PUSH:            doPush(fiber);                                 break;
        case OP_PUSH_LOCAL_PTR:  doPushLocalPtr(fiber);                         break;
        case OP_PUSH_REG:        doPushReg(fiber);                              break;
        case OP_PUSH_STRUCT:     doPushStruct(fiber, error);                    break;
        case OP_POP:             doPop(fiber);                                  break;
        case OP_POP_REG:         doPopReg(fiber);                               break;
        case OP_DUP:             doDup(fiber);                                  break;
        case OP_SWAP:            doSwap(fiber);                                 break;
        case OP_DEREF:           doDeref(fiber, error);                         break;
        case OP_ASSIGN:          doAssign(fiber, error);                        break;
        case OP_ASSIGN_OFS:      doAssignOfs(fiber);                            break;
        case OP_TRY_INC_REF_CNT: doTryIncRefCnt(fiber, chunks);                 break;
        case OP_TRY_DEC_REF_CNT: doTryDecRefCnt(fiber, chunks);                 break;
        case OP_UNARY:           doUnary(fiber, error);                         break;
        case OP_BINARY:          doBinary(fiber, error);                        break;
        case OP_GET_ARRAY_PTR:   doGetArrayPtr(fiber, error);                   break;
        case OP_GOTO:            doGoto(fiber);                                 break;
        case OP_GOTO_IF:         doGotoIf(fiber);                               break;
        case OP_CALL:            doCall(fiber);                                 break;
        case OP_CALL_EXTERN:     doCallExtern(fiber);                           break;
        case OP_CALL_BUILTIN:    doCallBuiltin(fiber, newFiber, chunks, error); break;
        case OP_RETURN:          doReturn(fiber, newFiber);                     break;
        case OP_ENTER_FRAME:     doEnterFrame(fiber, error);                    break;
        case OP_LEAVE_FRAME:     doLeaveFrame(fiber);                           break;

        default: error("Illegal instruction"); return;
    } // switch
}


void vmRun(VM *vm)
{
    while (vm->fiber->alive && vm->fiber->code[vm->fiber->ip].opcode != OP_HALT)
    {
        //char buf[256];
        //printf("%s\n", vmAsm(&vm->fiber.code[vm->fiber.ip], buf));

        Fiber *newFiber = NULL;
        fiberStep(vm->fiber, &newFiber, &vm->chunks, vm->error);
        if (newFiber)
            vm->fiber = newFiber;
    }
}


char *vmAsm(Instruction *instr, char *buf)
{
    sprintf(buf, "%16s %10s %8s %16X", spelling[instr->opcode], lexSpelling(instr->tokKind),
                                       typeKindSpelling(instr->typeKind), (int)instr->operand.intVal);
    return buf;
}
