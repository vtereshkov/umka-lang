#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

#include "umka_vm.h"

//#define DEBUG_REF_CNT


static const char *opcodeSpelling [] =
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
    "makefrom",
    "append",
    "delete",
    "len",
    "sizeof",
    "sizeofself",
    "selfhasptr",
    "fiberspawn",
    "fibercall",
    "fiberalive",
    "repr",
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
        if (ptr >= page->ptr && ptr < (void *)((char *)page->ptr + page->occupied))
        {
            HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)ptr - sizeof(HeapChunkHeader));
            if (chunk->magic == VM_HEAP_CHUNK_MAGIC)
                return page;
            return NULL;
        }

    return NULL;
}


static void *chunkAlloc(HeapPages *pages, int size, Error *error)
{
    if (size < 0)
        error->handlerRuntime(error->context, "Allocated memory block size cannot be negative");

    // Page layout: header, data, footer (char), header, data, footer (char)...
    int chunkSize = sizeof(HeapChunkHeader) + align(size + 1, sizeof(int64_t));
    int pageSize = chunkSize > VM_MIN_HEAP_PAGE ? chunkSize : VM_MIN_HEAP_PAGE;

    if (!pages->last || pages->last->occupied + chunkSize > pages->last->size)
        pageAdd(pages, pageSize);

    HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)pages->last->ptr + pages->last->occupied);
    chunk->magic = VM_HEAP_CHUNK_MAGIC;
    chunk->refCnt = 1;
    chunk->size = size;

    pages->last->occupied += chunkSize;
    pages->last->refCnt++;

#ifdef DEBUG_REF_CNT
    printf("Add chunk at %p\n", (void *)chunk + sizeof(HeapChunkHeader));
#endif

    return (char *)chunk + sizeof(HeapChunkHeader);
}


static void chunkChangeRefCnt(HeapPages *pages, HeapPage *page, void *ptr, int delta)
{
    HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)ptr - sizeof(HeapChunkHeader));

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


// I/O functions

static char fsgetc(bool string, void *stream, int *len)
{
    char ch = string ? ((char *)stream)[*len] : fgetc((FILE *)stream);
    (*len)++;
    return ch;
}


