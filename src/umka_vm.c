#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "umka_vm.h"


void vmInit(VM *vm, Instruction *code, ErrorFunc error)
{
    vm->code = vm->fiber.instr = code;
    vm->fiber.top = vm->fiber.base = vm->fiber.stack + VM_STACK_SIZE - 1;
    vm->error = error;
}


void vmFree(VM *vm)
{
}


static void doConvertFormatStringToC(char *formatC, char **format, ErrorFunc error) // "{%d}" -> "%d"
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
    if (fiber->instr->typeKind == TYPE_VOID)
        fprintf(file, formatC);
    else if (fiber->instr->typeKind == TYPE_REAL || fiber->instr->typeKind == TYPE_REAL32)
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
    if (fiber->instr->typeKind == TYPE_VOID)
        fscanf(file, formatC);
    else
        fscanf(file, formatC, *fiber->top);

    fiber->reg[VM_IO_FORMAT_REG].ptrVal = format;
    free(formatC);
}


static void doPush(Fiber *fiber)
{
    (--fiber->top)->intVal = fiber->instr->operand.intVal;
    fiber->instr++;
}


static void doPushLocalPtr(Fiber *fiber)
{
    // Local variable addresses are offsets (in bytes) from the stack frame base pointer
    (--fiber->top)->ptrVal = (int8_t *)fiber->base + fiber->instr->operand.intVal;
    fiber->instr++;
}


static void doPushReg(Fiber *fiber)
{
    (--fiber->top)->intVal = fiber->reg[fiber->instr->operand.intVal].intVal;
    fiber->instr++;
}


static void doPop(Fiber *fiber)
{
    fiber->top++;
    fiber->instr++;
}


static void doPopReg(Fiber *fiber)
{
    fiber->reg[fiber->instr->operand.intVal].intVal = (fiber->top++)->intVal;
    fiber->instr++;
}


static void doDup(Fiber *fiber)
{
    Slot val = *fiber->top;
    *(--fiber->top) = val;
    fiber->instr++;
}


static void doSwap(Fiber *fiber)
{
    Slot val = *fiber->top;
    *fiber->top = *(fiber->top + 1);
    *(fiber->top + 1) = val;
    fiber->instr++;
}


static void doDeref(Fiber *fiber)
{
    void *ptr = fiber->top->ptrVal;
    switch (fiber->instr->typeKind)
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
    fiber->instr++;
}


static void doAssign(Fiber *fiber, ErrorFunc error)
{
    Slot rhs = *fiber->top++;
    void *lhs = (fiber->top++)->ptrVal;
    switch (fiber->instr->typeKind)
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
        // Structured types are not assigned
        default:          error("Illegal type"); return;
    }
    fiber->instr++;
}


static void doUnary(Fiber *fiber, ErrorFunc error)
{
    if (fiber->instr->typeKind == TYPE_REAL || fiber->instr->typeKind == TYPE_REAL32)
        switch (fiber->instr->tokKind)
        {
            case TOK_MINUS: fiber->top->realVal = -fiber->top->realVal; break;
            default:        error("Illegal instruction"); return;
        }
    else
        switch (fiber->instr->tokKind)
        {
            case TOK_MINUS:      fiber->top->intVal = -fiber->top->intVal; break;
            case TOK_NOT:        fiber->top->intVal = !fiber->top->intVal; break;
            case TOK_XOR:        fiber->top->intVal = ~fiber->top->intVal; break;

            case TOK_PLUSPLUS:
            {
                void *ptr = (fiber->top++)->ptrVal;
                switch (fiber->instr->typeKind)
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
                switch (fiber->instr->typeKind)
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
    fiber->instr++;
}


static void doBinary(Fiber *fiber, ErrorFunc error)
{
    Slot rhs = *fiber->top++;

    if (fiber->instr->typeKind == TYPE_REAL || fiber->instr->typeKind == TYPE_REAL32)
        switch (fiber->instr->tokKind)
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
        switch (fiber->instr->tokKind)
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
    fiber->instr++;
}


static void doGetArrayPtr(Fiber *fiber)
{
    int size = fiber->instr->operand.intVal;
    int index = (fiber->top++)->intVal;
    fiber->top->ptrVal += size * index;
    fiber->instr++;
}


static void doGoto(Fiber *fiber)
{
    fiber->instr = fiber->instr->operand.ptrVal;
}


static void doGotoIf(Fiber *fiber)
{
    if ((fiber->top++)->intVal)
        fiber->instr = fiber->instr->operand.ptrVal;
    else
        fiber->instr++;
}


static void doCall(Fiber *fiber)
{
    // All calls are indirect, entry point address is below the parameters
    int numParams = fiber->instr->operand.intVal;
    Instruction *entryPtr = (fiber->top + numParams)->ptrVal;

    // Push return address and go to the entry point
    (--fiber->top)->ptrVal = fiber->instr + 1;
    fiber->instr = entryPtr;
}


static void doCallBuiltin(Fiber *fiber, ErrorFunc error)
{
    switch (fiber->instr->operand.builtinVal)
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
    fiber->instr++;
}


static void doReturn(Fiber *fiber)
{
    // Pop return address, remove parameters and entry point address from stack and go back
    Instruction *returnPtr = (fiber->top++)->ptrVal;
    fiber->top += fiber->instr->operand.intVal + 1;
    fiber->instr = returnPtr;
}


static void doEnterFrame(Fiber *fiber)
{
    // Push old stack frame base pointer, move new one to stack top, shift stack top by local variables' size
    (--fiber->top)->ptrVal = fiber->base;
    fiber->base = fiber->top;
    fiber->top = (Slot *)((int8_t *)(fiber->top) - fiber->instr->operand.intVal);

    // Push I/O registers
    *(--fiber->top) = fiber->reg[VM_IO_FILE_REG];
    *(--fiber->top) = fiber->reg[VM_IO_FORMAT_REG];

    fiber->instr++;
}


static void doLeaveFrame(Fiber *fiber)
{
    // Pop I/O registers
    fiber->reg[VM_IO_FORMAT_REG] = *(fiber->top++);
    fiber->reg[VM_IO_FILE_REG]   = *(fiber->top++);

    // Restore stack top, pop old stack frame base pointer
    fiber->top = fiber->base;
    fiber->base = (fiber->top++)->ptrVal;

    fiber->instr++;
}


void fiberStep(Fiber *fiber, ErrorFunc error)
{
    switch (fiber->instr->opcode)
    {
        case OP_PUSH:           doPush(fiber);              break;
        case OP_PUSH_LOCAL_PTR: doPushLocalPtr(fiber);      break;
        case OP_PUSH_REG:       doPushReg(fiber);           break;
        case OP_POP:            doPop(fiber);               break;
        case OP_POP_REG:        doPopReg(fiber);            break;
        case OP_DUP:            doDup(fiber);               break;
        case OP_SWAP:           doSwap(fiber);              break;
        case OP_DEREF:          doDeref(fiber);             break;
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
    while (vm->fiber.instr->opcode != OP_HALT)
        fiberStep(&vm->fiber, vm->error);
}
