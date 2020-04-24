#ifndef UMKA_DECL_H_INCLUDED
#define UMKA_DECL_H_INCLUDED

#include "umka_compiler.h"


Type *parseType(Compiler *comp, Ident *ident);
void parseShortVarDecl(Compiler *comp);
void parseDecl(Compiler *comp);
void parseProgram(Compiler *comp);


#endif // UMKA_DECL_H_INCLUDED
