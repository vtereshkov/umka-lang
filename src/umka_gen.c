#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>

#include "umka_gen.h"
#include "umka_const.h"


// Common functions

static void genNotify(CodeGen *gen, GenNotificationKind kind)
{
    gen->lastNotification.kind = kind;
    gen->lastNotification.ip = gen->ip;
}


static void genUnnotify(CodeGen *gen)
{
    genNotify(gen, GEN_NOTIFICATION_NONE);
}


static bool genJustNotified(CodeGen *gen, GenNotificationKind kind)
{
    return gen->lastNotification.kind == kind && gen->lastNotification.ip == gen->ip;
}


void genInit(CodeGen *gen, DebugInfo *debug, Error *error)
{
    gen->capacity = 1000;
    gen->ip = 0;
    gen->code = malloc(gen->capacity * sizeof(Instruction));
    gen->top = -1;
    gen->breaks = gen->continues = gen->returns = NULL;
    gen->debug = debug;
    gen->debugPerInstr = malloc(gen->capacity * sizeof(DebugInfo));
    gen->error = error;
    genUnnotify(gen);
}


void genFree(CodeGen *gen)
{
    free(gen->code);
    free(gen->debugPerInstr);
}


static void genRealloc(CodeGen *gen)
{
    gen->capacity *= 2;
    gen->code = realloc(gen->code, gen->capacity * sizeof(Instruction));
    gen->debugPerInstr = realloc(gen->debugPerInstr, gen->capacity * sizeof(DebugInfo));
}


static void genAddInstr(CodeGen *gen, const Instruction *instr)
{
    if (gen->ip >= gen->capacity)
        genRealloc(gen);

    gen->code[gen->ip] = *instr;
    gen->debugPerInstr[gen->ip] = *gen->debug;

    gen->ip++;
    genUnnotify(gen);
}


static void genRemoveInstr(CodeGen *gen)
{
    gen->ip--;
    genUnnotify(gen);
}


// Peephole optimizations

static Instruction *getPrevInstr(CodeGen *gen, int depth)
{
    if (gen->ip < depth)
        return NULL;

    // No branching within the peephole
    if (gen->top >= 0)
        if (gen->ip < gen->stack[gen->top] + depth)
            return NULL;

    return &gen->code[gen->ip - depth];
}


static bool optimizePushLocalPtr(CodeGen *gen, int offset)
{
    Instruction *prev = getPrevInstr(gen, 1), *prev2 = getPrevInstr(gen, 2);

    // Optimization: PUSH_LOCAL_PTR + ZERO + PUSH_LOCAL_PTR -> PUSH_LOCAL_PTR_ZERO
    if (prev  && prev->opcode  == OP_ZERO &&
        prev2 && prev2->opcode == OP_PUSH_LOCAL_PTR)
    {
        if (offset == prev2->operand.intVal)
        {
            const int size = prev->operand.intVal;

            genRemoveInstr(gen);
            genRemoveInstr(gen);
            genPushLocalPtrZero(gen, offset, size);

            genUnnotify(gen);
            return true;
        }
    }

    return false;
}


static bool optimizePushReg(CodeGen *gen, int regIndex)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: POP_REG SELF + PUSH_REG SELF -> 0
    // This is an inequivalent replacement since it cannot update VM_REG_SELF, but the updated register is never actually used
    if (prev && prev->opcode == OP_POP_REG && prev->operand.intVal == VM_REG_SELF && regIndex == VM_REG_SELF)
    {
        genRemoveInstr(gen);
        return true;
    }

    return false;
}


static bool optimizePushZero(CodeGen *gen, int slots)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: PUSH_ZERO (n) + PUSH_ZERO (m) -> PUSH_ZERO (n + m)
    if (prev && prev->opcode == OP_PUSH_ZERO)
    {
        prev->operand.intVal += slots;
        genUnnotify(gen);
        return true;
    }

    return false;
}


static bool optimizePop(CodeGen *gen)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: POP (n) + POP -> POP (n + 1)
    if (prev && prev->opcode == OP_POP)
    {
        prev->operand.intVal++;
        genUnnotify(gen);
        return true;
    }

    return false;
}


