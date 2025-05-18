#ifndef UMKA_DECL_H_INCLUDED
#define UMKA_DECL_H_INCLUDED

#include "umka_compiler.h"


const Type *parseType(Compiler *comp, const Ident *ident);
void parseShortVarDecl(Compiler *comp);
void parseDecl(Compiler *comp);
void parseProgram(Compiler *comp);


#endif // UMKA_DECL_H_INCLUDED
