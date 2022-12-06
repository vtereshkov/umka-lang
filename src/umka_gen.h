#ifndef UMKA_GEN_H_INCLUDED
#define UMKA_GEN_H_INCLUDED

#include "umka_common.h"
#include "umka_vm.h"


typedef struct
{
    int start[MAX_GOTOS];
    int numGotos;
    int block;
    Type *returnType;
} Gotos;


typedef struct
{
    Instruction *code;
    int ip, capacity;
    int stack[MAX_BLOCK_NESTING];
    int top;
    Gotos *breaks, *continues, *returns;
    DebugInfo *debug, *debugPerInstr;
    Error *error;
} CodeGen;


void genInit(CodeGen *gen, DebugInfo *debug, Error *error);
void genFree(CodeGen *gen);

// Atomic VM instructions

void genNop(CodeGen *gen);

void genPushIntConst (CodeGen *gen, int64_t intVal);
void genPushUIntConst(CodeGen *gen, uint64_t uintVal);
void genPushRealConst(CodeGen *gen, double realVal);
void genPushGlobalPtr(CodeGen *gen, void *ptrVal);
void genPushLocalPtr (CodeGen *gen, int offset);
void genPushLocal    (CodeGen *gen, TypeKind typeKind, int offset);
void genPushReg      (CodeGen *gen, int regIndex);
void genPushStruct   (CodeGen *gen, int size);

void genPop   (CodeGen *gen);
void genPopReg(CodeGen *gen, int regIndex);
void genDup   (CodeGen *gen);
void genSwap  (CodeGen *gen);
void genZero  (CodeGen *gen, int size);

void genDeref        (CodeGen *gen, TypeKind typeKind);
void genAssign       (CodeGen *gen, TypeKind typeKind, int structSize);
void genSwapAssign   (CodeGen *gen, TypeKind typeKind, int structSize);

void genChangeRefCnt            (CodeGen *gen, TokenKind tokKind, Type *type);
void genChangeRefCntAssign      (CodeGen *gen, Type *type);
void genSwapChangeRefCntAssign  (CodeGen *gen, Type *type);

void genUnary (CodeGen *gen, TokenKind tokKind, TypeKind typeKind);
void genBinary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind, int bufOffset /*bytes*/);

void genGetArrayPtr   (CodeGen *gen, int itemSize, int len);
void genGetDynArrayPtr(CodeGen *gen);
void genGetMapPtr     (CodeGen *gen, Type *mapType);
void genGetFieldPtr   (CodeGen *gen, int fieldOffset);

void genAssertType   (CodeGen *gen, Type *type);
void genAssertRange  (CodeGen *gen, TypeKind typeKind);

void genWeakenPtr    (CodeGen *gen);
void genStrengthenPtr(CodeGen *gen);

void genGoto  (CodeGen *gen, int dest);
void genGotoIf(CodeGen *gen, int dest);

void genCall        (CodeGen *gen, int entry);
void genCallIndirect(CodeGen *gen, int paramSlots);
void genCallExtern  (CodeGen *gen, void *entry);
void genCallBuiltin (CodeGen *gen, TypeKind typeKind, BuiltinFunc builtin);
void genReturn      (CodeGen *gen, int paramSlots);

void genEnterFrame(CodeGen *gen, int localVarSlots, int paramSlots, bool inHeap);
void genLeaveFrame(CodeGen *gen, bool inHeap);

void genHalt(CodeGen *gen);

// Compound VM instructions

void genGoFromTo(CodeGen *gen, int start, int dest);
void genGoFromToIf(CodeGen *gen, int start, int dest);

void genIfCondEpilog(CodeGen *gen);
void genElseProlog  (CodeGen *gen);
void genIfElseEpilog(CodeGen *gen);

void genSwitchCondEpilog(CodeGen *gen);
void genCaseExprEpilog  (CodeGen *gen, Const *constant);
void genCaseBlockProlog (CodeGen *gen);
void genCaseBlockEpilog (CodeGen *gen);
void genSwitchEpilog    (CodeGen *gen, int numCases);

void genWhileCondProlog(CodeGen *gen);
void genWhileCondEpilog(CodeGen *gen);
void genWhileEpilog    (CodeGen *gen);

void genForCondProlog    (CodeGen *gen);
void genForCondEpilog    (CodeGen *gen);
void genForPostStmtEpilog(CodeGen *gen);
void genForEpilog        (CodeGen *gen);

void genShortCircuitProlog(CodeGen *gen, TokenKind op);
void genShortCircuitEpilog(CodeGen *gen);

void genEnterFrameStub (CodeGen *gen);
void genLeaveFrameFixup(CodeGen *gen, int localVarSlots, int paramSlots);

void genEntryPoint(CodeGen *gen, int start);

int  genTryRemoveImmediateEntryPoint(CodeGen *gen);

void genGotosProlog (CodeGen *gen, Gotos *gotos, int block);
void genGotosAddStub(CodeGen *gen, Gotos *gotos);
void genGotosEpilog (CodeGen *gen, Gotos *gotos);

char *genAsm(CodeGen *gen, char *buf, int size);

#endif // UMKA_GEN_H_INCLUDED
