#ifndef UMKA_VM_H_INCLUDED
#define UMKA_VM_H_INCLUDED

#include "umka_common.h"
#include "umka_lexer.h"
#include "umka_types.h"


enum
{
    VM_NUM_REGS          = 64,

    // General-purpose registers
    VM_RESULT_REG_0      = 0,
    VM_DYNARRAY_REG      = 30,
    VM_SELF_REG          = 31,
    VM_COMMON_REG_0      = 32,
    VM_COMMON_REG_1      = VM_COMMON_REG_0 + 1,

    // Registers for special use by printf() / scanf()
    VM_IO_STREAM_REG     = VM_NUM_REGS - 3,
    VM_IO_FORMAT_REG     = VM_NUM_REGS - 2,
    VM_IO_COUNT_REG      = VM_NUM_REGS - 1,

    VM_MIN_FREE_STACK    = 1024,                    // Slots
    VM_MIN_HEAP_PAGE     = 1024 * 1024,             // Bytes

    VM_HEAP_CHUNK_MAGIC  = 0x1234567887654321LL,

    VM_FIBER_KILL_SIGNAL = -1                       // Used instead of return address in fiber function calls
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
    OP_SWAP_ASSIGN,
    OP_ASSIGN_OFS,
    OP_SWAP_ASSIGN_OFS,
    OP_CHANGE_REF_CNT,
    OP_CHANGE_REF_CNT_ASSIGN,
    OP_UNARY,
    OP_BINARY,
    OP_GET_ARRAY_PTR,
    OP_GET_DYNARRAY_PTR,
    OP_GET_FIELD_PTR,
    OP_ASSERT_TYPE,
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
    // I/O
    BUILTIN_PRINTF,
    BUILTIN_FPRINTF,
    BUILTIN_SPRINTF,
    BUILTIN_SCANF,
    BUILTIN_FSCANF,
    BUILTIN_SSCANF,

    // Math
    BUILTIN_REAL,           // Integer to real at stack top (right operand)
    BUILTIN_REAL_LHS,       // Integer to real at stack top + 1 (left operand) - implicit calls only
    BUILTIN_ROUND,
    BUILTIN_TRUNC,
    BUILTIN_FABS,
    BUILTIN_SQRT,
    BUILTIN_SIN,
    BUILTIN_COS,
    BUILTIN_ATAN,
    BUILTIN_EXP,
    BUILTIN_LOG,

    // Memory
    BUILTIN_NEW,
    BUILTIN_MAKE,
    BUILTIN_MAKEFROM,       // Array to dynamic array - implicit calls only
    BUILTIN_APPEND,
    BUILTIN_DELETE,
    BUILTIN_LEN,
    BUILTIN_SIZEOF,

    // Fibers
    BUILTIN_FIBERSPAWN,
    BUILTIN_FIBERCALL,
    BUILTIN_FIBERALIVE,

    // Misc
    BUILTIN_ERROR
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
    TokenKind tokKind;              // Unary/binary operation token
    TypeKind typeKind;              // Slot type kind
    Slot operand;
    bool inlineDeref;               // Compound instruction flag (PUSH + DEREF etc.)
    bool inlinePop;                 // Compound instruction flag (CHANGE_REF_CNT + POP)
    DebugInfo debug;
} Instruction;


typedef struct
{
    Instruction *code;
    int ip;
    Slot *stack, *top, *base;
    int stackSize;
    Slot reg[VM_NUM_REGS];
    bool alive;
} Fiber;


typedef struct tagHeapPage
{
    void *ptr;
    int size, occupied;
    int refCnt;
    struct tagHeapPage *prev, *next;
} HeapPage;


typedef struct
{
    HeapPage *first, *last;
} HeapPages;


typedef struct
{
    int64_t magic;
    int refCnt;
    int size;
} HeapChunkHeader;


typedef void (*ExternFunc)(Slot *params, Slot *result);


typedef struct
{
    Fiber *fiber;
    HeapPages pages;
    ErrorFunc error;
} VM;


void vmInit(VM *vm, int stackSize /* slots */, ErrorFunc error);
void vmFree(VM *vm);
void vmReset(VM *vm, Instruction *code);
void vmRun(VM *vm, int entryOffset, int numParamSlots, Slot *params, Slot *result);
int vmAsm(int ip, Instruction *instr, char *buf);

#endif // UMKA_VM_H_INCLUDED
