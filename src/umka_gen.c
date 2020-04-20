#include <stdio.h>
#include <stdlib.h>

#include "umka_gen.h"


void genInit(CodeGen *gen, ErrorFunc error)
{
    gen->capacity = 1000;
    gen->ip = 0;
    gen->code = malloc(gen->capacity * sizeof(Instruction));
    gen->top = -1;
    gen->breaks = gen->continues = gen->returns = NULL;
    gen->entryPointDefined = false;
    gen->error = error;
}


void genFree(CodeGen *gen)
{
    free(gen->code);
}


static void genRealloc(CodeGen *gen)
{
    gen->capacity *= 2;
    gen->code = realloc(gen->code, gen->capacity * sizeof(Instruction));
}


static void genAddInstr(CodeGen *gen, const Instruction *instr)
{
    if (gen->ip >= gen->capacity)
        genRealloc(gen);

    gen->code[gen->ip++] = *instr;
}


// Atomic VM instructions

void genNop(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_NOP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genPushIntConst(CodeGen *gen, int64_t intVal)
{
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_INT, .operand.intVal = intVal};
    genAddInstr(gen, &instr);
}


void genPushRealConst(CodeGen *gen, double realVal)
{
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_REAL, .operand.realVal = realVal};
    genAddInstr(gen, &instr);
}


void genPushGlobalPtr(CodeGen *gen, void *ptrVal)
{
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = ptrVal};
    genAddInstr(gen, &instr);
}


void genPushLocalPtr(CodeGen *gen, int offset)
{
    const Instruction instr = {.opcode = OP_PUSH_LOCAL_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = offset};
    genAddInstr(gen, &instr);
}


void genPushReg(CodeGen *gen, int regIndex)
{
    const Instruction instr = {.opcode = OP_PUSH_REG, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = regIndex};
    genAddInstr(gen, &instr);
}


void genPushStruct(CodeGen *gen, int size)
{
    const Instruction instr = {.opcode = OP_PUSH_STRUCT, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = size};
    genAddInstr(gen, &instr);
}


void genPop(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_POP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genPopReg(CodeGen *gen, int regIndex)
{
    const Instruction instr = {.opcode = OP_POP_REG, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = regIndex};
    genAddInstr(gen, &instr);
}


void genDup(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_DUP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genSwap(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_SWAP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genDeref(CodeGen *gen, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_DEREF, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genAssign(CodeGen *gen, TypeKind typeKind, int structSize)
{
    const Instruction instr = {.opcode = OP_ASSIGN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = structSize};
    genAddInstr(gen, &instr);
}


void genUnary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_UNARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genBinary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_BINARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genGetArrayPtr(CodeGen *gen, int itemSize)
{
    const Instruction instr = {.opcode = OP_GET_ARRAY_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_VOID, .operand.intVal = itemSize};
    genAddInstr(gen, &instr);
}


void genGoto(CodeGen *gen, int dest)
{
    const Instruction instr = {.opcode = OP_GOTO, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = dest};
    genAddInstr(gen, &instr);
}


void genGotoIf(CodeGen *gen, int dest)
{
    const Instruction instr = {.opcode = OP_GOTO_IF, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = dest};
    genAddInstr(gen, &instr);
}


void genCall(CodeGen *gen, int paramSlots)
{
    const Instruction instr = {.opcode = OP_CALL, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = paramSlots};
    genAddInstr(gen, &instr);
}


void genCallBuiltin(CodeGen *gen, TypeKind typeKind, BuiltinFunc builtin)
{
    const Instruction instr = {.opcode = OP_CALL_BUILTIN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.builtinVal = builtin};
    genAddInstr(gen, &instr);
}


void genReturn(CodeGen *gen, int paramSlots)
{
    const Instruction instr = {.opcode = OP_RETURN, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = paramSlots};
    genAddInstr(gen, &instr);
}


void genEnterFrame(CodeGen *gen, int localVarSize)
{
    const Instruction instr = {.opcode = OP_ENTER_FRAME, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = localVarSize};
    genAddInstr(gen, &instr);
}


void genLeaveFrame(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_LEAVE_FRAME, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genHalt(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_HALT, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


// Compound VM instructions

static void genSavePos(CodeGen *gen)
{
    gen->stack[++gen->top] = gen->ip;
}


static int genRestorePos(CodeGen *gen)
{
    return gen->stack[gen->top--];
}


static void genGoFromTo(CodeGen *gen, int start, int dest)
{
    int next = gen->ip;
    gen->ip = start;
    genGoto(gen, dest);
    gen->ip = next;
}


static void genGoFromToIf(CodeGen *gen, int start, int dest)
{
    int next = gen->ip;
    gen->ip = start;
    genGotoIf(gen, dest);
    gen->ip = next;
}


void genIfCondEpilog(CodeGen *gen)
{
    genGotoIf(gen, gen->ip + 2);                         // Goto "if" block start
    genSavePos(gen);
    genNop(gen);                                         // Goto "else" block start / statement end (stub)
}


void genElseProlog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->ip + 1);   // Goto "else" block start (fixup)
    genSavePos(gen);
    genNop(gen);                                         // Goto statement end (stub)
}


void genIfElseEpilog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->ip);       // Goto statement end (fixup)
}


void genSwitchCondEpilog(CodeGen *gen)
{
    genPopReg(gen, VM_COMMON_REG_0);                     // Save switch expression
    genPushIntConst(gen, 0);                             // Initialize comparison accumulator
    genPopReg(gen, VM_COMMON_REG_1);
}


void genCaseExprEpilog(CodeGen *gen, Const *constant)
{
    genPushReg(gen, VM_COMMON_REG_0);                    // Compare switch expression with case constant
    genPushIntConst(gen, constant->intVal);
    genBinary(gen, TOK_EQEQ, TYPE_INT);

    genPushReg(gen, VM_COMMON_REG_1);                    // Update comparison accumulator
    genBinary(gen, TOK_OR, TYPE_BOOL);
    genPopReg(gen, VM_COMMON_REG_1);
}


void genCaseBlockProlog(CodeGen *gen)
{
    genPushReg(gen, VM_COMMON_REG_1);                    // Push comparison accumulator
    genGotoIf(gen, gen->ip + 2);                         // Goto "case" block start
    genSavePos(gen);
    genNop(gen);                                         // Goto next "case" or "default" (stub)
}


void genCaseBlockEpilog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->ip + 1);   // Goto next "case" or "default" (fixup)
    genSavePos(gen);
    genNop(gen);                                         // Goto "switch" end (stub)
}