static int fsprintf(bool string, void *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    int res = string ? vsprintf((char *)stream, format, args) : vfprintf((FILE *)stream, format, args);

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


static char *fsscanfString(bool string, void *stream, int *len)
{
    int capacity = 8;
    char *str = malloc(capacity);

    *len = 0;
    int writtenLen = 0;
    char ch = ' ';

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

void vmInit(VM *vm, int stackSize, Error *error)
{
    vm->fiber = vm->mainFiber = malloc(sizeof(Fiber));
    vm->fiber->stack = malloc(stackSize * sizeof(Slot));
    vm->fiber->stackSize = stackSize;
    vm->fiber->alive = true;
    pageInit(&vm->pages);
    vm->error = error;
}


void vmFree(VM *vm)
{
    pageFree(&vm->pages);
    free(vm->mainFiber->stack);
    free(vm->mainFiber);
}


void vmReset(VM *vm, Instruction *code)
{
    vm->fiber = vm->mainFiber;
    vm->fiber->code = code;
    vm->fiber->ip = 0;
    vm->fiber->top = vm->fiber->base = vm->fiber->stack + vm->fiber->stackSize - 1;
}


static void doBasicSwap(Slot *slot)
{
    Slot val = slot[0];
    slot[0] = slot[1];
    slot[1] = val;
}


static void doBasicDeref(Slot *slot, TypeKind typeKind, Error *error)
{
    if (!slot->ptrVal)
        error->handlerRuntime(error->context, "Pointer is null");

    switch (typeKind)
    {
        case TYPE_INT8:         slot->intVal  = *(int8_t   *)slot->ptrVal; break;
        case TYPE_INT16:        slot->intVal  = *(int16_t  *)slot->ptrVal; break;
        case TYPE_INT32:        slot->intVal  = *(int32_t  *)slot->ptrVal; break;
        case TYPE_INT:          slot->intVal  = *(int64_t  *)slot->ptrVal; break;
        case TYPE_UINT8:        slot->intVal  = *(uint8_t  *)slot->ptrVal; break;
        case TYPE_UINT16:       slot->intVal  = *(uint16_t *)slot->ptrVal; break;
        case TYPE_UINT32:       slot->intVal  = *(uint32_t *)slot->ptrVal; break;
        case TYPE_UINT:         slot->uintVal = *(uint64_t *)slot->ptrVal; break;
        case TYPE_BOOL:         slot->intVal  = *(bool     *)slot->ptrVal; break;
        case TYPE_CHAR:         slot->intVal  = *(char     *)slot->ptrVal; break;
        case TYPE_REAL32:       slot->realVal = *(float    *)slot->ptrVal; break;
        case TYPE_REAL:         slot->realVal = *(double   *)slot->ptrVal; break;
        case TYPE_PTR:
        case TYPE_STR:          slot->ptrVal  = (int64_t)(*(void *   *)slot->ptrVal); break;
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_FIBER:        break;  // Always represented by pointer, not dereferenced
        case TYPE_FN:           slot->intVal  = *(int64_t  *)slot->ptrVal; break;

        default:                error->handlerRuntime(error->context, "Illegal type"); return;
    }
}


static void doBasicAssign(void *lhs, Slot rhs, TypeKind typeKind, int structSize, Error *error)
{
    if (!lhs)
        error->handlerRuntime(error->context, "Pointer is null");

    Const rhsConstant = {.intVal = rhs.intVal};
    if (typeOverflow(typeKind, rhsConstant))
        error->handlerRuntime(error->context, "Overflow in assignment to %s", typeKindSpelling(typeKind));

    switch (typeKind)
    {
        case TYPE_INT8:         *(int8_t   *)lhs = rhs.intVal;  break;
        case TYPE_INT16:        *(int16_t  *)lhs = rhs.intVal;  break;
        case TYPE_INT32:        *(int32_t  *)lhs = rhs.intVal;  break;
        case TYPE_INT:          *(int64_t  *)lhs = rhs.intVal;  break;
        case TYPE_UINT8:        *(uint8_t  *)lhs = rhs.intVal;  break;
        case TYPE_UINT16:       *(uint16_t *)lhs = rhs.intVal;  break;
        case TYPE_UINT32:       *(uint32_t *)lhs = rhs.intVal;  break;
        case TYPE_UINT:         *(uint64_t *)lhs = rhs.uintVal; break;
        case TYPE_BOOL:         *(bool     *)lhs = rhs.intVal;  break;
        case TYPE_CHAR:         *(char     *)lhs = rhs.intVal;  break;
        case TYPE_REAL32:       *(float    *)lhs = rhs.realVal; break;
        case TYPE_REAL:         *(double   *)lhs = rhs.realVal; break;
        case TYPE_PTR:
        case TYPE_STR:          *(void *   *)lhs = (void *)rhs.ptrVal; break;
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_FIBER:        memcpy(lhs, (void *)rhs.ptrVal, structSize); break;
        case TYPE_FN:           *(int64_t  *)lhs = rhs.intVal; break;

        default:                error->handlerRuntime(error->context, "Illegal type"); return;
    }
}


static void doBasicChangeRefCnt(Fiber *fiber, HeapPages *pages, void *ptr, Type *type, TokenKind tokKind, Error *error)
{
    // Update ref counts for pointers (including static/dynamic array items and structure/interface fields) if allocated dynamically
    // Among garbage collected types, all types except the pointer and string types are represented by pointers by default
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
                    HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)ptr - sizeof(HeapChunkHeader));
                    if (chunk->refCnt == 1 && typeKindGarbageCollected(type->base->kind))
                    {
                        void *data = ptr;
                        if (type->base->kind == TYPE_PTR || type->base->kind == TYPE_STR)
                            data = *(void **)data;

                        doBasicChangeRefCnt(fiber, pages, data, type->base, tokKind, error);
                    }
                    chunkChangeRefCnt(pages, page, ptr, -1);
                }
            }
            break;
        }

        case TYPE_STR:
        {
            HeapPage *page = pageFind(pages, ptr);
            if (page)
                chunkChangeRefCnt(pages, page, ptr, (tokKind == TOK_PLUSPLUS) ? 1 : -1);
            break;
        }

        case TYPE_ARRAY:
        {
            if (typeKindGarbageCollected(type->base->kind))
            {
                char *itemPtr = ptr;
                int itemSize = typeSizeNoCheck(type->base);

                for (int i = 0; i < type->numItems; i++)
                {
                    void *item = itemPtr;
                    if (type->base->kind == TYPE_PTR || type->base->kind == TYPE_STR)
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
                    HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)array->data - sizeof(HeapChunkHeader));
                    if (chunk->refCnt == 1 && typeKindGarbageCollected(type->base->kind))
                    {
                        char *itemPtr = array->data;

                        for (int i = 0; i < array->len; i++)
                        {
                            void *item = itemPtr;
                            if (type->base->kind == TYPE_PTR || type->base->kind == TYPE_STR)
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
                if (typeKindGarbageCollected(type->field[i]->type->kind))
                {
                    void *field = (char *)ptr + type->field[i]->offset;
                    if (type->field[i]->type->kind == TYPE_PTR || type->field[i]->type->kind == TYPE_STR)
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
            Type *__selftype = *(Type **)((char *)ptr + type->field[1]->offset);

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
                HeapChunkHeader *chunk = (HeapChunkHeader *)((char *)ptr - sizeof(HeapChunkHeader));
                if (chunk->refCnt == 1 && tokKind == TOK_MINUSMINUS)
                    free(((Fiber *)ptr)->stack);
            }
            break;
        }

        default: break;
    }
}


