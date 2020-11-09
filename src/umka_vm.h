#ifndef UMKA_VM_H_INCLUDED
#define UMKA_VM_H_INCLUDED

#include "umka_common.h"
#include "umka_lexer.h"
#include "umka_types.h"


enum
{
    VM_NUM_REGS          = 16,

    // General-purpose registers
    VM_REG_RESULT        = 0,
    VM_REG_SELF          = 1,
    VM_REG_COMMON_0      = 2,
    VM_REG_COMMON_1      = VM_REG_COMMON_0 + 1,
    VM_REG_COMMON_2      = VM_REG_COMMON_0 + 2,
    VM_REG_COMMON_3      = VM_REG_COMMON_0 + 3,

    // Registers for special use by printf() / scanf()
    VM_REG_IO_STREAM     = VM_NUM_REGS - 3,
    VM_REG_IO_FORMAT     = VM_NUM_REGS - 2,
    VM_REG_IO_COUNT      = VM_NUM_REGS - 1,

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
    BUILTIN_ATAN2,
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
    BUILTIN_SIZEOFSELF,
    BUILTIN_SELFHASPTR,

    // Fibers
    BUILTIN_FIBERSPAWN,
    BUILTIN_FIBERCALL,
    BUILTIN_FIBERALIVE,

    // Misc
    BUILTIN_REPR,
    BUILTIN_ERROR
} BuiltinFunc;


typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    int64_t ptrVal;
    double realVal;
    BuiltinFunc builtinVal;
} Slot;


typedef struct
{
    Opcode opcode;
    Opcode inlineOpcode;            // Inlined instruction (DEREF, POP, SWAP): PUSH + DEREF, CHANGE_REF_CNT + POP, SWAP + ASSIGN etc.
    TokenKind tokKind;              // Unary/binary operation token
    TypeKind typeKind;              // Slot type kind
    Slot operand;
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
    Fiber *fiber, *mainFiber;
    HeapPages pages;
    Error *error;
} VM;


void vmInit(VM *vm, int stackSize /* slots */, Error *error);
void vmFree(VM *vm);
void vmReset(VM *vm, Instruction *code);
void vmRun(VM *vm, int entryOffset, int numParamSlots, Slot *params, Slot *result);
int vmAsm(int ip, Instruction *instr, char *buf);
const char *vmBuiltinSpelling(BuiltinFunc builtin);

#endif // UMKA_VM_H_INCLUDED