void genSwitchEpilog(CodeGen *gen, int numCases)
{
    for (int i = 0; i < numCases; i++)
        genGoFromTo(gen, genRestorePos(gen), gen->ip);   // Goto "switch" end (fixup)
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
    genGoFromTo(gen, genRestorePos(gen), gen->ip + 1);   // Goto statement end (fixup)
    genGoto(gen, genRestorePos(gen));                    // Goto condition
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
    int condEpilog = genRestorePos(gen);
    int condProlog = genRestorePos(gen);

    genGoto(gen, condProlog);                            // Goto condition
    genGoFromToIf(gen, condEpilog, gen->ip);             // Goto post-statement end (fixup)

    gen->stack[++gen->top] = condEpilog;                 // Place to stack again
}


void genForEpilog(CodeGen *gen)
{
    int condEpilog = genRestorePos(gen);

    genGoto(gen, condEpilog + 2);                        // Goto post-statement (fixup)
    genGoFromTo(gen, condEpilog + 1, gen->ip);           // Goto statement end (fixup)
}


// a && b ==   a  ? b : a
// a || b == (!a) ? b : a
void genShortCircuitProlog(CodeGen *gen, TokenKind op)
{
    genDup(gen);
    genPopReg(gen, VM_COMMON_REG_0);

    if (op == TOK_OROR)
        genUnary(gen, TOK_NOT, TYPE_BOOL);

    genGotoIf(gen, gen->ip + 2);                         // Goto "b" evaluation
    genSavePos(gen);
    genNop(gen);                                         // Goto expression end (stub)
}


void genShortCircuitEpilog(CodeGen *gen)
{
    genPopReg(gen, VM_COMMON_REG_0);
    genGoFromTo(gen, genRestorePos(gen), gen->ip);       // Goto expression end (fixup)
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
    int next = gen->ip;
    gen->ip = genRestorePos(gen);
    genEnterFrame(gen, localVarSize);
    gen->ip = next;

    genLeaveFrame(gen);
}


void genEntryPoint(CodeGen *gen)
{
    genGoFromTo(gen, 0, gen->ip);
    gen->entryPointDefined = true;
}


void genGotosProlog(CodeGen *gen, Gotos *gotos)
{
    gotos->numGotos = 0;
}


void genGotosAddStub(CodeGen *gen, Gotos *gotos)
{
    if (gotos->numGotos >= MAX_GOTOS)
        gen->error("To many break/continue/return statements");

    gotos->start[gotos->numGotos++] = gen->ip;
    genNop(gen);                                        // Goto block/function end (stub)
}


void genGotosEpilog(CodeGen *gen, Gotos *gotos)
{
    for (int i = 0; i < gotos->numGotos; i++)
        genGoFromTo(gen, gotos->start[i], gen->ip);     // Goto block/function end (fixup)
}


char *genAsm(CodeGen *gen, char *buf)
{
    int ip = 0, pos = 0;
    do
    {
        char instrBuf[DEFAULT_STRING_LEN];
        pos += sprintf(buf + pos, "%08X %s\n", ip, vmAsm(&gen->code[ip], instrBuf));
    } while (gen->code[ip++].opcode != OP_HALT);

    return buf;
}




