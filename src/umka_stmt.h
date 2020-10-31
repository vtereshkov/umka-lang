#ifndef UMKA_STMT_H_INCLUDED
#define UMKA_STMT_H_INCLUDED

#include "umka_compiler.h"


void doResolveExtern(Compiler *comp);

void parseAssignmentStmt(Compiler *comp, Type *type, Const *varPtrConstList);
void parseDeclAssignmentStmt(Compiler *comp, IdentName *names, bool *exported, int num, bool constExpr);

void parseFnBlock(Compiler *comp, Ident *fn);
void parseFnPrototype(Compiler *comp, Ident *fn);


#endif // UMKA_STMT_H_INCLUDED
