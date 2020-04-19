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
    "UNARY",
    "BINARY",
    "GET_ARRAY_PTR",
    "GOTO",
    "GOTO_IF",
    "CALL",
    "CALL_BUILTIN",
    "RETURN",
    "ENTER_FRAME",
    "LEAVE_FRAME",
    "HALT"
};


void vmInit(VM *vm, Instruction *code, int stackSize, ErrorFunc error)
{
    vm->fiber.code = code;
    vm->fiber.ip = 0;

    vm->fiber.stack = malloc(stackSize * sizeof(Slot));
    vm->fiber.top = vm->fiber.base = vm->fiber.stack + stackSize - 1;
    vm->error = error;
}


void vmFree(VM *vm)
{
    free(vm->fiber.stack);
}


static void doConvertFormatStringToC(char *formatC, char **format, ErrorFunc error) // "{d}" -> "%d"
{
    bool leftBraceFound = false, rightBraceFound = false;
    while (**format)
    {
        if (**format == '{')
        {
            *formatC = '%';
            leftBraceFound = true;
            (*format)++;
            formatC++;
        }
        else if (**format == '}')
        {
            if (!leftBraceFound)
            {
                error("Illegal format string");
                return;
            }
            rightBraceFound = true;
            (*format)++;
            break;
        }
        else
            *formatC++ = *(*format)++;
    }
    *formatC = 0;

    if (leftBraceFound && !rightBraceFound)
    {
        error("Illegal format string");
        return;
    }
}


static void doFprintf(Fiber *fiber, bool console, ErrorFunc error)
{
    FILE *file    = console ? stdout : fiber->reg[VM_IO_FILE_REG].ptrVal;
    char *format  = fiber->reg[VM_IO_FORMAT_REG].ptrVal;

    char *formatC = malloc(strlen(format) + 1);
    doConvertFormatStringToC(formatC, &format, error);

    // Proxy to C fprintf()
    if (fiber->code[fiber->ip].typeKind == TYPE_VOID)
        fprintf(file, formatC);
    else if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
        fprintf(file, formatC, fiber->top->realVal);
    else
        fprintf(file, formatC, fiber->top->intVal);

    fiber->reg[VM_IO_FORMAT_REG].ptrVal = format;
    free(formatC);
}


