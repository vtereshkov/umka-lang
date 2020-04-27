#ifndef UMKA_VM_H_INCLUDED
#define UMKA_VM_H_INCLUDED

#include "umka_common.h"
#include "umka_lexer.h"
#include "umka_types.h"


enum
{
    VM_NUM_REGS         = 64,

    // General-purpose registers
    VM_RESULT_REG_0     = 0,
    VM_SELF_REG         = 31,
    VM_COMMON_REG_0     = 32,
    VM_COMMON_REG_1     = VM_COMMON_REG_0 + 1,

    // Registers for special use by printf() / scanf()
    VM_IO_STREAM_REG    = VM_NUM_REGS - 3,
    VM_IO_FORMAT_REG    = VM_NUM_REGS - 2,
    VM_IO_COUNT_REG     = VM_NUM_REGS - 1,

    VM_MIN_STACK_SIZE   = 1024  // Slots
};


typedef enum
{
    OP_NOP,
    OP_PUSH,
    OP_PUSH_LOCAL_PTR,
    OP_PUSH_REG,
    OP_PUSH_STRUCT,
    OP_POP,
    OP_POP_REG,
    OP_DUP,
    OP_SWAP,
    OP_DEREF,
    OP_ASSIGN,
    OP_ASSIGN_OFS,
    OP_UNARY,
    OP_BINARY,
    OP_GET_ARRAY_PTR,
    OP_GOTO,
    OP_GOTO_IF,
    OP_CALL,
    OP_CALL_EXTERN,
    OP_CALL_BUILTIN,
    OP_RETURN,
    OP_ENTER_FRAME,
    OP_LEAVE_FRAME,
    OP_HALT
} Opcode;


typedef enum
{
    BUILTIN_PRINTF,
    BUILTIN_FPRINTF,
    BUILTIN_SPRINTF,
    BUILTIN_SCANF,
    BUILTIN_FSCANF,
    BUILTIN_SSCANF,
    BUILTIN_REAL,           // Integer to real at stack top (right operand)
    BUILTIN_REAL_LHS,       // Integer to real at stack top + 1 (left operand)
    BUILTIN_ROUND,
    BUILTIN_TRUNC,
    BUILTIN_FABS,
    BUILTIN_SQRT,
    BUILTIN_SIN,
    BUILTIN_COS,
    BUILTIN_ATAN,
    BUILTIN_EXP,
    BUILTIN_LOG,
    BUILTIN_SIZEOF,
} BuiltinFunc;


typedef union
{
    int64_t intVal;
    void *ptrVal;
    double realVal;
    BuiltinFunc builtinVal;
} Slot;


typedef struct
{
    Opcode opcode;
    TokenKind tokKind;  // Unary/binary operation token
    TypeKind typeKind;  // Slot type kind
    Slot operand;
} Instruction;


typedef struct
{
    Instruction *code;
    int ip;
    Slot *stack, *top, *base;
    Slot reg[VM_NUM_REGS];
} Fiber;


typedef void (*ExternFunc)(Slot *params, Slot *result);


typedef struct
{
    Fiber fiber;
    ErrorFunc error;
} VM;


void vmInit(VM *vm, Instruction *code, int stackSize /* slots */, ErrorFunc error);
void vmFree(VM *vm);
void vmRun (VM *vm);
char *vmAsm(Instruction *instr, char *buf);

#endif // UMKA_VM_H_INCLUDED
