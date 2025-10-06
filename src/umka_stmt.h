#ifndef UMKA_STMT_H_INCLUDED
#define UMKA_STMT_H_INCLUDED

#include "umka_compiler.h"

void doGarbageCollection(Umka *umka);
void doGarbageCollectionDownToBlock(Umka *umka, int block);

void doZeroVar(Umka *umka, const Ident *ident);
void doResolveExtern(Umka *umka);

void parseAssignmentStmt(Umka *umka, const Type *type, Const *varPtrConstList);
void parseDeclAssignmentStmt(Umka *umka, IdentName *names, const bool *exported, int num, bool constExpr);

void parseFnBlock(Umka *umka, Ident *fn, const Type *upvaluesStructType);
void parseFnPrototype(Umka *umka, Ident *fn);


#endif // UMKA_STMT_H_INCLUDED