static void doFscanf(Fiber *fiber, bool console, ErrorFunc error)
{
    FILE *file    = console ? stdin : fiber->reg[VM_IO_FILE_REG].ptrVal;
    char *format  = fiber->reg[VM_IO_FORMAT_REG].ptrVal;

    char *formatC = malloc(strlen(format) + 1);
    doConvertFormatStringToC(formatC, &format, error);

    // Proxy to C fprintf()
    if (fiber->code[fiber->ip].typeKind == TYPE_VOID)
        fscanf(file, formatC);
    else
        fscanf(file, formatC, *fiber->top);

    fiber->reg[VM_IO_FORMAT_REG].ptrVal = format;
    free(formatC);
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


static void doPushStruct(Fiber *fiber)
{
    void *src = (fiber->top++)->ptrVal;
    int size  = fiber->code[fiber->ip].operand.intVal;

    fiber->top -= align(size, sizeof(Slot)) / sizeof(Slot);
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
        case TYPE_INT8:   fiber->top->intVal  = *(int8_t   *)ptr; break;
        case TYPE_INT16:  fiber->top->intVal  = *(int16_t  *)ptr; break;
        case TYPE_INT32:  fiber->top->intVal  = *(int32_t  *)ptr; break;
        case TYPE_INT:    fiber->top->intVal  = *(int64_t  *)ptr; break;
        case TYPE_UINT8:  fiber->top->intVal  = *(uint8_t  *)ptr; break;
        case TYPE_UINT16: fiber->top->intVal  = *(uint16_t *)ptr; break;
        case TYPE_UINT32: fiber->top->intVal  = *(uint32_t *)ptr; break;
        case TYPE_BOOL:   fiber->top->intVal  = *(bool     *)ptr; break;
        case TYPE_CHAR:   fiber->top->intVal  = *(char     *)ptr; break;
        case TYPE_REAL32: fiber->top->realVal = *(float    *)ptr; break;
        case TYPE_REAL:   fiber->top->realVal = *(double   *)ptr; break;
        case TYPE_PTR:    fiber->top->ptrVal  = *(void *   *)ptr; break;
        // Structured types are represented by pointers, so they are not dereferenced
        default:          fiber->top->intVal =   (int64_t   )ptr; break;
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
        {
            int size = fiber->code[fiber->ip].operand.intVal;
            memcpy(lhs, rhs.ptrVal, size);
            break;
        }
        default:          error("Illegal type"); return;
    }
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

    if (fiber->code[fiber->ip].typeKind == TYPE_REAL || fiber->code[fiber->ip].typeKind == TYPE_REAL32)
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


static void doGetArrayPtr(Fiber *fiber)
{
    int size = fiber->code[fiber->ip].operand.intVal;
    int index = (fiber->top++)->intVal;
    fiber->top->ptrVal += size * index;
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


static void doCallBuiltin(Fiber *fiber, ErrorFunc error)
{
    switch (fiber->code[fiber->ip].operand.builtinVal)
    {
        case BUILTIN_PRINTF:    doFprintf(fiber, true, error); break;
        case BUILTIN_FPRINTF:   doFprintf(fiber, false, error); break;
        case BUILTIN_SCANF:     doFscanf(fiber, true, error); break;
        case BUILTIN_FSCANF:    doFscanf(fiber, false, error); break;
        case BUILTIN_REAL:      fiber->top->realVal = fiber->top->intVal; break;
        case BUILTIN_REAL_LHS:  (fiber->top + 1)->realVal = (fiber->top + 1)->intVal; break;
        case BUILTIN_ROUND:     fiber->top->intVal = (int64_t)round(fiber->top->realVal); break;
        case BUILTIN_TRUNC:     fiber->top->intVal = (int64_t)trunc(fiber->top->realVal); break;
        case BUILTIN_FABS:      fiber->top->realVal = fabs(fiber->top->realVal); break;
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
        case BUILTIN_SIN:       fiber->top->realVal = sin (fiber->top->realVal); break;
        case BUILTIN_COS:       fiber->top->realVal = cos (fiber->top->realVal); break;
        case BUILTIN_ATAN:      fiber->top->realVal = atan(fiber->top->realVal); break;
        case BUILTIN_EXP:       fiber->top->realVal = exp (fiber->top->realVal); break;
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
    }
    fiber->ip++;
}


static void doReturn(Fiber *fiber)
{
    // Pop return address, remove parameters and entry point address from stack and go back
    int returnOffset = (fiber->top++)->intVal;
    fiber->top += fiber->code[fiber->ip].operand.intVal + 1;
    fiber->ip = returnOffset;
}


static void doEnterFrame(Fiber *fiber)
{
    // Push old stack frame base pointer, move new one to stack top, shift stack top by local variables' size
    (--fiber->top)->ptrVal = fiber->base;
    fiber->base = fiber->top;
    fiber->top = (Slot *)((int8_t *)(fiber->top) - fiber->code[fiber->ip].operand.intVal);

    // Push I/O registers
    *(--fiber->top) = fiber->reg[VM_IO_FILE_REG];
    *(--fiber->top) = fiber->reg[VM_IO_FORMAT_REG];

    fiber->ip++;
}


static void doLeaveFrame(Fiber *fiber)
{
    // Pop I/O registers
    fiber->reg[VM_IO_FORMAT_REG] = *(fiber->top++);
    fiber->reg[VM_IO_FILE_REG]   = *(fiber->top++);

    // Restore stack top, pop old stack frame base pointer
    fiber->top = fiber->base;
    fiber->base = (fiber->top++)->ptrVal;

    fiber->ip++;
}


void fiberStep(Fiber *fiber, ErrorFunc error)
{
    switch (fiber->code[fiber->ip].opcode)
    {
        case OP_PUSH:           doPush(fiber);              break;
        case OP_PUSH_LOCAL_PTR: doPushLocalPtr(fiber);      break;
        case OP_PUSH_REG:       doPushReg(fiber);           break;
        case OP_PUSH_STRUCT:    doPushStruct(fiber);        break;
        case OP_POP:            doPop(fiber);               break;
        case OP_POP_REG:        doPopReg(fiber);            break;
        case OP_DUP:            doDup(fiber);               break;
        case OP_SWAP:           doSwap(fiber);              break;
        case OP_DEREF:          doDeref(fiber, error);      break;
        case OP_ASSIGN:         doAssign(fiber, error);     break;
        case OP_UNARY:          doUnary(fiber, error);      break;
        case OP_BINARY:         doBinary(fiber, error);     break;
        case OP_GET_ARRAY_PTR:  doGetArrayPtr(fiber);       break;
        case OP_GOTO:           doGoto(fiber);              break;
        case OP_GOTO_IF:        doGotoIf(fiber);            break;
        case OP_CALL:           doCall(fiber);              break;
        case OP_CALL_BUILTIN:   doCallBuiltin(fiber, error);break;
        case OP_RETURN:         doReturn(fiber);            break;
        case OP_ENTER_FRAME:    doEnterFrame(fiber);        break;
        case OP_LEAVE_FRAME:    doLeaveFrame(fiber);        break;

        default: error("Illegal instruction"); return;
    } // switch
}


void vmRun(VM *vm)
{
    while (vm->fiber.code[vm->fiber.ip].opcode != OP_HALT)
    {
        //char buf[256];
        //printf("%s\n", vmAsm(&vm->fiber.code[vm->fiber.ip], buf));
        fiberStep(&vm->fiber, vm->error);
    }
}


char *vmAsm(Instruction *instr, char *buf)
{
    sprintf(buf, "%16s %10s %8s %08X", spelling[instr->opcode], lexSpelling(instr->tokKind),
                                       typeKindSpelling(instr->typeKind), (int)instr->operand.intVal);
    return buf;
}