static int doFillReprBuf(Slot *slot, Type *type, char *buf, int maxLen, Error *error)
{
    int len = 0;
    switch (type->kind)
    {
        case TYPE_VOID:     len = snprintf(buf, maxLen, "void ");                                           break;
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:   len = snprintf(buf, maxLen, "%lld ",  (long long int)slot->intVal);             break;
        case TYPE_UINT:     len = snprintf(buf, maxLen, "%llu ", (unsigned long long int)slot->uintVal);    break;
        case TYPE_BOOL:     len = snprintf(buf, maxLen, slot->intVal ? "true " : "false ");                 break;
        case TYPE_CHAR:     len = snprintf(buf, maxLen, "%c ", (char)slot->intVal);                         break;
        case TYPE_REAL32:
        case TYPE_REAL:     len = snprintf(buf, maxLen, "%lf ", slot->realVal);                             break;
        case TYPE_PTR:      len = snprintf(buf, maxLen, "%p ", (void *)slot->ptrVal);                       break;
        case TYPE_STR:      len = snprintf(buf, maxLen, "\"%s\" ", (char *)slot->ptrVal);                   break;

        case TYPE_ARRAY:
        {
            len += snprintf(buf, maxLen, "{ ");

            char *itemPtr = (char *)slot->ptrVal;
            int itemSize = typeSizeNoCheck(type->base);

            for (int i = 0; i < type->numItems; i++)
            {
                Slot itemSlot = {.ptrVal = (int64_t)itemPtr};
                doBasicDeref(&itemSlot, type->base->kind, error);
                len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, error);
                itemPtr += itemSize;
            }

            len += snprintf(buf + len, maxLen, "} ");
            break;
        }

        case TYPE_DYNARRAY:
        {
            len += snprintf(buf, maxLen, "{ ");

            DynArray *array = (DynArray *)slot->ptrVal;
            if (array && array->data)
            {
                char *itemPtr = array->data;
                for (int i = 0; i < array->len; i++)
                {
                    Slot itemSlot = {.ptrVal = (int64_t)itemPtr};
                    doBasicDeref(&itemSlot, type->base->kind, error);
                    len += doFillReprBuf(&itemSlot, type->base, buf + len, maxLen, error);
                    itemPtr += array->itemSize;
                }
            }

            len += snprintf(buf + len, maxLen, "} ");
            break;
        }

        case TYPE_STRUCT:
        {
            len += snprintf(buf, maxLen, "{ ");
            bool skipNames = typeExprListStruct(type);

            for (int i = 0; i < type->numItems; i++)
            {
                Slot fieldSlot = {.ptrVal = slot->ptrVal + type->field[i]->offset};
                doBasicDeref(&fieldSlot, type->field[i]->type->kind, error);
                if (!skipNames)
                    len += snprintf(buf + len, maxLen, "%s: ", type->field[i]->name);
                len += doFillReprBuf(&fieldSlot, type->field[i]->type, buf + len, maxLen, error);
            }

            len += snprintf(buf + len, maxLen, "} ");
            break;
        }

        case TYPE_INTERFACE:
        {
            // Interface layout: __self, __selftype, methods
            void *__self = *(void **)slot->ptrVal;
            Type *__selftype = *(Type **)(slot->ptrVal + type->field[1]->offset);

            if (__self)
            {
                Slot selfSlot = {.ptrVal = (int64_t)__self};
                doBasicDeref(&selfSlot, __selftype->base->kind, error);
                len += doFillReprBuf(&selfSlot, __selftype->base, buf + len, maxLen, error);
            }
            break;
        }

        case TYPE_FIBER:    len = snprintf(buf, maxLen, "fiber ");                                 break;
        case TYPE_FN:       len = snprintf(buf, maxLen, "fn ");                                    break;
        default:            break;
    }

    return len;
}


static void doCheckFormatString(const char *format, int *formatLen, TypeKind *typeKind, Error *error)
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

                default : error->handlerRuntime(error->context, "Illegal type character %c in format string", format[i]);
            }
            i++;
        }
        break;
    }
    *formatLen = i;
}


static void doBuiltinPrintf(Fiber *fiber, bool console, bool string, Error *error)
{
    void *stream       = console ? stdout : (void *)fiber->reg[VM_REG_IO_STREAM].ptrVal;
    const char *format = (const char *)fiber->reg[VM_REG_IO_FORMAT].ptrVal;
    TypeKind typeKind  = fiber->code[fiber->ip].typeKind;

    if (!stream)
        error->handlerRuntime(error->context, "printf() destination is null");

    if (!format)
        error->handlerRuntime(error->context, "printf() format string is null");

    int formatLen;
    TypeKind expectedTypeKind;
    doCheckFormatString(format, &formatLen, &expectedTypeKind, error);

    if (typeKind != expectedTypeKind && !(typeKindInteger(typeKind) && typeKindInteger(expectedTypeKind)) &&
                                        !(typeKindReal(typeKind)    && typeKindReal(expectedTypeKind)))
        error->handlerRuntime(error->context, "Incompatible types %s and %s in printf()", typeKindSpelling(expectedTypeKind), typeKindSpelling(typeKind));

    char *curFormat = malloc(formatLen + 1);
    strncpy(curFormat, format, formatLen);
    curFormat[formatLen] = 0;

    int len = 0;

    if (typeKind == TYPE_VOID)
        len = fsprintf(string, stream, curFormat);
    else if (typeKind == TYPE_REAL || typeKind == TYPE_REAL32)
        len = fsprintf(string, stream, curFormat, fiber->top->realVal);
    else
        len = fsprintf(string, stream, curFormat, fiber->top->intVal);

    fiber->reg[VM_REG_IO_FORMAT].ptrVal += formatLen;
    fiber->reg[VM_REG_IO_COUNT].intVal += len;
    if (string)
        fiber->reg[VM_REG_IO_STREAM].ptrVal += len;

    free(curFormat);
}


static void doBuiltinScanf(Fiber *fiber, HeapPages *pages, bool console, bool string, Error *error)
{
    void *stream       = console ? stdin : (void *)fiber->reg[VM_REG_IO_STREAM].ptrVal;
    const char *format = (const char *)fiber->reg[VM_REG_IO_FORMAT].ptrVal;
    TypeKind typeKind  = fiber->code[fiber->ip].typeKind;

    if (!stream)
        error->handlerRuntime(error->context, "scanf() source is null");

    if (!format)
        error->handlerRuntime(error->context, "scanf() format string is null");

    int formatLen;
    TypeKind expectedTypeKind;
    doCheckFormatString(format, &formatLen, &expectedTypeKind, error);

    if (typeKind != expectedTypeKind)
        error->handlerRuntime(error->context, "Incompatible types %s and %s in scanf()", typeKindSpelling(expectedTypeKind), typeKindSpelling(typeKind));

    char *curFormat = malloc(formatLen + 2 + 1);     // + 2 for "%n"
    strncpy(curFormat, format, formatLen);
    curFormat[formatLen] = 0;
    strcat(curFormat, "%n");

    int len = 0, cnt = 0;

    if (typeKind == TYPE_VOID)
        cnt = fsscanf(string, stream, curFormat, &len);
    else
    {
        if (!fiber->top->ptrVal)
            error->handlerRuntime(error->context, "scanf() destination is null");

        // Strings need special handling, as the required buffer size is unknown
        if (typeKind == TYPE_STR)
        {
            char *src = fsscanfString(string, stream, &len);
            char **dest = (char **)fiber->top->ptrVal;

            // Decrease old string ref count
            Type destType = {.kind = TYPE_STR};
            doBasicChangeRefCnt(fiber, pages, *dest, &destType, TOK_MINUSMINUS, error);

            // Allocate new string
            *dest = chunkAlloc(pages, strlen(src) + 1, error);
            strcpy(*dest, src);
            free(src);

            cnt = (*dest)[0] ? 1 : 0;
        }
        else
            cnt = fsscanf(string, stream, curFormat, (void *)fiber->top->ptrVal, &len);
    }

    fiber->reg[VM_REG_IO_FORMAT].ptrVal += formatLen;
    fiber->reg[VM_REG_IO_COUNT].intVal += cnt;
    if (string)
        fiber->reg[VM_REG_IO_STREAM].ptrVal += len;

    free(curFormat);
}


