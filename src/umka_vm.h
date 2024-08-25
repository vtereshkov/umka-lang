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

    VM_MIN_FREE_STACK    = 1024,                    // Slots
    VM_MIN_HEAP_CHUNK    = 64,                      // Bytes
    VM_MIN_HEAP_PAGE     = 1024 * 1024,             // Bytes

    VM_RETURN_FROM_VM    = -2,                      // Used instead of return address in functions called by umkaCall()
    VM_RETURN_FROM_FIBER = -1                       // Used instead of return address in fiber function calls
};

enum
{
    VM_RUNTIME_ERROR = -1
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
    BUILTIN_CEIL,
    BUILTIN_FLOOR,
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
    BUILTIN_MAKEFROMARR,    // Array to dynamic array - implicit calls only
    BUILTIN_MAKEFROMSTR,    // String to dynamic array - implicit calls only
    BUILTIN_MAKETOARR,      // Dynamic array to array - implicit calls only
    BUILTIN_MAKETOSTR,      // Character or dynamic array to string - implicit calls only
    BUILTIN_COPY,
    BUILTIN_APPEND,
    BUILTIN_INSERT,
    BUILTIN_DELETE,
    BUILTIN_SLICE,
    BUILTIN_SORT,
    BUILTIN_SORTFAST,
    BUILTIN_LEN,
    BUILTIN_CAP,
    BUILTIN_SIZEOF,
    BUILTIN_SIZEOFSELF,
    BUILTIN_SELFHASPTR,
    BUILTIN_SELFTYPEEQ,
    BUILTIN_TYPEPTR,
    BUILTIN_VALID,

    // Maps
    BUILTIN_VALIDKEY,
    BUILTIN_KEYS,

    // Fibers
    BUILTIN_RESUME,

    // Misc
    BUILTIN_MEMUSAGE,
    BUILTIN_EXIT
} BuiltinFunc;


typedef union
{
    int64_t intVal;         // For all ordinal types except uint
    uint64_t uintVal;
    int32_t int32Val[2];
    void *ptrVal;
    uint64_t weakPtrVal;
    double realVal;         // For all real types
    BuiltinFunc builtinVal;
} Slot;


typedef struct
{
    Opcode opcode;
    Opcode inlineOpcode;         // Inlined instruction (DEREF, SWAP): PUSH + DEREF, SWAP + ASSIGN etc.
    TokenKind tokKind;           // Unary/binary operation token
    TypeKind typeKind;
    Type *type;
    Slot operand;
} Instruction;


typedef struct
{
    int64_t entryOffset;
    Slot *params;
    Slot *result;
} FuncContext;


typedef void (*ExternFunc)(Slot *params, Slot *result);


typedef struct tagHeapPage
{
    int id;
    void *ptr;
    int numChunks, numOccupiedChunks, numChunksWithOnFree, chunkSize;
    int refCnt;
    struct tagHeapPage *prev, *next;
} HeapPage;


typedef struct
{
    HeapPage *first, *last;
    int freeId;
    int64_t totalSize;
    struct tagFiber *fiber;
    Error *error;
} HeapPages;


typedef struct
{
    int refCnt;
    int size;
    struct tagType *type;       // Optional type for garbage collection
    ExternFunc onFree;          // Optional callback called when ref count reaches zero
    int64_t ip;                 // Optional instruction pointer at which the chunk has been allocated
    bool isStack;
    bool reserved[7];
} HeapChunkHeader;


typedef struct
{
    void *ptr;
    Type *type;
    HeapPage *pageForDeferred;   // Mandatory for deferred ref count updates, NULL otherwise
} RefCntChangeCandidate;


typedef struct
{
    RefCntChangeCandidate *stack;
    int top, capacity;
} RefCntChangeCandidates;


typedef enum
{
    HOOK_CALL,
    HOOK_RETURN,

    NUM_HOOKS
} HookEvent;


typedef void (*HookFunc)(const char *fileName, const char *funcName, int line);


typedef struct tagFiber
{
    // Must have 8 byte alignment
    Instruction *code;
    int ip;
    Slot *stack, *top, *base;
    int stackSize;
    Slot reg[VM_NUM_REGS];
    struct tagFiber *parent;
    DebugInfo *debugPerInstr;
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
    HookFunc hooks[NUM_HOOKS];
    bool terminatedNormally;
    Error *error;
} VM;


void vmInit                     (VM *vm, int stackSize /* slots */, bool fileSystemEnabled, Error *error);
void vmFree                     (VM *vm);
void vmReset                    (VM *vm, Instruction *code, DebugInfo *debugPerInstr);
void vmRun                      (VM *vm, FuncContext *fn);
bool vmAlive                    (VM *vm);
void vmKill                     (VM *vm);
int vmAsm                       (int ip, Instruction *code, DebugInfo *debugPerInstr, char *buf, int size);
bool vmUnwindCallStack          (VM *vm, Slot **base, int *ip);
void vmSetHook                  (VM *vm, HookEvent event, HookFunc hook);
void *vmAllocData               (VM *vm, int size, ExternFunc onFree);
void vmIncRef                   (VM *vm, void *ptr);
void vmDecRef                   (VM *vm, void *ptr);
void *vmGetMapNodeData          (VM *vm, Map *map, Slot key);
char *vmMakeStr                 (VM *vm, const char *str);
void vmMakeDynArray             (VM *vm, DynArray *array, Type *type, int len);
int64_t vmGetMemUsage           (VM *vm);
const char *vmBuiltinSpelling   (BuiltinFunc builtin);

#endif // UMKA_VM_H_INCLUDED
