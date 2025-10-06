#ifndef UMKA_DECL_H_INCLUDED
#define UMKA_DECL_H_INCLUDED

#include "umka_compiler.h"


const Type *parseType(Umka *umka, const Ident *ident);
void parseShortVarDecl(Umka *umka);
void parseDecl(Umka *umka);
void parseProgram(Umka *umka);


#endif // UMKA_DECL_H_INCLUDED