static bool optimizeSwapAssign(CodeGen *gen, TypeKind typeKind, int structSize)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: SWAP + SWAP_ASSIGN -> ASSIGN
    if (prev && prev->opcode == OP_SWAP)
    {
        genRemoveInstr(gen);
        const Instruction instr = {.opcode = OP_ASSIGN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = structSize};
        genAddInstr(gen, &instr);
        return true;
    }

    return false;
}


static bool optimizeChangeRefCnt(CodeGen *gen, Type *type)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: PUSH ^ + CHANGE_REF_CNT -> PUSH ^
    if (prev && prev->opcode == OP_PUSH && prev->inlineOpcode == OP_NOP && prev->typeKind == TYPE_PTR && (type->kind == TYPE_PTR || type->kind == TYPE_STR))
    {
        genUnnotify(gen);
        return true;
    }

    return false;
}


static bool optimizeDeref(CodeGen *gen, TypeKind typeKind)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: PUSH_LOCAL_PTR + DEREF -> PUSH_LOCAL
    // These sequences constitute 20...30 % of all instructions and need a special, more optimized single instruction
    if (prev && prev->opcode == OP_PUSH_LOCAL_PTR)
    {
        genRemoveInstr(gen);
        genPushLocal(gen, typeKind, prev->operand.intVal);
        return true;
    }

    // Optimization: (PUSH | ...) + DEREF -> (PUSH | ...); DEREF
    if (prev && ((prev->opcode == OP_PUSH && prev->typeKind == TYPE_PTR) ||
                  prev->opcode == OP_GET_ARRAY_PTR                       ||
                  prev->opcode == OP_GET_DYNARRAY_PTR                    ||
                  prev->opcode == OP_GET_FIELD_PTR)                      &&
                  prev->inlineOpcode == OP_NOP)
    {
        prev->inlineOpcode = OP_DEREF;
        prev->typeKind = typeKind;
        genUnnotify(gen);
        return true;
    }

    return false;
}


static bool optimizeGetArrayPtr(CodeGen *gen, int itemSize, int len)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: PUSH + GET_ARRAY_PTR -> GET_FIELD_PTR
    if (prev && prev->opcode == OP_PUSH && prev->typeKind == TYPE_INT && prev->inlineOpcode == OP_NOP && len >= 0)
    {
        int index = prev->operand.intVal;

        if (index < 0 || index > len - 1)
            gen->error->handler(gen->error->context, "Index %d is out of range 0...%d", index, len - 1);

        genRemoveInstr(gen);
        genGetFieldPtr(gen, itemSize * index);
        return true;
    }

    return false;
}


static bool optimizeGetFieldPtr(CodeGen *gen, int fieldOffset)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: PUSH_LOCAL_PTR + GET_FIELD_PTR -> PUSH_LOCAL_PTR
    if (prev && prev->opcode == OP_PUSH_LOCAL_PTR)
    {
        prev->operand.intVal += fieldOffset;
        genUnnotify(gen);
        return true;
    }

    return false;
}


static bool optimizeUnary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    Instruction *prev = getPrevInstr(gen, 1);

    // Optimization: PUSH + UNARY -> PUSH
    if (prev && prev->opcode == OP_PUSH && prev->inlineOpcode == OP_NOP && tokKind != TOK_PLUSPLUS && tokKind != TOK_MINUSMINUS)
    {
        Const arg;
        if (typeKindReal(typeKind))
            arg.realVal = prev->operand.realVal;
        else
            arg.intVal = prev->operand.intVal;

        Consts consts = {.error = gen->error};
        constUnary(&consts, &arg, tokKind, typeKind);

        prev->typeKind = typeKind;

        if (typeKindReal(typeKind))
            prev->operand.realVal = arg.realVal;
        else
            prev->operand.intVal = arg.intVal;

        genUnnotify(gen);
        return true;
    }

    return false;
}


