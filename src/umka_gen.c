#include <stdlib.h>

#include "umka_gen.h"


void genInit(CodeGen *gen, int capacity)
{
    gen->code = gen->instr = malloc(capacity * sizeof(Instruction));
    gen->top = -1;
}


void genFree(CodeGen *gen)
{
    free(gen->code);
}


// Atomic VM instructions

void genNop(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_NOP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genPushIntConst(CodeGen *gen, int64_t intVal)
{
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_INT, .operand.intVal = intVal};
    *gen->instr++ = instr;
}


void genPushRealConst(CodeGen *gen, double realVal)
{
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_REAL, .operand.realVal = realVal};
    *gen->instr++ = instr;
}


void genPushGlobalPtr(CodeGen *gen, void *ptrVal)
{
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = ptrVal};
    *gen->instr++ = instr;
}


void genPushReg(CodeGen *gen, int regIndex)
{
    const Instruction instr = {.opcode = OP_PUSH_REG, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = regIndex};
    *gen->instr++ = instr;
}


void genPushLocalPtr(CodeGen *gen, int offset)
{
    const Instruction instr = {.opcode = OP_PUSH_LOCAL_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = offset};
    *gen->instr++ = instr;
}


void genPop(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_POP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genPopReg(CodeGen *gen, int regIndex)
{
    const Instruction instr = {.opcode = OP_POP_REG, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = regIndex};
    *gen->instr++ = instr;
}


void genDup(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_DUP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genSwap(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_SWAP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genDeref(CodeGen *gen, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_DEREF, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genAssign(CodeGen *gen, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_ASSIGN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genUnary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_UNARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genBinary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_BINARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genGetArrayPtr(CodeGen *gen, int itemSize)
{
    const Instruction instr = {.opcode = OP_GET_ARRAY_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_VOID, .operand.intVal = itemSize};
    *gen->instr++ = instr;
}


void genGoto(CodeGen *gen, Instruction *dest)
{
    const Instruction instr = {.opcode = OP_GOTO, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = dest};
    *gen->instr++ = instr;
}


void genGotoIf(CodeGen *gen, Instruction *dest)
{
    const Instruction instr = {.opcode = OP_GOTO_IF, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = dest};
    *gen->instr++ = instr;
}


void genCall(CodeGen *gen, int numParams)
{
    const Instruction instr = {.opcode = OP_CALL, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = numParams};
    *gen->instr++ = instr;
}


void genCallBuiltin(CodeGen *gen, TypeKind typeKind, BuiltinFunc builtin)
{
    const Instruction instr = {.opcode = OP_CALL_BUILTIN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.builtinVal = builtin};
    *gen->instr++ = instr;
}


void genReturn(CodeGen *gen, int numParams)
{
    const Instruction instr = {.opcode = OP_RETURN, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = numParams};
    *gen->instr++ = instr;
}


void genEnterFrame(CodeGen *gen, int localVarSize)
{
    const Instruction instr = {.opcode = OP_ENTER_FRAME, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = localVarSize};
    *gen->instr++ = instr;
}


void genLeaveFrame(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_LEAVE_FRAME, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    *gen->instr++ = instr;
}


void genHalt(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_HALT, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    *gen->instr++ = instr;
}


// Compound VM instructions

void genSavePos(CodeGen *gen)
{
    gen->stack[++gen->top] = gen->instr;
}


Instruction *genRestorePos(CodeGen *gen)
{
    return gen->stack[gen->top--];
}


static void genGoFromTo(CodeGen *gen, Instruction *start, Instruction *dest)
{
    Instruction *next = gen->instr;
    gen->instr = start;
    genGoto(gen, dest);
    gen->instr = next;
}


static void genGoFromToIf(CodeGen *gen, Instruction *start, Instruction *dest)
{
    Instruction *next = gen->instr;
    gen->instr = start;
    genGotoIf(gen, dest);
    gen->instr = next;
}


void genIfCondEpilog(CodeGen *gen)
{
    genGotoIf(gen, gen->instr + 2);                         // Goto "if" block start
    genSavePos(gen);
    genNop(gen);                                            // Goto "else" block start / statement end (stub)
}


void genElseProlog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->instr + 1);   // Goto "else" block start (fixup)
    genSavePos(gen);
    genNop(gen);                                            // Goto statement end (stub)
}


void genIfElseEpilog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->instr);       // Goto statement end (fixup)
}


void genWhileCondProlog(CodeGen *gen)
{
    genSavePos(gen);
}


void genWhileCondEpilog(CodeGen *gen)
{
    genIfCondEpilog(gen);
}


void genWhileEpilog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->instr + 1);   // Goto statement end (fixup)
    genGoto(gen, genRestorePos(gen));                       // Goto condition
}


void genForCondProlog(CodeGen *gen)
{
    genSavePos(gen);
}


void genForCondEpilog(CodeGen *gen)
{
    genSavePos(gen);
    genNop(gen);                                            // Goto post-statement end (stub)
    genNop(gen);                                            // Goto statement end (stub)
}


void genForPostStmtEpilog(CodeGen *gen)
{
    Instruction *condEpilog = genRestorePos(gen);
    Instruction *condProlog = genRestorePos(gen);

    genGoto(gen, condProlog);                               // Goto condition
    genGoFromToIf(gen, condEpilog, gen->instr);             // Goto post-statement end (fixup)

    gen->stack[++gen->top] = condEpilog;                    // Place to stack again
}


void genForEpilog(CodeGen *gen)
{
    Instruction *condEpilog = genRestorePos(gen);

    genGoto(gen, condEpilog + 2);                           // Goto post-statement (fixup)
    genGoFromTo(gen, condEpilog + 1, gen->instr);           // Goto statement end (fixup)
}


// a && b ==   a  ? b : a
// a || b == (!a) ? b : a
void genShortCircuitProlog(CodeGen *gen, TokenKind op)
{
    genDup(gen);
    genPopReg(gen, VM_COMMON_REG_0);

    if (op == TOK_OROR)
        genUnary(gen, TOK_NOT, TYPE_BOOL);

    genGotoIf(gen, gen->instr + 2);                         // Goto "b" evaluation
    genSavePos(gen);
    genNop(gen);                                            // Goto expression end (stub)
}


void genShortCircuitEpilog(CodeGen *gen)
{
    genPopReg(gen, VM_COMMON_REG_0);
    genGoFromTo(gen, genRestorePos(gen), gen->instr);       // Goto expression end (fixup)
    genPushReg(gen, VM_COMMON_REG_0);
}


void genEnterFrameStub(CodeGen *gen)
{
    genSavePos(gen);
    genNop(gen);
}


void genLeaveFrameFixup(CodeGen *gen, int localVarSize)
{
    // Fixup enter stub
    Instruction *next = gen->instr;
    gen->instr = genRestorePos(gen);
    genEnterFrame(gen, localVarSize);
    gen->instr = next;

    genLeaveFrame(gen);
}


void genEntryPoint(CodeGen *gen)
{
    genGoFromTo(gen, gen->code, gen->instr);
}