// fn make([...] type (actually itemSize: int), len: int): [] type
static void doBuiltinMake(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    result->len      = (fiber->top++)->intVal;
    result->itemSize = (fiber->top++)->intVal;
    result->data     = chunkAlloc(pages, result->len * result->itemSize, error);

    (--fiber->top)->ptrVal = (int64_t)result;
}


// fn makefrom(array: [...] type, itemSize: int, len: int): [] type
static void doBuiltinMakefrom(Fiber *fiber, HeapPages *pages, Error *error)
{
    doBuiltinMake(fiber, pages, error);

    DynArray *dest = (DynArray *)(fiber->top++)->ptrVal;
    void *src      = (void     *)(fiber->top++)->ptrVal;

    memcpy(dest->data, src, dest->len * dest->itemSize);
    (--fiber->top)->ptrVal = (int64_t)dest;
}


// fn append(array: [] type, item: (^type | [] type), single: bool): [] typee
static void doBuiltinAppend(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    bool single      = (bool      )(fiber->top++)->intVal;
    void *item       = (void     *)(fiber->top++)->ptrVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    if (!array || !array->data)
        error->handlerRuntime(error->context, "Dynamic array is null");

    void *rhs = item;
    int rhsLen = 1;

    if (!single)
    {
        DynArray *rhsArray = item;

        if (!rhsArray || !rhsArray->data)
            error->handlerRuntime(error->context, "Dynamic array is null");

        rhs = rhsArray->data;
        rhsLen = rhsArray->len;
    }

    result->len      = array->len + rhsLen;
    result->itemSize = array->itemSize;
    result->data     = chunkAlloc(pages, result->len * result->itemSize, error);

    memcpy((char *)result->data, (char*)array->data, array->len * array->itemSize);
    memcpy((char *)result->data + array->len * array->itemSize, (char *)rhs, rhsLen * array->itemSize);

    (--fiber->top)->ptrVal = (int64_t)result;
}


// fn delete(array: [] type, index: int): [] type
static void doBuiltinDelete(Fiber *fiber, HeapPages *pages, Error *error)
{
    DynArray *result = (DynArray *)(fiber->top++)->ptrVal;
    int index        =             (fiber->top++)->intVal;
    DynArray *array  = (DynArray *)(fiber->top++)->ptrVal;

    if (!array || !array->data)
        error->handlerRuntime(error->context, "Dynamic array is null");

    result->len      = array->len - 1;
    result->itemSize = array->itemSize;
    result->data     = chunkAlloc(pages, result->len * result->itemSize, error);

    memcpy((char *)result->data, (char *)array->data, index * array->itemSize);
    memcpy((char *)result->data + index * result->itemSize, (char *)array->data + (index + 1) * result->itemSize, (result->len - index) * result->itemSize);

    (--fiber->top)->ptrVal = (int64_t)result;
}


static void doBuiltinLen(Fiber *fiber, Error *error)
{
    if (!fiber->top->ptrVal)
        error->handlerRuntime(error->context, "Dynamic array or string is null");

    switch (fiber->code[fiber->ip].typeKind)
    {
        // Done at compile time for arrays
        case TYPE_DYNARRAY: fiber->top->intVal = ((DynArray *)(fiber->top->ptrVal))->len; break;
        case TYPE_STR:      fiber->top->intVal = strlen((char *)fiber->top->ptrVal); break;
        default:            error->handlerRuntime(error->context, "Illegal type"); return;
    }
}


static void doBuiltinSizeofself(Fiber *fiber, Error *error)
{
    int size = 0;

    // Interface layout: __self, __selftype, methods
    Type *__selftype = *(Type **)(fiber->top->ptrVal + sizeof(void *));
    if (__selftype)
        size = typeSizeNoCheck(__selftype->base);

    fiber->top->intVal = size;
}


static void doBuiltinSelfhasptr(Fiber *fiber, Error *error)
{
    bool hasPtr = false;

    // Interface layout: __self, __selftype, methods
    Type *__selftype = *(Type **)(fiber->top->ptrVal + sizeof(void *));
    if (__selftype)
        hasPtr = typeGarbageCollected(__selftype->base);

    fiber->top->intVal = hasPtr;
}


// type FiberFunc = fn(parent: ^fiber, anyParam: ^type)
// fn fiberspawn(childFunc: FiberFunc, anyParam: ^type): ^fiber
static void doBuiltinFiberspawn(Fiber *fiber, HeapPages *pages, Error *error)
{
    void *anyParam = (void *)(fiber->top++)->ptrVal;
    int childEntryOffset = (fiber->top++)->intVal;

    // Copy whole fiber context
    Fiber *child = chunkAlloc(pages, sizeof(Fiber), error);
    *child = *fiber;

    child->stack = malloc(fiber->stackSize * sizeof(Slot));
    *(child->stack) = *(fiber->stack);

    child->top  = child->stack + (fiber->top  - fiber->stack);
    child->base = child->stack + (fiber->base - fiber->stack);

    // Call child fiber function
    (--child->top)->ptrVal = (int64_t)fiber;                  // Push parent fiber pointer
    (--child->top)->ptrVal = (int64_t)anyParam;               // Push arbitrary pointer parameter
    (--child->top)->intVal = VM_FIBER_KILL_SIGNAL;   // Push fiber kill signal instead of return address
    child->ip = childEntryOffset;                    // Call

    // Return child fiber pointer to parent fiber as result
    (--fiber->top)->ptrVal = (int64_t)child;
}


