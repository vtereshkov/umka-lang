#ifndef UMKA_EXPR_H_INCLUDED
#define UMKA_EXPR_H_INCLUDED

#include "umka_compiler.h"


void doPushConst                    (Compiler *comp, Type *type, Const *constant);
void doPushVarPtr                   (Compiler *comp, Ident *ident);
void doCopyResultToTempVar          (Compiler *comp, Type *type);
bool doTryRemoveCopyResultToTempVar (Compiler *comp);
void doImplicitTypeConv             (Compiler *comp, Type *dest, Type **src, Const *constant);
void doAssertImplicitTypeConv       (Compiler *comp, Type *dest, Type **src, Const *constant);
void doExplicitTypeConv             (Compiler *comp, Type *dest, Type **src, Const *constant);
void doApplyOperator                (Compiler *comp, Type **type, Type **rightType, Const *constant, Const *rightConstant, TokenKind op, bool apply, bool convertLhs);

Ident *parseQualIdent               (Compiler *comp);
void parseDesignatorList            (Compiler *comp, Type **type, Const *constant, bool *isVar, bool *isCall);
void parseExpr                      (Compiler *comp, Type **type, Const *constant);
void parseExprList                  (Compiler *comp, Type **type, Const *constant);


#endif // UMKA_EXPR_H_INCLUDED
