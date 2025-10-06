#ifndef UMKA_EXPR_H_INCLUDED
#define UMKA_EXPR_H_INCLUDED

#include "umka_compiler.h"


void doPushConst                    (Umka *umka, const Type *type, const Const *constant);
void doPushVarPtr                   (Umka *umka, const Ident *ident);
void doCopyResultToTempVar          (Umka *umka, const Type *type);
bool doTryRemoveCopyResultToTempVar (Umka *umka);
void doImplicitTypeConv             (Umka *umka, const Type *dest, const Type **src, Const *constant);
void doAssertImplicitTypeConv       (Umka *umka, const Type *dest, const Type **src, Const *constant);
void doExplicitTypeConv             (Umka *umka, const Type *dest, const Type **src, Const *constant);
void doApplyOperator                (Umka *umka, const Type **type, const Type **rightType, Const *constant, Const *rightConstant, TokenKind op, bool apply, bool convertLhs);

const Ident *parseQualIdent         (Umka *umka);
void parseDesignatorList            (Umka *umka, const Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit);
void parseExpr                      (Umka *umka, const Type **type, Const *constant);
void parseExprList                  (Umka *umka, const Type **type, Const *constant);


#endif // UMKA_EXPR_H_INCLUDED