// fn fibercall(child: ^fiber)
static void doBuiltinFibercall(Fiber *fiber, Fiber **newFiber, HeapPages *pages, Error *error)
{
    *newFiber = (Fiber *)(fiber->top++)->ptrVal;
    if (!(*newFiber) || !(*newFiber)->alive)
        error->handlerRuntime(error->context, "Fiber is null");
}


// fn fiberalive(child: ^fiber)
static void doBuiltinFiberalive(Fiber *fiber, HeapPages *pages, Error *error)
{
    Fiber *child = (Fiber *)fiber->top->ptrVal;
    if (!child)
        error->handlerRuntime(error->context, "Fiber is null");

    fiber->top->intVal = child->alive;
}


// fn repr(val: type, type): str
static void doBuiltinRepr(Fiber *fiber, HeapPages *pages, Error *error)
{
    Type *type = (Type *)(fiber->top++)->ptrVal;
    Slot *val = fiber->top;

    int len = doFillReprBuf(val, type, NULL, 0, error);     // Predict buffer length
    char *buf = chunkAlloc(pages, len + 1, error);          // Allocate buffer
    doFillReprBuf(val, type, buf, INT_MAX, error);          // Fill buffer

    fiber->top->ptrVal = (int64_t)buf;
}


static void doPush(Fiber *fiber, Error *error)
{
    (--fiber->top)->intVal = fiber->code[fiber->ip].operand.intVal;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doPushLocalPtr(Fiber *fiber, Error *error)
{
    // Local variable addresses are offsets (in bytes) from the stack frame base pointer
    (--fiber->top)->ptrVal = (int64_t)((int8_t *)fiber->base + fiber->code[fiber->ip].operand.intVal);

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doPushReg(Fiber *fiber)
{
    (--fiber->top)->intVal = fiber->reg[fiber->code[fiber->ip].operand.intVal].intVal;
    fiber->ip++;
}


static void doPushStruct(Fiber *fiber, Error *error)
{
    void *src = (void *)(fiber->top++)->ptrVal;
    int size  = fiber->code[fiber->ip].operand.intVal;
    int slots = align(size, sizeof(Slot)) / sizeof(Slot);

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error->handlerRuntime(error->context, "Stack overflow");

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
    doBasicSwap(fiber->top);
    fiber->ip++;
}


static void doDeref(Fiber *fiber, Error *error)
{
    doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);
    fiber->ip++;
}


static void doAssign(Fiber *fiber, Error *error)
{
    if (fiber->code[fiber->ip].inlineOpcode == OP_SWAP)
        doBasicSwap(fiber->top);

    Slot rhs = *fiber->top++;
    void *lhs = (void *)(fiber->top++)->ptrVal;;

    doBasicAssign(lhs, rhs, fiber->code[fiber->ip].typeKind, fiber->code[fiber->ip].operand.intVal, error);
    fiber->ip++;
}


static void doChangeRefCnt(Fiber *fiber, HeapPages *pages, Error *error)
{
    void *ptr         = (void *)fiber->top->ptrVal;
    TokenKind tokKind = fiber->code[fiber->ip].tokKind;
    Type *type        = (Type *)fiber->code[fiber->ip].operand.ptrVal;

    doBasicChangeRefCnt(fiber, pages, ptr, type, tokKind, error);

    if (fiber->code[fiber->ip].inlineOpcode == OP_POP)
        fiber->top++;

    fiber->ip++;
}


static void doChangeRefCntAssign(Fiber *fiber, HeapPages *pages, Error *error)
{
    if (fiber->code[fiber->ip].inlineOpcode == OP_SWAP)
        doBasicSwap(fiber->top);

    Slot rhs   = *fiber->top++;
    void *lhs  = (void *)(fiber->top++)->ptrVal;
    Type *type = (Type *)fiber->code[fiber->ip].operand.ptrVal;

    // Increase right-hand side ref count
    doBasicChangeRefCnt(fiber, pages, (void *)rhs.ptrVal, type, TOK_PLUSPLUS, error);

    // Decrease left-hand side ref count
    Slot lhsDeref = {.ptrVal = (int64_t)lhs};
    doBasicDeref(&lhsDeref, type->kind, error);
    doBasicChangeRefCnt(fiber, pages, (void *)lhsDeref.ptrVal, type, TOK_MINUSMINUS, error);

    doBasicAssign(lhs, rhs, type->kind, typeSizeNoCheck(type), error);
    fiber->ip++;
}


static void doUnary(Fiber *fiber, Error *error)
{
    if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_MINUS: fiber->top->realVal = -fiber->top->realVal; break;
            default:        error->handlerRuntime(error->context, "Illegal instruction"); return;
        }
    else
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_MINUS:      fiber->top->intVal = -fiber->top->intVal; break;
            case TOK_NOT:        fiber->top->intVal = !fiber->top->intVal; break;
            case TOK_XOR:        fiber->top->intVal = ~fiber->top->intVal; break;

            case TOK_PLUSPLUS:
            {
                void *ptr = (void *)(fiber->top++)->ptrVal;
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
                    case TYPE_CHAR:   (*(char     *)ptr)++; break;
                    case TYPE_PTR:    (*(char *   *)ptr)++; break;
                    // Structured, boolean and real types are not incremented/decremented
                    default:          error->handlerRuntime(error->context, "Illegal type"); return;
                }
            break;
            }

            case TOK_MINUSMINUS:
            {
                void *ptr = (void *)(fiber->top++)->ptrVal;
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
                    case TYPE_CHAR:   (*(char     *)ptr)--; break;
                    case TYPE_PTR:    (*(char *   *)ptr)--; break;
                    // Structured, boolean and real types are not incremented/decremented
                    default:          error->handlerRuntime(error->context, "Illegal type"); return;
                }
            break;
            }

            default: error->handlerRuntime(error->context, "Illegal instruction"); return;
        }
    fiber->ip++;
}


