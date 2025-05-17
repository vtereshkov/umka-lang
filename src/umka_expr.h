#ifndef UMKA_EXPR_H_INCLUDED
#define UMKA_EXPR_H_INCLUDED

#include "umka_compiler.h"


void doPushConst                    (Compiler *comp, const Type *type, const Const *constant);
void doPushVarPtr                   (Compiler *comp, Ident *ident);
void doCopyResultToTempVar          (Compiler *comp, const Type *type);
bool doTryRemoveCopyResultToTempVar (Compiler *comp);
void doImplicitTypeConv             (Compiler *comp, const Type *dest, const Type **src, Const *constant);
void doAssertImplicitTypeConv       (Compiler *comp, const Type *dest, const Type **src, Const *constant);
void doExplicitTypeConv             (Compiler *comp, const Type *dest, const Type **src, Const *constant);
void doApplyOperator                (Compiler *comp, const Type **type, const Type **rightType, Const *constant, Const *rightConstant, TokenKind op, bool apply, bool convertLhs);

Ident *parseQualIdent               (Compiler *comp);
void parseDesignatorList            (Compiler *comp, const Type **type, Const *constant, bool *isVar, bool *isCall, bool *isCompLit);
void parseExpr                      (Compiler *comp, const Type **type, Const *constant);
void parseExprList                  (Compiler *comp, const Type **type, Const *constant);


#endif // UMKA_EXPR_H_INCLUDED
