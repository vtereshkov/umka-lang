#ifndef UMKA_VM_H_INCLUDED
#define UMKA_VM_H_INCLUDED

#include "umka_common.h"
#include "umka_types.h"


typedef enum
{
    REG_RESULT,
    REG_SELF,
    REG_HEAP_COPY,
    REG_SWITCH_EXPR,
    REG_EXPR_LIST,

    NUM_REGS
} RegisterIndex;


enum    // Memory manager settings
{
    MEM_MIN_FREE_STACK    = 1024,                   // Slots
    MEM_MIN_FREE_HEAP     = 1024,                   // Bytes
    MEM_MIN_HEAP_CHUNK    = 64,                     // Bytes
    MEM_MIN_HEAP_PAGE     = 1024 * 1024,            // Bytes
    MEM_MIN_GC_THRESHOLD  = 16 * 1024 * 1024        // Bytes
};


enum
{
    MAX_VM_CALL_NESTING = 64
};


enum
{
    JUMP_TO_CLEANUP = 0
};


enum    // Special values for return addresses
{
    RETURN_FROM_VM    = -2,                      // Used instead of return address in functions called by umkaCall()
    RETURN_FROM_FIBER = -1                       // Used instead of return address in fiber function calls
};


enum    // Runtime error codes
{
    ERR_RUNTIME = -1
};


typedef enum
{
    OP_NOP,
    OP_PUSH,
    OP_PUSH_GLOBAL,
    OP_PUSH_ZERO,
    OP_PUSH_LOCAL_PTR,
    OP_PUSH_LOCAL_PTR_ZERO,
    OP_PUSH_LOCAL,
    OP_PUSH_REG,
    OP_PUSH_UPVALUE,
    OP_POP,
    OP_POP_REG,
    OP_DUP,
    OP_SWAP,
    OP_ZERO,
    OP_DEREF,
    OP_ASSIGN,
    OP_SWAP_ASSIGN,
    OP_ASSIGN_PARAM,
    OP_UNARY,
    OP_BINARY,
    OP_GET_ARRAY_PTR,
    OP_GET_ARRAY,
    OP_GET_DYNARRAY_PTR,
    OP_GET_DYNARRAY,
    OP_GET_MAP_PTR,
    OP_GET_MAP,
    OP_GET_FIELD_PTR,
    OP_GET_FIELD,
    OP_ASSERT_TYPE,
    OP_ASSERT_RANGE,
    OP_GOTO,
    OP_GOTO_IF,
    OP_GOTO_IF_NOT,
    OP_CALL,
    OP_CALL_INDIRECT,
    OP_CALL_EXTERN,
    OP_CALL_BUILTIN,
    OP_RETURN,
    OP_ENTER_FRAME,
    OP_LEAVE_FRAME,
    OP_HALT
} Opcode;


typedef union               // Extended version of UmkaStackSlot
{
    int64_t intVal;         // For all ordinal types except uint
    uint64_t uintVal;
    int32_t int32Val[2];
    void *ptrVal;
    double realVal;         // For all real types
    BuiltinFunc builtinVal;
    UmkaStackSlot apiSlot;  // For compatibility with C API
} Slot;


typedef struct
{
    Opcode opcode;
    TokenKind tokKind;
    TypeKind typeKind;
    const Type *type;
    Slot operand;
} Instruction;


typedef struct
{
    void *ptr;
    const Type *type;           // If NULL, ptr is a fiber whose stack is to be scanned conservatively
} GCCandidate;


typedef struct
{
    GCCandidate *stack;
    int top, capacity;
    Storage *storage;
} GCCandidates;


typedef enum
{
    CHUNK_DATA,             // Data of the type stored in the chunk header, if any
    CHUNK_DYNARRAY_DATA,    // Dynamic array dimensions followed by the items
    CHUNK_FIBER,            // Fiber
    CHUNK_STACK             // Fiber stack
} ChunkKind;


typedef struct tagHeapChunk
{
    struct tagHeapChunk *nextFree;
    int size;
    unsigned char kind;         // ChunkKind
    bool allocated, marked;
    const Type *type;           // Optional type of the data stored in the chunk
    UmkaExternFunc onFree;      // Optional callback called when the chunk is collected
    int64_t data[];
} HeapChunk;


typedef struct tagHeapPage
{
    int numChunks, numOccupiedChunks, numAllocatedChunks, chunkSize;
    struct tagHeapPage *prev, *next;
    HeapChunk *firstFree;
    char *end;
    int64_t data[];
} HeapPage;


typedef struct
{
    HeapPage *first, *firstRecycled, *lastAccessed;
    char *lowest, *highest;
    int64_t totalSize, occupiedSize, gcThreshold;
    bool gcRequested;
    GCCandidates markCandidates, escapeSuspects, roots;
    Error *error;
} HeapPages;


typedef struct tagFiber
{
    // Must have 8 byte alignment
    const Instruction *code;
    int ip;
    Slot *stack, *top, *base;
    int stackSize;
    Slot reg[NUM_REGS];
    struct tagFiber *parent;
    const DebugInfo *debugPerInstr;
    struct tagVM *vm;
    bool alive;
    bool fileSystemEnabled;
} Fiber;


typedef struct tagIdents Idents;


typedef struct tagVM
{
    Fiber *fiber, *mainFiber;
    HeapPages pages;
    const Idents *idents;
    UmkaHookFunc hooks[UMKA_NUM_HOOKS];
    bool terminatedNormally;
    int callNesting;
    Storage *storage;
    Error *error;
} VM;


void vmInit                     (VM *vm, Storage *storage, const Idents *idents, int stackSize, bool fileSystemEnabled, Error *error);
void vmFree                     (VM *vm);
void vmReset                    (VM *vm, const Instruction *code, const DebugInfo *debugPerInstr);
void vmCall                     (VM *vm, UmkaFuncContext *fn);
void vmCleanup                  (VM *vm);
bool vmAlive                    (VM *vm);
void vmKill                     (VM *vm);
int vmAsm                       (int ip, const Instruction *code, const DebugInfo *debugPerInstr, const Idents *idents, char *buf, int size);
bool vmUnwindCallStack          (VM *vm, const Slot **base, int *ip);
void vmSetHook                  (VM *vm, UmkaHookEvent event, UmkaHookFunc hook);
void *vmAllocData               (VM *vm, int size, UmkaExternFunc onFree);
void *vmGetMapNodeData          (VM *vm, Map *map, Slot key);
char *vmMakeStr                 (VM *vm, const char *str);
void vmMakeDynArray             (VM *vm, DynArray *array, const Type *type, int len);
void *vmMakeStruct              (VM *vm, const Type *type);
int64_t vmGetMemUsage           (VM *vm);
const char *vmBuiltinSpelling   (BuiltinFunc builtin);


static inline const StackFrameLayout **vmGetStackFrameLayout(UmkaStackSlot *params)
{
    return (const StackFrameLayout **)&params[-4].ptrVal;     // For -4, see the stack layout diagram in umka_vm.c
}

#endif // UMKA_VM_H_INCLUDED