static bool optimizeBinary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    Instruction *prev = getPrevInstr(gen, 1), *prev2 = getPrevInstr(gen, 2);

    // Optimization: PUSH + PUSH + BINARY -> PUSH
    if (prev  && prev->opcode  == OP_PUSH && prev->inlineOpcode  == OP_NOP &&
        prev2 && prev2->opcode == OP_PUSH && prev2->inlineOpcode == OP_NOP &&
       (typeKindOrdinal(typeKind) || typeKindReal(typeKind) || typeKind == TYPE_BOOL))
    {
        Const lhs, rhs;
        if (typeKindReal(typeKind))
        {
            lhs.realVal = prev2->operand.realVal;
            rhs.realVal = prev->operand.realVal;
        }
        else
        {
            lhs.intVal = prev2->operand.intVal;
            rhs.intVal = prev->operand.intVal;
        }

        prev = prev2;
        genRemoveInstr(gen);

        Consts consts = {.error = gen->error};
        constBinary(&consts, &lhs, &rhs, tokKind, typeKind);

        prev->typeKind = typeKind;

        if (tokKind == TOK_EQEQ      || tokKind == TOK_NOTEQ   ||
            tokKind == TOK_GREATER   || tokKind == TOK_LESS    ||
            tokKind == TOK_GREATEREQ || tokKind == TOK_LESSEQ)
        {
            prev->typeKind = TYPE_BOOL;
        }

        if (typeKindReal(typeKind))
            prev->operand.realVal = lhs.realVal;
        else
            prev->operand.intVal = lhs.intVal;

        genUnnotify(gen);
        return true;
    }

    return false;
}