static void doBinary(Fiber *fiber, HeapPages *pages, Error *error)
{
    Slot rhs = *fiber->top++;

    if (fiber->code[fiber->ip].typeKind == TYPE_STR)
    {
        if (!fiber->top->ptrVal || !rhs.ptrVal)
            error->handlerRuntime(error->context, "String is null");

        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:
            {
                char *buf = chunkAlloc(pages, strlen((char *)fiber->top->ptrVal) + strlen((char *)rhs.ptrVal) + 1, error);
                strcpy(buf, (char *)fiber->top->ptrVal);
                strcat(buf, (char *)rhs.ptrVal);
                fiber->top->ptrVal = (int64_t)buf;
                break;
            }

            case TOK_EQEQ:      fiber->top->intVal = strcmp((char *)fiber->top->ptrVal, (char *)rhs.ptrVal) == 0; break;
            case TOK_NOTEQ:     fiber->top->intVal = strcmp((char *)fiber->top->ptrVal, (char *)rhs.ptrVal) != 0; break;
            case TOK_GREATER:   fiber->top->intVal = strcmp((char *)fiber->top->ptrVal, (char *)rhs.ptrVal)  > 0; break;
            case TOK_LESS:      fiber->top->intVal = strcmp((char *)fiber->top->ptrVal, (char *)rhs.ptrVal)  < 0; break;
            case TOK_GREATEREQ: fiber->top->intVal = strcmp((char *)fiber->top->ptrVal, (char *)rhs.ptrVal) >= 0; break;
            case TOK_LESSEQ:    fiber->top->intVal = strcmp((char *)fiber->top->ptrVal, (char *)rhs.ptrVal) <= 0; break;

            default:            error->handlerRuntime(error->context, "Illegal instruction"); return;
        }
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
                    error->handlerRuntime(error->context, "Division by zero");
                fiber->top->realVal /= rhs.realVal;
                break;
            }

            case TOK_EQEQ:      fiber->top->intVal = fiber->top->realVal == rhs.realVal; break;
            case TOK_NOTEQ:     fiber->top->intVal = fiber->top->realVal != rhs.realVal; break;
            case TOK_GREATER:   fiber->top->intVal = fiber->top->realVal >  rhs.realVal; break;
            case TOK_LESS:      fiber->top->intVal = fiber->top->realVal <  rhs.realVal; break;
            case TOK_GREATEREQ: fiber->top->intVal = fiber->top->realVal >= rhs.realVal; break;
            case TOK_LESSEQ:    fiber->top->intVal = fiber->top->realVal <= rhs.realVal; break;

            default:            error->handlerRuntime(error->context, "Illegal instruction"); return;
        }
    else if (fiber->code[fiber->ip].typeKind == TYPE_UINT)
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  fiber->top->uintVal += rhs.uintVal; break;
            case TOK_MINUS: fiber->top->uintVal -= rhs.uintVal; break;
            case TOK_MUL:   fiber->top->uintVal *= rhs.uintVal; break;
            case TOK_DIV:
            {
                if (rhs.uintVal == 0)
                    error->handlerRuntime(error->context, "Division by zero");
                fiber->top->uintVal /= rhs.uintVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.uintVal == 0)
                    error->handlerRuntime(error->context, "Division by zero");
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

            default:            error->handlerRuntime(error->context, "Illegal instruction"); return;
        }
    else  // All ordinal types except TYPE_UINT
        switch (fiber->code[fiber->ip].tokKind)
        {
            case TOK_PLUS:  fiber->top->intVal += rhs.intVal; break;
            case TOK_MINUS: fiber->top->intVal -= rhs.intVal; break;
            case TOK_MUL:   fiber->top->intVal *= rhs.intVal; break;
            case TOK_DIV:
            {
                if (rhs.intVal == 0)
                    error->handlerRuntime(error->context, "Division by zero");
                fiber->top->intVal /= rhs.intVal;
                break;
            }
            case TOK_MOD:
            {
                if (rhs.intVal == 0)
                    error->handlerRuntime(error->context, "Division by zero");
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

            default:            error->handlerRuntime(error->context, "Illegal instruction"); return;
        }

    fiber->ip++;
}


