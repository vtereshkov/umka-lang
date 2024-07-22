#ifndef UMKA_LEXER_H_INCLUDED
#define UMKA_LEXER_H_INCLUDED

#include "umka_common.h"


typedef enum
{
    TOK_NONE,

    // Keywords
    TOK_BREAK,
    TOK_CASE,
    TOK_CONST,
    TOK_CONTINUE,
    TOK_DEFAULT,
    TOK_ELSE,
    TOK_ENUM,
    TOK_FN,
    TOK_FOR,
    TOK_IMPORT,
    TOK_INTERFACE,
    TOK_IF,
    TOK_IN,
    TOK_MAP,
    TOK_RETURN,
    TOK_STR,
    TOK_STRUCT,
    TOK_SWITCH,
    TOK_TYPE,
    TOK_VAR,
    TOK_WEAK,

    // Operators
    TOK_PLUS,
    TOK_MINUS,
    TOK_MUL,
    TOK_DIV,
    TOK_MOD,
    TOK_AND,
    TOK_OR,
    TOK_XOR,
    TOK_SHL,
    TOK_SHR,
    TOK_PLUSEQ,
    TOK_MINUSEQ,
    TOK_MULEQ,
    TOK_DIVEQ,
    TOK_MODEQ,
    TOK_ANDEQ,
    TOK_OREQ,
    TOK_XOREQ,
    TOK_SHLEQ,
    TOK_SHREQ,
    TOK_ANDAND,
    TOK_OROR,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_EQEQ,
    TOK_LESS,
    TOK_GREATER,
    TOK_EQ,
    TOK_QUESTION,
    TOK_NOT,
    TOK_NOTEQ,
    TOK_LESSEQ,
    TOK_GREATEREQ,
    TOK_COLONEQ,
    TOK_LPAR,
    TOK_RPAR,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_CARET,
    TOK_COMMA,
    TOK_SEMICOLON,
    TOK_COLON,
    TOK_COLONCOLON,
    TOK_PERIOD,
    TOK_ELLIPSIS,

    // Other tokens
    TOK_IDENT,
    TOK_INTNUMBER,
    TOK_REALNUMBER,
    TOK_CHARLITERAL,
    TOK_STRLITERAL,

    TOK_IMPLICIT_SEMICOLON,
    TOK_EOLN,
    TOK_EOF
} TokenKind;


typedef char IdentName[MAX_IDENT_LEN + 1];


typedef struct
{
    TokenKind kind;
    union
    {
        struct
        {
            IdentName name;
            unsigned int hash;
        };
        int64_t intVal;
        uint64_t uintVal;
        double realVal;
        char *strVal;
    };
    int line, pos;
} Token;


typedef struct
{
    char *fileName;
    bool hasSourceString;
    char *buf;
    int bufPos, line, pos;
    Token tok, prevTok;
    Storage *storage;
    DebugInfo *debug;
    Error *error;
} Lexer;


int lexInit(Lexer *lex, Storage *storage, DebugInfo *debug, const char *fileName, const char *sourceString, Error *error);
void lexFree(Lexer *lex);
void lexNext(Lexer *lex);
void lexNextForcedSemicolon(Lexer *lex);
bool lexCheck(Lexer *lex, TokenKind kind);
void lexEat(Lexer *lex, TokenKind kind);
const char *lexSpelling(TokenKind kind);
TokenKind lexShortAssignment(TokenKind kind);


#endif // UMKA_LEXER_H_INCLUDED
