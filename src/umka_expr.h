#ifndef UMKA_EXPR_H_INCLUDED
#define UMKA_EXPR_H_INCLUDED

#include "umka_compiler.h"


void doPushConst(Compiler *comp, Type *type, Const *constant);
void doPushVarPtr(Compiler *comp, Ident *ident);
void doImplicitTypeConv(Compiler *comp, Type *dest, Type **src, Const *constant, bool lhs);
void doApplyOperator(Compiler *comp, Type **type, Type **rightType, Const *constant, Const *rightConstant, TokenKind op, bool apply, bool convertLhs);

void parseDesignator(Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall);
void parseExpr(Compiler *comp, Type **type, Const *constant);


#endif // UMKA_EXPR_H_INCLUDED