static void doGetArrayPtr(Fiber *fiber, Error *error)
{
    int itemSize = fiber->code[fiber->ip].operand.intVal;
    int len      = (fiber->top++)->intVal;
    int index    = (fiber->top++)->intVal;

    if (!fiber->top->ptrVal)
        error->handlerRuntime(error->context, "Array or string is null");

    // For strings, negative length means that the actual string length is to be used
    if (len < 0)
        len = strlen((char *)fiber->top->ptrVal);

    if (index < 0 || index > len - 1)
        error->handlerRuntime(error->context, "Index %d is out of range 0...%d", index, len - 1);

    fiber->top->ptrVal += itemSize * index;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doGetDynArrayPtr(Fiber *fiber, Error *error)
{
    int index       = (fiber->top++)->intVal;
    DynArray *array = (DynArray *)(fiber->top++)->ptrVal;

    if (!array || !array->data)
        error->handlerRuntime(error->context, "Dynamic array is null");

    int itemSize    = array->itemSize;
    int len         = array->len;

    if (index < 0 || index > len - 1)
        error->handlerRuntime(error->context, "Index %d is out of range 0...%d", index, len - 1);

    (--fiber->top)->ptrVal = (int64_t)((char *)array->data + itemSize * index);

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doGetFieldPtr(Fiber *fiber, Error *error)
{
    int fieldOffset = fiber->code[fiber->ip].operand.intVal;

    if (!fiber->top->ptrVal)
        error->handlerRuntime(error->context, "Array or structure is null");

    fiber->top->ptrVal += fieldOffset;

    if (fiber->code[fiber->ip].inlineOpcode == OP_DEREF)
        doBasicDeref(fiber->top, fiber->code[fiber->ip].typeKind, error);

    fiber->ip++;
}


static void doAssertType(Fiber *fiber)
{
    void *interface  = (void *)(fiber->top++)->ptrVal;
    Type *type       = (Type *)fiber->code[fiber->ip].operand.ptrVal;

    // Interface layout: __self, __selftype, methods
    void *__self     = *(void **)interface;
    Type *__selftype = *(Type **)((char *)interface + sizeof(__self));

    (--fiber->top)->ptrVal = (int64_t)((__selftype && typeEquivalent(type, __selftype)) ? __self : NULL);
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


static void doCall(Fiber *fiber, Error *error)
{
    // All calls are indirect, entry point address is below the parameters
    int paramSlots = fiber->code[fiber->ip].operand.intVal;
    int entryOffset = (fiber->top + paramSlots)->intVal;

    if (entryOffset == 0)
        error->handlerRuntime(error->context, "Called function is not defined");

    // Push return address and go to the entry point
    (--fiber->top)->intVal = fiber->ip + 1;
    fiber->ip = entryOffset;
}


static void doCallExtern(Fiber *fiber)
{
    ExternFunc fn = (ExternFunc)fiber->code[fiber->ip].operand.ptrVal;
    fn(fiber->top + 5, &fiber->reg[VM_REG_RESULT]);       // + 5 for saved I/O registers, old base pointer and return address
    fiber->ip++;
}


static void doCallBuiltin(Fiber *fiber, Fiber **newFiber, HeapPages *pages, Error *error)
{
    BuiltinFunc builtin = fiber->code[fiber->ip].operand.builtinVal;
    TypeKind typeKind   = fiber->code[fiber->ip].typeKind;

    switch (builtin)
    {
        // I/O
        case BUILTIN_PRINTF:        doBuiltinPrintf(fiber, true, false, error); break;
        case BUILTIN_FPRINTF:       doBuiltinPrintf(fiber, false, false, error); break;
        case BUILTIN_SPRINTF:       doBuiltinPrintf(fiber, false, true, error); break;
        case BUILTIN_SCANF:         doBuiltinScanf(fiber, pages, true, false, error); break;
        case BUILTIN_FSCANF:        doBuiltinScanf(fiber, pages, false, false, error); break;
        case BUILTIN_SSCANF:        doBuiltinScanf(fiber, pages, false, true, error); break;

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
        case BUILTIN_FABS:          fiber->top->realVal = fabs(fiber->top->realVal); break;
        case BUILTIN_SQRT:
        {
            if (fiber->top->realVal < 0)
                error->handlerRuntime(error->context, "sqrt() domain error");
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
                error->handlerRuntime(error->context, "atan2() domain error");
            fiber->top->realVal = atan2(y, x);
            break;
        }
        case BUILTIN_EXP:           fiber->top->realVal = exp (fiber->top->realVal); break;
        case BUILTIN_LOG:
        {
            if (fiber->top->realVal <= 0)
                error->handlerRuntime(error->context, "log() domain error");
            fiber->top->realVal = log(fiber->top->realVal);
            break;
        }

        // Memory
        case BUILTIN_NEW:           fiber->top->ptrVal = (int64_t)chunkAlloc(pages, fiber->top->intVal, error); break;
        case BUILTIN_MAKE:          doBuiltinMake(fiber, pages, error); break;
        case BUILTIN_MAKEFROM:      doBuiltinMakefrom(fiber, pages, error); break;
        case BUILTIN_APPEND:        doBuiltinAppend(fiber, pages, error); break;
        case BUILTIN_DELETE:        doBuiltinDelete(fiber, pages, error); break;
        case BUILTIN_LEN:           doBuiltinLen(fiber, error); break;
        case BUILTIN_SIZEOF:        error->handlerRuntime(error->context, "Illegal instruction"); return;       // Done at compile time
        case BUILTIN_SIZEOFSELF:    doBuiltinSizeofself(fiber, error); break;
        case BUILTIN_SELFHASPTR:    doBuiltinSelfhasptr(fiber, error); break;

        // Fibers
        case BUILTIN_FIBERSPAWN:    doBuiltinFiberspawn(fiber, pages, error); break;
        case BUILTIN_FIBERCALL:     doBuiltinFibercall(fiber, newFiber, pages, error); break;
        case BUILTIN_FIBERALIVE:    doBuiltinFiberalive(fiber, pages, error); break;

        // Misc
        case BUILTIN_REPR:          doBuiltinRepr(fiber, pages, error); break;
        case BUILTIN_ERROR:         error->handlerRuntime(error->context, (char *)fiber->top->ptrVal); return;
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
        *newFiber = (Fiber *)(fiber->top + 1)->ptrVal;
    }
    else
    {
        // For conventional function, remove parameters and entry point address from stack and go back
        fiber->top += fiber->code[fiber->ip].operand.intVal + 1;
        fiber->ip = returnOffset;
    }
}


static void doEnterFrame(Fiber *fiber, Error *error)
{
    // Push old stack frame base pointer, move new one to stack top, shift stack top by local variables' size
    int size = fiber->code[fiber->ip].operand.intVal;
    int slots = align(size, sizeof(Slot)) / sizeof(Slot);

    if (fiber->top - slots - fiber->stack < VM_MIN_FREE_STACK)
        error->handlerRuntime(error->context, "Stack overflow");

    (--fiber->top)->ptrVal = (int64_t)fiber->base;
    fiber->base = fiber->top;
    fiber->top -= slots;

    // Zero all local variables
    if (slots == 1)
        fiber->top->intVal = 0;
    else if (slots > 0)
        memset(fiber->top, 0, slots * sizeof(Slot));

    // Push I/O registers
    *(--fiber->top) = fiber->reg[VM_REG_IO_STREAM];
    *(--fiber->top) = fiber->reg[VM_REG_IO_FORMAT];
    *(--fiber->top) = fiber->reg[VM_REG_IO_COUNT];

    fiber->ip++;
}


static void doLeaveFrame(Fiber *fiber)
{
    // Pop I/O registers
    fiber->reg[VM_REG_IO_COUNT]  = *(fiber->top++);
    fiber->reg[VM_REG_IO_FORMAT] = *(fiber->top++);
    fiber->reg[VM_REG_IO_STREAM] = *(fiber->top++);

    // Restore stack top, pop old stack frame base pointer
    fiber->top = fiber->base;
    fiber->base = (Slot *)(fiber->top++)->ptrVal;

    fiber->ip++;
}


static void vmLoop(VM *vm)
{
    Fiber *fiber = vm->fiber;
    HeapPages *pages = &vm->pages;
    Error *error = vm->error;

    while (1)
    {
        if (fiber->top - fiber->stack < VM_MIN_FREE_STACK)
            error->handlerRuntime(error->context, "Stack overflow");

        switch (fiber->code[fiber->ip].opcode)
        {
            case OP_PUSH:                           doPush(fiber, error);                         break;
            case OP_PUSH_LOCAL_PTR:                 doPushLocalPtr(fiber, error);                 break;
            case OP_PUSH_REG:                       doPushReg(fiber);                             break;
            case OP_PUSH_STRUCT:                    doPushStruct(fiber, error);                   break;
            case OP_POP:                            doPop(fiber);                                 break;
            case OP_POP_REG:                        doPopReg(fiber);                              break;
            case OP_DUP:                            doDup(fiber);                                 break;
            case OP_SWAP:                           doSwap(fiber);                                break;
            case OP_DEREF:                          doDeref(fiber, error);                        break;
            case OP_ASSIGN:                         doAssign(fiber, error);                       break;
            case OP_CHANGE_REF_CNT:                 doChangeRefCnt(fiber, pages, error);          break;
            case OP_CHANGE_REF_CNT_ASSIGN:          doChangeRefCntAssign(fiber, pages, error);    break;
            case OP_UNARY:                          doUnary(fiber, error);                        break;
            case OP_BINARY:                         doBinary(fiber, pages, error);                break;
            case OP_GET_ARRAY_PTR:                  doGetArrayPtr(fiber, error);                  break;
            case OP_GET_DYNARRAY_PTR:               doGetDynArrayPtr(fiber, error);               break;
            case OP_GET_FIELD_PTR:                  doGetFieldPtr(fiber, error);                  break;
            case OP_ASSERT_TYPE:                    doAssertType(fiber);                          break;
            case OP_GOTO:                           doGoto(fiber);                                break;
            case OP_GOTO_IF:                        doGotoIf(fiber);                              break;
            case OP_CALL:                           doCall(fiber, error);                         break;
            case OP_CALL_EXTERN:                    doCallExtern(fiber);                          break;
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
            case OP_ENTER_FRAME:                    doEnterFrame(fiber, error);                   break;
            case OP_LEAVE_FRAME:                    doLeaveFrame(fiber);                          break;
            case OP_HALT:                           return;

            default: error->handlerRuntime(error->context, "Illegal instruction"); return;
        } // switch
    }
}


void vmRun(VM *vm, int entryOffset, int numParamSlots, Slot *params, Slot *result)
{
    if (entryOffset < 0)
        vm->error->handlerRuntime(vm->error->context, "Called function is not defined");

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
        *result = vm->fiber->reg[VM_REG_RESULT];
}


int vmAsm(int ip, Instruction *instr, char *buf)
{
    char opcodeBuf[DEFAULT_STR_LEN + 1];
    sprintf(opcodeBuf, "%s%s", instr->inlineOpcode == OP_SWAP ? "SWAP; " : "", opcodeSpelling[instr->opcode]);
    int chars = sprintf(buf, "%09d %6d %28s", ip, instr->debug.line, opcodeBuf);

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
                chars += sprintf(buf + chars, " %p", (void *)instr->operand.ptrVal);
            else
                chars += sprintf(buf + chars, " %lld", (long long int)instr->operand.intVal);
            break;
        }
        case OP_PUSH_LOCAL_PTR:
        case OP_PUSH_REG:
        case OP_PUSH_STRUCT:
        case OP_POP_REG:
        case OP_ASSIGN:
        case OP_BINARY:
        case OP_GET_ARRAY_PTR:
        case OP_GET_FIELD_PTR:
        case OP_GOTO:
        case OP_GOTO_IF:
        case OP_CALL:
        case OP_RETURN:
        case OP_ENTER_FRAME:            chars += sprintf(buf + chars, " %lld", (long long int)instr->operand.intVal); break;
        case OP_CALL_EXTERN:            chars += sprintf(buf + chars, " %p",   (void *)instr->operand.ptrVal); break;
        case OP_CALL_BUILTIN:           chars += sprintf(buf + chars, " %s",   builtinSpelling[instr->operand.builtinVal]); break;
        case OP_CHANGE_REF_CNT:
        case OP_CHANGE_REF_CNT_ASSIGN:
        case OP_ASSERT_TYPE:
        {
            char typeBuf[DEFAULT_STR_LEN + 1];
            chars += sprintf(buf + chars, " %s", typeSpelling((Type *)instr->operand.ptrVal, typeBuf));
            break;
        }
        default: break;
    }

    if (instr->inlineOpcode == OP_DEREF)
        chars += sprintf(buf + chars, "; DEREF");

    else if (instr->inlineOpcode == OP_POP)
        chars += sprintf(buf + chars, "; POP");

    return chars;
}
