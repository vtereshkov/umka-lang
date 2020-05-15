#ifndef UMKA_CONST_H_INCLUDED
#define UMKA_CONST_H_INCLUDED

#include "umka_lexer.h"
#include "umka_vm.h"


typedef struct
{
    ErrorFunc error;
} Consts;


void constInit(Consts *consts, ErrorFunc error);
void constFree(Consts *consts);
void constZero(void *lhs, int size);
void constAssign(Consts *consts, void *lhs, Const *rhs, TypeKind typeKind, int size);
void constUnary(Consts *consts, Const *arg, TokenKind tokKind, TypeKind typeKind);
void constBinary(Consts *consts, Const *lhs, const Const *rhs, TokenKind tokKind, TypeKind typeKind);
void constCallBuiltin(Consts *consts, Const *arg, BuiltinFunc builtinVal);


#endif // UMKA_CONST_H_INCLUDED
