#ifndef UMKA_STMT_H_INCLUDED
#define UMKA_STMT_H_INCLUDED

#include "umka_compiler.h"


void parseAssignmentStmt(Compiler *comp, Type *type, void *initializedVarPtr);
void parseDeclAssignmentStmt(Compiler *comp, IdentName name, bool constExpr, bool exported);

void parseBlock(Compiler *comp, Ident *fn);


#endif // UMKA_STMT_H_INCLUDED
