#include <stdio.h>
#include <stdlib.h>

#include "umka_gen.h"


// Common functions

void genInit(CodeGen *gen, DebugInfo *debug, ErrorFunc error)
{
    gen->capacity = 1000;
    gen->ip = 0;
    gen->code = malloc(gen->capacity * sizeof(Instruction));
    gen->top = -1;
    gen->breaks = gen->continues = gen->returns = NULL;
    gen->mainDefined = false;
    gen->debug = debug;
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

    gen->code[gen->ip] = *instr;
    gen->code[gen->ip].inlineDeref = false;
    gen->code[gen->ip].inlinePop = false;
    gen->code[gen->ip].debug = *gen->debug;

    gen->ip++;
}



// Peephole optimizations

static bool peepholeFound(CodeGen *gen, int size)
{
    if (gen->ip < size)
        return false;

    // No branching within the peephole
    if (gen->top >= 0)
        if (gen->ip < gen->stack[gen->top] + size)
            return false;

    return true;
}


static bool optimizePop(CodeGen *gen)
{
    if (!peepholeFound(gen, 1))
        return false;

    Instruction *prev = &gen->code[gen->ip - 1];

    // Optimization: CHANGE_REF_CNT + POP -> CHANGE_REF_CNT; POP
    if (prev->opcode == OP_CHANGE_REF_CNT && !prev->inlinePop)
    {
        prev->inlinePop = true;
        return true;
    }

    return false;
}


static bool optimizeSwapAssign(CodeGen *gen, TypeKind typeKind, int structSize)
{
    if (!peepholeFound(gen, 1))
        return false;

    Instruction *prev = &gen->code[gen->ip - 1];

    // Optimization: SWAP + SWAP_ASSIGN -> ASSIGN
    if (prev->opcode == OP_SWAP)
    {
        gen->ip -= 1;
        const Instruction instr = {.opcode = OP_ASSIGN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = structSize};
        genAddInstr(gen, &instr);
        return true;
    }

    return false;
}


static bool optimizeDeref(CodeGen *gen, TypeKind typeKind)
{
    if (!peepholeFound(gen, 1))
        return false;

    Instruction *prev = &gen->code[gen->ip - 1];

    // Optimization: (PUSH | ...) + DEREF -> (PUSH | ...); DEREF
    if (((prev->opcode == OP_PUSH && prev->typeKind == TYPE_PTR) ||
          prev->opcode == OP_PUSH_LOCAL_PTR                      ||
          prev->opcode == OP_GET_ARRAY_PTR                       ||
          prev->opcode == OP_GET_DYNARRAY_PTR                    ||
          prev->opcode == OP_GET_FIELD_PTR)                      &&
         !prev->inlineDeref)
    {
        prev->inlineDeref = true;
        prev->typeKind = typeKind;
        return true;
    }

    return false;
}


static bool optimizeGetArrayPtr(CodeGen *gen, int itemSize)
{
    if (!peepholeFound(gen, 2))
        return false;

    Instruction *prev  = &gen->code[gen->ip - 1];
    Instruction *prev2 = &gen->code[gen->ip - 2];

    // Optimization: PUSH + PUSH + GET_ARRAY_PTR -> GET_FIELD_PTR
    if (prev2->opcode == OP_PUSH && prev->opcode == OP_PUSH)
    {
        int len   = prev->operand.intVal;
        int index = prev2->operand.intVal;

        if (index < 0 || index > len - 1)
            gen->error("Index is out of range");

        gen->ip -= 2;
        genGetFieldPtr(gen, itemSize * index);
        return true;
    }

    return false;
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
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_PTR, .operand.ptrVal = ptrVal};
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
    if (!optimizePop(gen))
    {
        const Instruction instr = {.opcode = OP_POP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
        genAddInstr(gen, &instr);
    }
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
    if (!optimizeDeref(gen, typeKind))
    {
        const Instruction instr = {.opcode = OP_DEREF, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = 0};
        genAddInstr(gen, &instr);
    }
}