static bool optimizeCallBuiltin(CodeGen *gen, TypeKind typeKind, BuiltinFunc builtin)
{
    Instruction *prev = getPrevInstr(gen, 1), *prev2 = getPrevInstr(gen, 2);

    // Optimization: PUSH + CALL_BUILTIN -> PUSH
    if (prev && prev->opcode == OP_PUSH && prev->inlineOpcode == OP_NOP)
    {
        Const arg, arg2;
        TypeKind resultTypeKind = TYPE_NONE;

        switch (builtin)
        {
            case BUILTIN_REAL:
            {
                arg.intVal = prev->operand.intVal;
                resultTypeKind = TYPE_REAL;
                break;
            }
            case BUILTIN_REAL_LHS:
            {
                if (prev2 && prev2->opcode == OP_PUSH && prev2->inlineOpcode == OP_NOP)
                {
                    arg.intVal = prev2->operand.intVal;
                    resultTypeKind = TYPE_REAL;
                    prev = prev2;
                }
                break;
            }
            case BUILTIN_ROUND:
            case BUILTIN_TRUNC:
            case BUILTIN_CEIL:
            case BUILTIN_FLOOR:
            case BUILTIN_FABS:
            case BUILTIN_SQRT:
            case BUILTIN_SIN:
            case BUILTIN_COS:
            case BUILTIN_ATAN:
            case BUILTIN_EXP:
            case BUILTIN_LOG:
            {
                arg.realVal = prev->operand.realVal;
                resultTypeKind = (builtin == BUILTIN_ROUND || builtin == BUILTIN_TRUNC || builtin == BUILTIN_CEIL || builtin == BUILTIN_FLOOR) ? TYPE_INT : TYPE_REAL;
                break;
            }
            case BUILTIN_ATAN2:
            {
                if (prev2 && prev2->opcode == OP_PUSH && prev2->inlineOpcode == OP_NOP)
                {
                    arg.realVal = prev2->operand.realVal;
                    arg2.realVal = prev->operand.realVal;

                    resultTypeKind = TYPE_REAL;
                    prev = prev2;
                    genRemoveInstr(gen);
                }
                break;
            }
            default: break;
        }

        if (resultTypeKind != TYPE_NONE)
        {
            Consts consts = {.error = gen->error};
            constCallBuiltin(&consts, &arg, &arg2, prev->typeKind, builtin);

            prev->typeKind = resultTypeKind;

            if (resultTypeKind == TYPE_REAL)
                prev->operand.realVal = arg.realVal;
            else
                prev->operand.intVal = arg.intVal;

            genUnnotify(gen);
            return true;
        }
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


void genPushUIntConst(CodeGen *gen, uint64_t uintVal)
{
    const Instruction instr = {.opcode = OP_PUSH, .tokKind = TOK_NONE, .typeKind = TYPE_UINT, .operand.uintVal = uintVal};
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
    if (!optimizePushLocalPtr(gen, offset))
    {
        const Instruction instr = {.opcode = OP_PUSH_LOCAL_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = offset};
        genAddInstr(gen, &instr);
    }
}


void genPushLocalPtrZero(CodeGen *gen, int offset, int size)
{
    const Instruction instr = {.opcode = OP_PUSH_LOCAL_PTR_ZERO, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.int32Val = {offset, size}};
    genAddInstr(gen, &instr);
}


void genPushLocal(CodeGen *gen, TypeKind typeKind, int offset)
{
    const Instruction instr = {.opcode = OP_PUSH_LOCAL, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = offset};
    genAddInstr(gen, &instr);
}


void genPushReg(CodeGen *gen, int regIndex)
{
    if (!optimizePushReg(gen, regIndex))
    {
        const Instruction instr = {.opcode = OP_PUSH_REG, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = regIndex};
        genAddInstr(gen, &instr);
    }
}


void genPushUpvalue(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_PUSH_UPVALUE, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genPushZero(CodeGen *gen, int slots)
{
    if (!optimizePushZero(gen, slots))
    {
        const Instruction instr = {.opcode = OP_PUSH_ZERO, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = slots};
        genAddInstr(gen, &instr);
    }
}


void genPop(CodeGen *gen)
{
    if (!optimizePop(gen))
    {
        const Instruction instr = {.opcode = OP_POP, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 1};
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


void genZero(CodeGen *gen, int size)
{
    const Instruction instr = {.opcode = OP_ZERO, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = size};
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
        const Instruction instr = {.opcode = OP_ASSIGN, .inlineOpcode = OP_SWAP, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = structSize};
        genAddInstr(gen, &instr);
    }
}


void genAssignParam(CodeGen *gen, TypeKind typeKind, int structSize)
{
    const Instruction instr = {.opcode = OP_ASSIGN_PARAM, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = structSize};
    genAddInstr(gen, &instr);
}


void genChangeRefCnt(CodeGen *gen, TokenKind tokKind, Type *type)
{
    if (typeGarbageCollected(type) && !optimizeChangeRefCnt(gen, type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT, .tokKind = tokKind, .type = type};
        genAddInstr(gen, &instr);
    }
}


void genChangeRefCntGlobal(CodeGen *gen, TokenKind tokKind, void *ptrVal, Type *type)
{
    if (typeGarbageCollected(type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT_GLOBAL, .tokKind = tokKind, .operand.ptrVal = ptrVal, .type = type};
        genAddInstr(gen, &instr);
    }
}


void genChangeRefCntLocal(CodeGen *gen, TokenKind tokKind, int offset, Type *type)
{
    if (typeGarbageCollected(type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT_LOCAL, .tokKind = tokKind, .operand.intVal = offset, .type = type};
        genAddInstr(gen, &instr);
    }
}


void genChangeRefCntAssign(CodeGen *gen, Type *type)
{
    if (typeGarbageCollected(type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT_ASSIGN, .tokKind = TOK_NONE, .type = type};
        genAddInstr(gen, &instr);
    }
    else
        genAssign(gen, type->kind, typeSizeNoCheck(type));
}


void genSwapChangeRefCntAssign(CodeGen *gen, Type *type)
{
    if (typeGarbageCollected(type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT_ASSIGN, .inlineOpcode = OP_SWAP, .tokKind = TOK_NONE, .type = type};
        genAddInstr(gen, &instr);
    }
    else
        genSwapAssign(gen, type->kind, typeSizeNoCheck(type));
}


void genChangeLeftRefCntAssign(CodeGen *gen, Type *type)
{
    if (typeGarbageCollected(type))
    {
        const Instruction instr = {.opcode = OP_CHANGE_REF_CNT_ASSIGN, .tokKind = TOK_MINUSMINUS, .type = type};
        genAddInstr(gen, &instr);
    }
    else
        genAssign(gen, type->kind, typeSizeNoCheck(type));
}


void genUnary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind)
{
    if (!optimizeUnary(gen, tokKind, typeKind))
    {
        const Instruction instr = {.opcode = OP_UNARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = 0};
        genAddInstr(gen, &instr);
    }
}


void genBinary(CodeGen *gen, TokenKind tokKind, TypeKind typeKind, int structSize)
{
    if (!optimizeBinary(gen, tokKind, typeKind))
    {
        const Instruction instr = {.opcode = OP_BINARY, .tokKind = tokKind, .typeKind = typeKind, .operand.intVal = structSize};
        genAddInstr(gen, &instr);
    }
}


void genGetArrayPtr(CodeGen *gen, int itemSize, int len)
{
    if (!optimizeGetArrayPtr(gen, itemSize, len))
    {
        const Instruction instr = {.opcode = OP_GET_ARRAY_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.int32Val = {itemSize, len}};
        genAddInstr(gen, &instr);
    }
}


void genGetDynArrayPtr(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_GET_DYNARRAY_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genGetMapPtr(CodeGen *gen, Type *mapType)
{
    const Instruction instr = {.opcode = OP_GET_MAP_PTR, .tokKind = TOK_NONE, .type = mapType};
    genAddInstr(gen, &instr);
}


void genGetFieldPtr(CodeGen *gen, int fieldOffset)
{
    if (!optimizeGetFieldPtr(gen, fieldOffset))
    {
        const Instruction instr = {.opcode = OP_GET_FIELD_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = fieldOffset};
        genAddInstr(gen, &instr);
    }
}


void genAssertType(CodeGen *gen, Type *type)
{
    const Instruction instr = {.opcode = OP_ASSERT_TYPE, .tokKind = TOK_NONE, .type = type};
    genAddInstr(gen, &instr);
}


void genAssertRange(CodeGen *gen, TypeKind typeKind)
{
    const Instruction instr = {.opcode = OP_ASSERT_RANGE, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genWeakenPtr(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_WEAKEN_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
    genAddInstr(gen, &instr);
}


void genStrengthenPtr(CodeGen *gen)
{
    const Instruction instr = {.opcode = OP_STRENGTHEN_PTR, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = 0};
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


void genGotoIfNot(CodeGen *gen, int dest)
{
    const Instruction instr = {.opcode = OP_GOTO_IF_NOT, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = dest};
    genAddInstr(gen, &instr);
}


void genCall(CodeGen *gen, int entry)
{
    const Instruction instr = {.opcode = OP_CALL, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = entry};
    genAddInstr(gen, &instr);
}


void genCallIndirect(CodeGen *gen, int paramSlots)
{
    const Instruction instr = {.opcode = OP_CALL_INDIRECT, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = paramSlots};
    genAddInstr(gen, &instr);
}


void genCallExtern(CodeGen *gen, void *entry)
{
    const Instruction instr = {.opcode = OP_CALL_EXTERN, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.ptrVal = entry};
    genAddInstr(gen, &instr);
}


void genCallBuiltin(CodeGen *gen, TypeKind typeKind, BuiltinFunc builtin)
{
    if (!optimizeCallBuiltin(gen, typeKind, builtin))
    {
        const Instruction instr = {.opcode = OP_CALL_BUILTIN, .tokKind = TOK_NONE, .typeKind = typeKind, .operand.builtinVal = builtin};
        genAddInstr(gen, &instr);
    }
}


void genCallTypedBuiltin(CodeGen *gen, Type *type, BuiltinFunc builtin)
{
    if (!optimizeCallBuiltin(gen, type->kind, builtin))
    {
        const Instruction instr = {.opcode = OP_CALL_BUILTIN, .tokKind = TOK_NONE, .type = type, .operand.builtinVal = builtin};
        genAddInstr(gen, &instr);
    }
}


void genReturn(CodeGen *gen, int paramSlots)
{
    const Instruction instr = {.opcode = OP_RETURN, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = paramSlots};
    genAddInstr(gen, &instr);
}


void genEnterFrame(CodeGen *gen, int localVarSlots)
{
    const Instruction instr = {.opcode = OP_ENTER_FRAME, .tokKind = TOK_NONE, .typeKind = TYPE_NONE, .operand.intVal = localVarSlots};
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


void genGoFromTo(CodeGen *gen, int start, int dest)
{
    int next = gen->ip;
    gen->ip = start;
    genGoto(gen, dest);
    gen->ip = next;
}


void genGoFromToIf(CodeGen *gen, int start, int dest)
{
    int next = gen->ip;
    gen->ip = start;
    genGotoIf(gen, dest);
    gen->ip = next;
}


void genGoFromToIfNot(CodeGen *gen, int start, int dest)
{
    int next = gen->ip;
    gen->ip = start;
    genGotoIfNot(gen, dest);
    gen->ip = next;
}


void genIfCondEpilog(CodeGen *gen)
{
    genSavePos(gen);
    genNop(gen);                                            // Goto "else" block start / statement end (stub)
}


void genIfEpilog(CodeGen *gen)
{
    genGoFromToIfNot(gen, genRestorePos(gen), gen->ip);     // Goto end of "if" block (fixup)
}


void genElseProlog(CodeGen *gen)
{
    genGoFromToIfNot(gen, genRestorePos(gen), gen->ip + 1); // Goto "else" block start (fixup)
    genSavePos(gen);
    genNop(gen);                                            // Goto statement end (stub)
}


void genIfElseEpilog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->ip);          // Goto statement end (fixup)
}


void genSwitchCondEpilog(CodeGen *gen)
{
    genPopReg(gen, VM_REG_COMMON_0);                        // Save switch expression
    genPushIntConst(gen, 0);                                // Initialize comparison accumulator
    genPopReg(gen, VM_REG_COMMON_1);
}


void genCaseExprEpilog(CodeGen *gen, Const *constant)
{
    genPushReg(gen, VM_REG_COMMON_0);                       // Compare switch expression with case constant
    genPushIntConst(gen, constant->intVal);
    genBinary(gen, TOK_EQEQ, TYPE_INT, 0);

    genPushReg(gen, VM_REG_COMMON_1);                       // Update comparison accumulator
    genBinary(gen, TOK_OR, TYPE_BOOL, 0);
    genPopReg(gen, VM_REG_COMMON_1);
}


void genCaseBlockProlog(CodeGen *gen)
{
    genPushReg(gen, VM_REG_COMMON_1);                       // Push comparison accumulator
    genGotoIf(gen, gen->ip + 2);                            // Goto "case" block start
    genSavePos(gen);
    genNop(gen);                                            // Goto next "case" or "default" (stub)
}


void genCaseBlockEpilog(CodeGen *gen)
{
    genGoFromTo(gen, genRestorePos(gen), gen->ip + 1);      // Goto next "case" or "default" (fixup)
    genSavePos(gen);
    genNop(gen);                                            // Goto "switch" end (stub)
}


void genSwitchEpilog(CodeGen *gen, int numCases)
{
    for (int i = 0; i < numCases; i++)
        genGoFromTo(gen, genRestorePos(gen), gen->ip);      // Goto "switch" end (fixup)
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
    genGoFromToIfNot(gen, genRestorePos(gen), gen->ip + 1); // Goto statement end (fixup)
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
    int condEpilog = genRestorePos(gen);
    int condProlog = genRestorePos(gen);

    genGoto(gen, condProlog);                               // Goto condition
    genGoFromToIf(gen, condEpilog, gen->ip);                // Goto post-statement end (fixup)

    gen->stack[++gen->top] = condEpilog;                    // Place to stack again
}


void genForEpilog(CodeGen *gen)
{
    int condEpilog = genRestorePos(gen);

    genGoto(gen, condEpilog + 2);                           // Goto post-statement (fixup)
    genGoFromTo(gen, condEpilog + 1, gen->ip);              // Goto statement end (fixup)
}


// a && b ==   a  ? b : a
// a || b == (!a) ? b : a
void genShortCircuitProlog(CodeGen *gen)
{
    genDup(gen);
    genSavePos(gen);
    genNop(gen);                                            // Goto expression end (stub)
    genPop(gen);
}


void genShortCircuitEpilog(CodeGen *gen, TokenKind op)
{
    if (op == TOK_ANDAND)
        genGoFromToIfNot(gen, genRestorePos(gen), gen->ip); // Goto expression end (fixup)
    else
        genGoFromToIf(gen, genRestorePos(gen), gen->ip);    // Goto expression end (fixup)
}


void genEnterFrameStub(CodeGen *gen)
{
    genSavePos(gen);
    genNop(gen);
}


void genLeaveFrameFixup(CodeGen *gen, int localVarSlots)
{
    // Fixup enter stub
    int next = gen->ip;
    gen->ip = genRestorePos(gen);

    genEnterFrame(gen, localVarSlots);
    gen->ip = next;

    genLeaveFrame(gen);
}


void genEntryPoint(CodeGen *gen, int start)
{
    genGoFromTo(gen, start, gen->ip);
}


int genTryRemoveImmediateEntryPoint(CodeGen *gen)
{
    Instruction *prev = getPrevInstr(gen, 1);
    if (prev && prev->opcode == OP_PUSH && prev->inlineOpcode == OP_NOP)
    {
        int entry = prev->operand.intVal;
        genRemoveInstr(gen);
        return entry;
    }
    return -1;
}


void genGotosProlog(CodeGen *gen, Gotos *gotos, int block)
{
    gotos->numGotos = 0;
    gotos->block = block;
}


void genGotosAddStub(CodeGen *gen, Gotos *gotos)
{
    if (gotos->numGotos >= MAX_GOTOS)
        gen->error->handler(gen->error->context, "To many break/continue/return statements");

    gotos->start[gotos->numGotos++] = gen->ip;
    genNop(gen);                                            // Goto block/function end (stub)
}


void genGotosEpilog(CodeGen *gen, Gotos *gotos)
{
    for (int i = 0; i < gotos->numGotos; i++)
        genGoFromTo(gen, gotos->start[i], gen->ip);         // Goto block/function end (fixup)
}


void genCopyResultToTempVar(CodeGen *gen, Type *type, int offset)
{
    genDup(gen);
    genPushLocalPtr(gen, offset);
    genSwapAssign(gen, type->kind, typeSizeNoCheck(type));

    genNotify(gen, GEN_NOTIFICATION_COPY_RESULT_TO_TEMP_VAR);
}


int genTryRemoveCopyResultToTempVar(CodeGen *gen)
{
    if (!genJustNotified(gen, GEN_NOTIFICATION_COPY_RESULT_TO_TEMP_VAR))
        return 0;

    Instruction *prev = getPrevInstr(gen, 1), *prev2 = getPrevInstr(gen, 2), *prev3 = getPrevInstr(gen, 3);

    if (prev3 && prev3->opcode == OP_DUP            && prev3->inlineOpcode == OP_NOP &&
        prev2 && prev2->opcode == OP_PUSH_LOCAL_PTR && prev2->inlineOpcode == OP_NOP &&
        prev  && prev->opcode  == OP_ASSIGN         && prev->inlineOpcode  == OP_SWAP)
    {
        int tempVarOffset = prev2->operand.intVal;
        genRemoveInstr(gen);
        genRemoveInstr(gen);
        genRemoveInstr(gen);
        return tempVarOffset;
    }

    return 0;
}



// Assembly output

int genAsm(CodeGen *gen, char *buf, int size)
{
    bool *jumpFrom = calloc(gen->capacity + 1, 1);
    bool *jumpTo = calloc(gen->capacity + 1, 1);

    int ip = 0;
    do
    {
        if (gen->code[ip].opcode == OP_GOTO || gen->code[ip].opcode == OP_GOTO_IF || gen->code[ip].opcode == OP_GOTO_IF_NOT)
        {
            jumpFrom[ip] = true;
            jumpTo[(int)gen->code[ip].operand.intVal] = true;
        }
    } while (gen->code[ip++].opcode != OP_HALT);

    ip = 0;
    int chars = 0;
    do
    {
        if (ip == 0 || gen->debugPerInstr[ip].fileName != gen->debugPerInstr[ip - 1].fileName)
            chars += snprintf(buf + chars, nonneg(size - chars), "\nModule: %s\n\n", gen->debugPerInstr[ip].fileName);

        if (gen->code[ip].opcode == OP_ENTER_FRAME)
            chars += snprintf(buf + chars, nonneg(size - chars), "\nFunction: %s\n\n", gen->debugPerInstr[ip].fnName);

        chars += vmAsm(ip, gen->code, gen->debugPerInstr, buf + chars, nonneg(size - chars));
        chars += snprintf(buf + chars, nonneg(size - chars), "\n");

        if (gen->code[ip].opcode == OP_RETURN || jumpFrom[ip] || jumpTo[ip + 1])
            chars += snprintf(buf + chars, nonneg(size - chars), "\n");

    } while (gen->code[ip++].opcode != OP_HALT);

    free(jumpFrom);
    free(jumpTo);

    return chars;
}

