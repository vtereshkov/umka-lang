#ifndef UMKA_CONST_H_INCLUDED
#define UMKA_CONST_H_INCLUDED

#include "umka_lexer.h"
#include "umka_vm.h"


typedef struct
{
    Error *error;
} Consts;


typedef struct
{
    Storage *storage;
    Const *data;
    const Type *type;
    int len, capacity;
} ConstArray;


void constInit(Consts *consts, Error *error);
void constZero(void *lhs, int size);
bool constDeref(const Consts *consts, Const *constant, TypeKind typeKind);
bool constAssign(const Consts *consts, void *lhs, const Const *rhs, TypeKind typeKind, int size);
int64_t constCompare(const Consts *consts, const Const *lhs, const Const *rhs, const Type *type);

void constUnary(const Consts *consts, Const *arg, TokenKind op, const Type *type);
void constBinary(const Consts *consts, Const *lhs, const Const *rhs, TokenKind op, const Type *type);
void constCallBuiltin(const Consts *consts, Const *arg, const Const *arg2, TypeKind argTypeKind, BuiltinFunc builtinVal);

void constArrayAlloc(ConstArray *array, Storage *storage, const Type *type);
void constArrayAppend(ConstArray *array, Const val);
int constArrayFind(const Consts *consts, const ConstArray *array, Const val);
int constArrayFindEquivalentType(const Consts *consts, const ConstArray *array, Const val);
void constArrayFree(ConstArray *array);


#endif // UMKA_CONST_H_INCLUDED