void genAssign(CodeGen *gen, TypeKind typeKind, int structSize)
{
    const Instruction instr = {.opcode = OP_ASSIGN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = structSize};
    genAddInstr(gen, &instr);
}


void genSwapAssign(CodeGen *gen, TypeKind typeKind, int structSize)
{
    if (!optimizeSwapAssign(gen, typeKind, structSize))
    {
        const Instruction instr = {.opcode = OP_SWAP_ASSIGN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = structSize};
        genAddInstr(gen, &instr);
    }
}


void genAssignOfs(CodeGen *gen, int offset)
{
    const Instruction instr = {.opcode = OP_ASSIGN_OFS, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = offset};
    genAddInstr(gen, &instr);
}


void genSwapAssignOfs(CodeGen *gen, int offset)
{
    const Instruction instr = {.opcode = OP_SWAP_ASSIGN_OFS, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = offset};
    genAddInstr(gen, &instr);
}


void genChangeRefCnt(CodeGen *gen, TokenKind tokKind, Type *type)
{
    if (typeGarbageCollected(type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT, .tokKind = tokKind, .typeKind = TYPE_NONE, .operand.ptrVal = type};
        genAddInstr(gen, &instr);
    }
}


void genChangeRefCntAssign(CodeGen *gen, Type *type)
{
    if (typeGarbageCollected(type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT_ASSIGN, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = type};
        genAddInstr(gen, &instr);
    }
    else
        genAssign(gen, type->kind, typeSizeRuntime(type));
}


void genUnary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_UNARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genBinary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind, int bufOffset)
{
    const Instruction instr = {.opcode = OP_BINARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = bufOffset};
    genAddInstr(gen, &instr);
}


void genGetArrayPtr(CodeGen *gen, int itemSize)
{
    if (!optimizeGetArrayPtr(gen, itemSize))
    {
        const Instruction instr = {.opcode = OP_GET_ARRAY_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = itemSize};
        genAddInstr(gen, &instr);
    }
}


void genGetDynArrayPtr(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_GET_DYNARRAY_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genGetFieldPtr(CodeGen *gen, int fieldOffset)
{
    if (fieldOffset != 0)
    {
        const Instruction instr = {.opcode = OP_GET_FIELD_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = fieldOffset};
        genAddInstr(gen, &instr);
    }
}


void genAssertType(CodeGen *gen, Type *type)
{
    const Instruction instr = {.opcode = OP_ASSERT_TYPE, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = type};
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


void genCallExtern(CodeGen *gen, void *entry)
{
    const Instruction instr = {.opcode = OP_CALL_EXTERN, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = entry};
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
    genBinary(gen, TOK_EQEQ, TYPE_INT, 0);

    genPushReg(gen, VM_COMMON_REG_1);                    // Update comparison accumulator
    genBinary(gen, TOK_OR, TYPE_BOOL, 0);
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


void genEntryPoint(CodeGen *gen, int start)
{
    genGoFromTo(gen, start, gen->ip);
    if (start == 0)
        gen->mainDefined = true;
}


void genGotosProlog(CodeGen *gen, Gotos *gotos, int block)
{
    gotos->numGotos = 0;
    gotos->block = block;
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


// Assembly output

char *genAsm(CodeGen *gen, char *buf)
{
    int ip = 0, chars = 0;
    do
    {
        if (ip == 0 || gen->code[ip].debug.fileName != gen->code[ip - 1].debug.fileName)
            chars += sprintf(buf + chars, "\n\nModule: %s\n", gen->code[ip].debug.fileName);

        if (gen->code[ip].opcode == OP_ENTER_FRAME)
            chars += sprintf(buf + chars, "\n\n");

        chars += vmAsm(ip, &gen->code[ip], buf + chars);
        chars += sprintf(buf + chars, "\n");

        if (gen->code[ip].opcode == OP_GOTO || gen->code[ip].opcode == OP_GOTO_IF)
            chars += sprintf(buf + chars, "\n");

    } while (gen->code[ip++].opcode != OP_HALT);

    return buf;
}

