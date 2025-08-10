#ifndef UMKA_GEN_H_INCLUDED
#define UMKA_GEN_H_INCLUDED

#include "umka_common.h"
#include "umka_vm.h"


typedef struct
{
    int start[MAX_GOTOS];
    int numGotos;
    int block;
    const Type *returnType;
} Gotos;


typedef enum
{
    GEN_NOTIFICATION_NONE,
    GEN_NOTIFICATION_COPY_RESULT_TO_TEMP_VAR
} GenNotificationKind;


typedef struct
{
    GenNotificationKind kind;
    int ip;
} GenNotification;


typedef struct
{
    Instruction *code;
    int ip, capacity;
    int stack[MAX_BLOCK_NESTING];
    int top;
    int lastJump;
    Gotos *breaks, *continues, *returns;
    Storage *storage;
    DebugInfo *debug, *debugPerInstr;
    GenNotification lastNotification;
    Error *error;
} CodeGen;


void genInit(CodeGen *gen, Storage *storage, DebugInfo *debug, Error *error);

// Atomic VM instructions

void genNop(CodeGen *gen);

void genPushIntConst    (CodeGen *gen, int64_t intVal);
void genPushUIntConst   (CodeGen *gen, uint64_t uintVal);
void genPushRealConst   (CodeGen *gen, double realVal);
void genPushGlobalPtr   (CodeGen *gen, void *ptrVal);
void genPushLocalPtr    (CodeGen *gen, int offset);
void genPushLocalPtrZero(CodeGen *gen, int offset, int size);
void genPushLocal       (CodeGen *gen, TypeKind typeKind, int offset);
void genPushReg         (CodeGen *gen, RegisterIndex regIndex);
void genPushStruct      (CodeGen *gen, int size);
void genPushUpvalue     (CodeGen *gen);
void genPushZero        (CodeGen *gen, int slots);

void genPop   (CodeGen *gen);
void genPopReg(CodeGen *gen, RegisterIndex regIndex);
void genDup   (CodeGen *gen);
void genSwap  (CodeGen *gen);
void genZero  (CodeGen *gen, int size);

void genDeref        (CodeGen *gen, TypeKind typeKind);
void genAssign       (CodeGen *gen, TypeKind typeKind, int structSize);
void genSwapAssign   (CodeGen *gen, TypeKind typeKind, int structSize);
void genAssignParam  (CodeGen *gen, TypeKind typeKind, int structSize);

void genChangeRefCnt            (CodeGen *gen, TokenKind tokKind, const Type *type);
void genChangeRefCntGlobal      (CodeGen *gen, TokenKind tokKind, void *ptrVal, const Type *type);
void genChangeRefCntLocal       (CodeGen *gen, TokenKind tokKind, int offset, const Type *type);
void genChangeRefCntAssign      (CodeGen *gen, const Type *type);
void genSwapChangeRefCntAssign  (CodeGen *gen, const Type *type);
void genChangeLeftRefCntAssign  (CodeGen *gen, const Type *type);

void genUnary (CodeGen *gen, TokenKind tokKind, const Type *type);
void genBinary(CodeGen *gen, TokenKind tokKind, const Type *type);

void genGetArrayPtr   (CodeGen *gen, int itemSize, int len);
void genGetDynArrayPtr(CodeGen *gen);
void genGetMapPtr     (CodeGen *gen, const Type *mapType);
void genGetFieldPtr   (CodeGen *gen, int fieldOffset);

void genAssertType   (CodeGen *gen, const Type *type);
void genAssertRange  (CodeGen *gen, TypeKind destTypeKind, const Type *srcType);

void genWeakenPtr    (CodeGen *gen);
void genStrengthenPtr(CodeGen *gen);

void genGoto        (CodeGen *gen, int dest);
void genGotoIf      (CodeGen *gen, int dest);
void genGotoIfNot   (CodeGen *gen, int dest);

void genCall                (CodeGen *gen, int entry);
void genCallIndirect        (CodeGen *gen, int paramSlots);
void genCallExtern          (CodeGen *gen, void *entry);
void genCallBuiltin         (CodeGen *gen, TypeKind typeKind, BuiltinFunc builtin);
void genCallTypedBuiltin    (CodeGen *gen, const Type *type, BuiltinFunc builtin);
void genReturn              (CodeGen *gen, int paramSlots);

void genEnterFrame(CodeGen *gen, const ParamAndLocalVarLayout *layout);
void genLeaveFrame(CodeGen *gen);

void genHalt(CodeGen *gen);

// Compound VM instructions

void genGoFromTo    (CodeGen *gen, int start, int dest);
void genGoFromToIf  (CodeGen *gen, int start, int dest);

void genIfCondEpilog(CodeGen *gen);
void genIfEpilog    (CodeGen *gen);
void genElseProlog  (CodeGen *gen);
void genIfElseEpilog(CodeGen *gen);

void genSwitchCondEpilog    (CodeGen *gen);
void genCaseConstantCheck   (CodeGen *gen, const Type *type, Const *constant);
void genCaseBlockProlog     (CodeGen *gen, int numCaseConstants);
void genCaseBlockEpilog     (CodeGen *gen);
void genSwitchEpilog        (CodeGen *gen, int numCases);

void genWhileCondProlog(CodeGen *gen);
void genWhileCondEpilog(CodeGen *gen);
void genWhileEpilog    (CodeGen *gen);

void genForCondProlog    (CodeGen *gen);
void genForCondEpilog    (CodeGen *gen);
void genForPostStmtEpilog(CodeGen *gen);
void genForEpilog        (CodeGen *gen);

void genShortCircuitProlog(CodeGen *gen);
void genShortCircuitEpilog(CodeGen *gen, TokenKind op);

void genEnterFrameStub (CodeGen *gen);
void genLeaveFrameFixup(CodeGen *gen, const ParamAndLocalVarLayout *layout);

void genEntryPoint(CodeGen *gen, int start);

int  genTryRemoveImmediateEntryPoint(CodeGen *gen);

void genGotosProlog (CodeGen *gen, Gotos *gotos, int block);
void genGotosAddStub(CodeGen *gen, Gotos *gotos);
void genGotosEpilog (CodeGen *gen, Gotos *gotos);

void genCopyResultToTempVar(CodeGen *gen, const Type *type, int offset);
int  genTryRemoveCopyResultToTempVar(CodeGen *gen);

int genAsm(CodeGen *gen, char *buf, int size);

#endif // UMKA_GEN_H_INCLUDED
