#ifndef UMKA_GEN_H_INCLUDED
#define UMKA_GEN_H_INCLUDED

#include "umka_common.h"
#include "umka_vm.h"


typedef struct
{
    int start[MAX_GOTOS];
    int numGotos;
    Type *returnType;
} Gotos;


typedef struct
{
    Instruction *code;
    int ip, capacity;
    int stack[MAX_BLOCK_NESTING];
    int top;
    Gotos *breaks, *continues, *returns;
    ErrorFunc error;
} CodeGen;


void genInit(CodeGen *gen, ErrorFunc error);
void genFree(CodeGen *gen);

// Atomic VM instructions

void genNop(CodeGen *gen);

void genPushIntConst (CodeGen *gen, int64_t intVal);
void genPushRealConst(CodeGen *gen, double realVal);
void genPushGlobalPtr(CodeGen *gen, void *ptrVal);
void genPushLocalPtr (CodeGen *gen, int offset);
void genPushReg      (CodeGen *gen, int regIndex);

void genPop   (CodeGen *gen);
void genPopReg(CodeGen *gen, int regIndex);
void genDup   (CodeGen *gen);
void genSwap(CodeGen *gen);

void genDeref (CodeGen *gen, TypeKind typeKind);
void genAssign(CodeGen *gen, TypeKind typeKind);

void genUnary (CodeGen *gen, TokenKind tokKind, TypeKind typeKind);
void genBinary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind);

void genGetArrayPtr(CodeGen *gen, int itemSize);

void genGoto  (CodeGen *gen, int dest);
void genGotoIf(CodeGen *gen, int dest);

void genCall       (CodeGen *gen, int numParams);
void genCallBuiltin(CodeGen *gen, TypeKind typeKind, BuiltinFunc builtin);
void genReturn     (CodeGen *gen, int numParams);

void genEnterFrame(CodeGen *gen, int localVarSize);
void genLeaveFrame(CodeGen *gen);

void genHalt(CodeGen *gen);

// Compound VM instructions

void genIfCondEpilog(CodeGen *gen);
void genElseProlog  (CodeGen *gen);
void genIfElseEpilog(CodeGen *gen);

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
void genLeaveFrameFixup(CodeGen *gen, int localVarSize);

void genEntryPoint(CodeGen *gen);

void genGotosProlog (CodeGen *gen, Gotos *gotos);
void genGotosAddStub(CodeGen *gen, Gotos *gotos);
void genGotosEpilog (CodeGen *gen, Gotos *gotos);

char *genAsm(CodeGen *gen, char *buf);

#endif // UMKA_GEN_H_INCLUDED
