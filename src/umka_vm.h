#ifndef UMKA_VM_H_INCLUDED
#define UMKA_VM_H_INCLUDED

#include "umka_common.h"
#include "umka_lexer.h"
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
    MEM_MIN_FREE_STACK = 1024,                   // Slots
    MEM_MIN_FREE_HEAP  = 1024,                   // Bytes
    MEM_MIN_HEAP_CHUNK = 64,                     // Bytes
    MEM_MIN_HEAP_PAGE  = 1024 * 1024,            // Bytes
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
    OP_ASSIGN_PARAM,
    OP_CHANGE_REF_CNT,
    OP_CHANGE_REF_CNT_GLOBAL,
    OP_CHANGE_REF_CNT_LOCAL,
    OP_CHANGE_REF_CNT_ASSIGN,
    OP_UNARY,
    OP_BINARY,
    OP_GET_ARRAY_PTR,
    OP_GET_DYNARRAY_PTR,
    OP_GET_MAP_PTR,
    OP_GET_FIELD_PTR,
    OP_ASSERT_TYPE,
    OP_ASSERT_RANGE,
    OP_WEAKEN_PTR,
    OP_STRENGTHEN_PTR,
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
    uint64_t weakPtrVal;    // For global pointers, stores the pointer. For heap pointers, stores the heap flag (bit 63), page ID (bits 32...62), offset within page (bits 0..31)
    double realVal;         // For all real types
    BuiltinFunc builtinVal;
    UmkaStackSlot apiSlot;  // For compatibility with C API
} Slot;


typedef struct
{
    Opcode opcode;
    Opcode inlineOpcode;         // Inlined instruction (DEREF, SWAP): PUSH + DEREF, SWAP + ASSIGN etc.
    TokenKind tokKind;           // Unary/binary operation token
    TypeKind typeKind;
    const Type *type;
    Slot operand;
} Instruction;


typedef struct tagHeapPage
{
    int id;
    int refCnt;
    int numChunks, numOccupiedChunks, numChunksWithOnFree, chunkSize;
    struct tagHeapPage *prev, *next;
    char *end;
    char data[];
} HeapPage;


typedef struct
{
    HeapPage *first;
    HeapPage *lastAccessed;
    char *lowest, *highest;
    int freeId;
    int64_t totalSize;
    struct tagFiber *fiber;
    Error *error;
} HeapPages;


typedef struct
{
    int refCnt;
    int size;
    const Type *type;           // Optional type for garbage collection
    UmkaExternFunc onFree;      // Optional callback called when ref count reaches zero
    int64_t ip;                 // Optional instruction pointer at which the chunk has been allocated
    bool isStack;
    bool reserved[7];
    char data[];
} HeapChunk;


typedef struct
{
    void *ptr;
    const Type *type;
    HeapPage *pageForDeferred;   // Mandatory for deferred ref count updates, NULL otherwise
} RefCntChangeCandidate;


typedef struct
{
    RefCntChangeCandidate *stack;
    int top, capacity;
    Storage *storage;
} RefCntChangeCandidates;


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
    RefCntChangeCandidates *refCntChangeCandidates;
    struct tagVM *vm;
    bool alive;
    bool fileSystemEnabled;
} Fiber;


typedef struct tagVM
{
    Fiber *fiber, *mainFiber;
    HeapPages pages;
    RefCntChangeCandidates refCntChangeCandidates;
    UmkaHookFunc hooks[UMKA_NUM_HOOKS];
    bool terminatedNormally;
    Storage *storage;
    Error *error;
} VM;


void vmInit                     (VM *vm, Storage *storage, int stackSize, bool fileSystemEnabled, Error *error);
void vmFree                     (VM *vm);
void vmReset                    (VM *vm, const Instruction *code, const DebugInfo *debugPerInstr);
void vmRun                      (VM *vm, UmkaFuncContext *fn);
bool vmAlive                    (VM *vm);
void vmKill                     (VM *vm);
int vmAsm                       (int ip, const Instruction *code, const DebugInfo *debugPerInstr, char *buf, int size);
bool vmUnwindCallStack          (VM *vm, Slot **base, int *ip);
void vmSetHook                  (VM *vm, UmkaHookEvent event, UmkaHookFunc hook);
void *vmAllocData               (VM *vm, int size, UmkaExternFunc onFree);
void vmIncRef                   (VM *vm, void *ptr, const Type *type);
void vmDecRef                   (VM *vm, void *ptr, const Type *type);
void *vmGetMapNodeData          (VM *vm, Map *map, Slot key);
char *vmMakeStr                 (VM *vm, const char *str);
void vmMakeDynArray             (VM *vm, DynArray *array, const Type *type, int len);
void *vmMakeStruct              (VM *vm, const Type *type);
int64_t vmGetMemUsage           (VM *vm);
const char *vmBuiltinSpelling   (BuiltinFunc builtin);

#endif // UMKA_VM_H_INCLUDED
