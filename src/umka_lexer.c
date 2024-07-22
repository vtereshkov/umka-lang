#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "umka_common.h"
#include "umka_lexer.h"


static const char *spelling [] =
{
    "nothing",

    // Keywords
    "break",
    "case",
    "const",
    "continue",
    "default",
    "else",
    "enum",
    "fn",
    "for",
    "import",
    "interface",
    "if",
    "in",
    "map",
    "return",
    "str",
    "struct",
    "switch",
    "type",
    "var",
    "weak",

    // Operators
    "+",
    "-",
    "*",
    "/",
    "%",
    "&",
    "|",
    "~",
    "<<",
    ">>",
    "+=",
    "-=",
    "*=",
    "/=",
    "%=",
    "&=",
    "|=",
    "~=",
    "<<=",
    ">>=",
    "&&",
    "||",
    "++",
    "--",
    "==",
    "<",
    ">",
    "=",
    "?",
    "!",
    "!=",
    "<=",
    ">=",
    ":=",
    "(",
    ")",
    "[",
    "]",
    "{",
    "}",
    "^",
    ",",
    ";",
    ":",
    "::",
    ".",
    "..",

    // Other tokens
    "identifier",
    "integer number",
    "real number",
    "character",
    "string",

    "end of line",
    "end of line",
    "end of file"
};


enum
{
    NUM_KEYWORDS = TOK_WEAK - TOK_BREAK + 1
};


static unsigned int keywordHash[NUM_KEYWORDS];


int lexInit(Lexer *lex, Storage *storage, DebugInfo *debug, const char *fileName, const char *sourceString, Error *error)
{
    // Fill keyword hashes
    for (int i = 0; i < NUM_KEYWORDS; i++)
        keywordHash[i] = hash(spelling[TOK_BREAK + i]);

    // Initialize lexer
    errno = 0;

    lex->error = error;
    lex->hasSourceString = false;
    lex->buf = NULL;
    int bufLen = 0;

    if (sourceString)
    {
        // Read source from a string buffer
        lex->hasSourceString = true;

        bufLen = strlen(sourceString);
        lex->buf = malloc(bufLen + 1);
        strcpy(lex->buf, sourceString);
        lex->buf[bufLen] = 0;
    }
    else
    {
        // Read source from a file
        FILE *file = fopen(fileName, "rb");

        if (!file)
        {
            lex->error->handler(lex->error->context, "Cannot open file %s", fileName);
            return 0;
        }

        fseek(file, 0, SEEK_END);
        bufLen = ftell(file);
        rewind(file);

        lex->buf = malloc(bufLen + 1);

        if (fread(lex->buf, 1, bufLen, file) != bufLen)
        {
            lex->error->handler(lex->error->context, "Cannot read file %s", fileName);
            return 0;
        }

        lex->buf[bufLen] = 0;
        fclose(file);
    }

    lex->fileName = storageAdd(storage, strlen(fileName) + 1);
    strcpy(lex->fileName, fileName);

    lex->bufPos = 0;
    lex->line = 1;
    lex->pos = 1;
    lex->tok.kind = TOK_NONE;
    lex->tok.strVal = NULL;
    lex->tok.line = lex->line;
    lex->tok.pos = lex->pos;
    lex->prevTok = lex->tok;
    lex->storage = storage;
    lex->debug = debug;
    lex->debug->fileName = lex->fileName;
    lex->debug->fnName = "<unknown>";
    lex->debug->line = lex->line;

    return bufLen;
}


void lexFree(Lexer *lex)
{
    if (lex->buf)
    {
        free(lex->buf);
        lex->fileName = NULL;
        lex->buf = NULL;
    }
}


static char lexChar(Lexer *lex)
{
    char ch = lex->buf[lex->bufPos];
    if (ch)
    {
        lex->bufPos++;
        lex->pos++;
        if (ch == '\n')
        {
            lex->line++;
            lex->pos = 1;
        }
    }
    return lex->buf[lex->bufPos];
}


static char lexEscChar(Lexer *lex, bool *escaped)
{
    if (escaped) *escaped = false;
    char ch = lexChar(lex);

    if (ch == '\\')
    {
        if (escaped) *escaped = true;
        ch = lexChar(lex);

        switch (ch)
        {
            case '0': return '\0';
            case 'a': return '\a';
            case 'b': return '\b';
            case 'f': return '\f';
            case 'n': return '\n';
            case 'r': return '\r';
            case 't': return '\t';
            case 'v': return '\v';
            case 'x':
            {
                lexChar(lex);

                unsigned int hex = 0;
                int len = 0;
                const int items = sscanf(lex->buf + lex->bufPos, "%x%n", &hex, &len);

                if (items < 1 || hex > 0xFF)
                {
                    lex->error->handler(lex->error->context, "Illegal character code");
                    lex->tok.kind = TOK_NONE;
                    return 0;
                }

                lex->bufPos += len - 1;
                lex->pos += len - 1;
                return (char)hex;
            }
            default: return ch;
        }
    }
    return ch;
}


static void lexSingleLineComment(Lexer *lex)
{
    char ch = lexChar(lex);
    while (ch && ch != '\n')
        ch = lexChar(lex);
}


static void lexMultiLineComment(Lexer *lex)
{
    char ch = lexChar(lex);
    bool asteriskFound = false;

    while (ch && !(ch == '/' && asteriskFound))
    {
        asteriskFound = false;

        while (ch && ch != '*')
            ch = lexChar(lex);

        if (ch == '*') asteriskFound = true;
        ch = lexChar(lex);
    }

    ch = lexChar(lex);
}


static void lexSpacesAndComments(Lexer *lex)
{
    char ch = lex->buf[lex->bufPos];

    while (ch && (ch == ' ' || ch == '\t' || ch == '\r' || ch == '/'))
    {
        if (ch == '/')
        {
            ch = lexChar(lex);
            if (ch == '/')
                lexSingleLineComment(lex);
            else if (ch == '*')
                lexMultiLineComment(lex);
            else
            {
                // Discard ch
                lex->bufPos--;
                lex->pos--;
                break;
            }

            ch = lex->buf[lex->bufPos];
        }
        else
            ch = lexChar(lex);
    } // while
}


static void lexKeywordOrIdent(Lexer *lex)
{
    lex->tok.kind = TOK_NONE;
    char ch = lex->buf[lex->bufPos];
    int len = 0;

    do
    {
        lex->tok.name[len++] = ch;
        ch = lexChar(lex);

        if (len > MAX_IDENT_LEN)
        {
            lex->error->handler(lex->error->context, "Identifier is too long");
            lex->tok.kind = TOK_NONE;
            return;
        }
    } while (((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||  ch == '_'));

    lex->tok.name[len] = 0;
    lex->tok.hash = hash(lex->tok.name);

    // Search for a keyword
    for (int i = 0; i < NUM_KEYWORDS; i++)
        if (lex->tok.hash == keywordHash[i] && strcmp(lex->tok.name, spelling[TOK_BREAK + i]) == 0)
        {
            lex->tok.kind = TOK_BREAK + i;
            break;
        }

    if (lex->tok.kind == TOK_NONE)
        lex->tok.kind = TOK_IDENT;
}


static void lexOperator(Lexer *lex)
{
    lex->tok.kind = TOK_NONE;
    char ch = lex->buf[lex->bufPos];

    switch (ch)
    {
        case '+':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_PLUSEQ;
                ch = lexChar(lex);
            }
            else if (ch == '+')
            {
                lex->tok.kind = TOK_PLUSPLUS;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_PLUS;
            break;
        }

        case '-':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_MINUSEQ;
                ch = lexChar(lex);
            }
            else if (ch == '-')
            {
                lex->tok.kind = TOK_MINUSMINUS;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_MINUS;
            break;
        }

        case '*':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_MULEQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_MUL;
            break;
        }

        case '/':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_DIVEQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_DIV;
            break;
        }

        case '%':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_MODEQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_MOD;
            break;
        }

        case '&':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_ANDEQ;
                ch = lexChar(lex);
            }
            else if (ch == '&')
            {
                lex->tok.kind = TOK_ANDAND;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_AND;
            break;
        }

        case '|':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_OREQ;
                ch = lexChar(lex);
            }
            else if (ch == '|')
            {
                lex->tok.kind = TOK_OROR;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_OR;
            break;
        }

        case '~':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_XOREQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_XOR;
            break;
        }

        case '<':
        {
            ch = lexChar(lex);
            if (ch == '<')
            {
                ch = lexChar(lex);
                if (ch == '=')
                {
                    lex->tok.kind = TOK_SHLEQ;
                    ch = lexChar(lex);
                }
                else
                    lex->tok.kind = TOK_SHL;
            }
            else if (ch == '=')
            {
                lex->tok.kind = TOK_LESSEQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_LESS;
            break;
        }

        case '>':
        {
            ch = lexChar(lex);
            if (ch == '>')
            {
                ch = lexChar(lex);
                if (ch == '=')
                {
                    lex->tok.kind = TOK_SHREQ;
                    ch = lexChar(lex);
                }
                else
                    lex->tok.kind = TOK_SHR;
            }
            else if (ch == '=')
            {
                lex->tok.kind = TOK_GREATEREQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_GREATER;
            break;
        }

        case '=':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_EQEQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_EQ;
            break;
        }

        case '?':
        {
            lex->tok.kind = TOK_QUESTION;
            ch = lexChar(lex);
            break;
        }

        case '!':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_NOTEQ;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_NOT;
            break;
        }

        case '(':
        {
            lex->tok.kind = TOK_LPAR;
            ch = lexChar(lex);
            break;
        }

        case ')':
        {
            lex->tok.kind = TOK_RPAR;
            ch = lexChar(lex);
            break;
        }

        case '[':
        {
            lex->tok.kind = TOK_LBRACKET;
            ch = lexChar(lex);
            break;
        }

        case ']':
        {
            lex->tok.kind = TOK_RBRACKET;
            ch = lexChar(lex);
            break;
        }

        case '{':
        {
            lex->tok.kind = TOK_LBRACE;
            ch = lexChar(lex);
            break;
        }

        case '}':
        {
            lex->tok.kind = TOK_RBRACE;
            ch = lexChar(lex);
            break;
        }

        case '^':
        {
            lex->tok.kind = TOK_CARET;
            ch = lexChar(lex);
            break;
        }

        case ',':
        {
            lex->tok.kind = TOK_COMMA;
            ch = lexChar(lex);
            break;
        }

        case ';':
        {
            lex->tok.kind = TOK_SEMICOLON;
            ch = lexChar(lex);
            break;
        }

        case ':':
        {
            ch = lexChar(lex);
            if (ch == '=')
            {
                lex->tok.kind = TOK_COLONEQ;
                ch = lexChar(lex);
            }
            else if (ch == ':')
            {
                lex->tok.kind = TOK_COLONCOLON;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_COLON;
            break;
        }

        case '.':
        {
            ch = lexChar(lex);
            if (ch == '.')
            {
                lex->tok.kind = TOK_ELLIPSIS;
                ch = lexChar(lex);
            }
            else
                lex->tok.kind = TOK_PERIOD;
            break;
        }

        case '\n':
        {
            lex->tok.kind = TOK_EOLN;
            ch = lexChar(lex);
            break;
        }
    } // switch
}


static void lexNumber(Lexer *lex)
{
    lex->tok.kind = TOK_NONE;
    char ch = lex->buf[lex->bufPos];
    char *tail;

    // Integer number
    lex->tok.kind = TOK_INTNUMBER;
    lex->tok.uintVal = strtoull(lex->buf + lex->bufPos, &tail, 0);

    if (errno == ERANGE)
        lex->error->handler(lex->error->context, "Number is too large");

    if (tail == lex->buf + lex->bufPos && ch != '.')
    {
        lex->tok.kind = TOK_NONE;
        return;
    }

    // Real number
    if (tail[0] == '.' || tail[0] == 'E' || tail[0] == 'e')
    {
        lex->tok.kind = TOK_REALNUMBER;
        lex->tok.realVal = strtod(lex->buf + lex->bufPos, &tail);

        if (lex->tok.realVal == HUGE_VAL)
            lex->error->handler(lex->error->context, "Number is too large");

        if (tail == lex->buf + lex->bufPos)
        {
            lex->tok.kind = TOK_NONE;
            return;
        }
    }

    int len = tail - (lex->buf + lex->bufPos);
    lex->bufPos += len;
    lex->pos += len;
}


static void lexCharLiteral(Lexer *lex)
{
    lex->tok.kind = TOK_CHARLITERAL;
    lex->tok.intVal = lexEscChar(lex, NULL);

    const char ch = lexChar(lex);
    if (ch != '\'')
    {
        lex->error->handler(lex->error->context, "Invalid character literal");
        lex->tok.kind = TOK_NONE;
    }
    lexChar(lex);
}


static int lexStrLiteralAndGetSize(Lexer *lex)
{
    lex->tok.kind = TOK_STRLITERAL;

    int size = 0;
    bool escaped = false;
    char ch = lexEscChar(lex, &escaped);

    while (ch != '\"' || escaped)
    {
        if (ch == 0 || (ch == '\n' && !escaped))
        {
            lex->error->handler(lex->error->context, "Unterminated string");
            lex->tok.kind = TOK_NONE;
            return 0;
        }

        if (lex->tok.strVal)
            lex->tok.strVal[size] = ch;
        size++;
        ch = lexEscChar(lex, &escaped);
    }

    if (lex->tok.strVal)
        lex->tok.strVal[size] = 0;
    size++;
    lexChar(lex);

    return size;
}


static void lexStrLiteral(Lexer *lex)
{
    Lexer lookaheadLex = *lex;
    lookaheadLex.tok.strVal = NULL;
    const int size = lexStrLiteralAndGetSize(&lookaheadLex);

    lex->tok.strVal = storageAddStr(lex->storage, size - 1);
    lexStrLiteralAndGetSize(lex);
}


static void lexNextWithEOLN(Lexer *lex)
{
    lexSpacesAndComments(lex);

    lex->tok.kind = TOK_NONE;
    lex->tok.line = lex->debug->line = lex->line;
    lex->tok.pos = lex->pos;

    char ch = lex->buf[lex->bufPos];
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_')
        lexKeywordOrIdent(lex);
    else if ((ch >= '0' && ch <= '9') || ch == '.')
        lexNumber(lex);
    else if (ch == '\'')
        lexCharLiteral(lex);
    else if (ch == '"')
        lexStrLiteral(lex);

    if (lex->tok.kind == TOK_NONE)
        lexOperator(lex);

    if (lex->tok.kind == TOK_NONE)
    {
        if (!lex->buf[lex->bufPos])
            lex->tok.kind = TOK_EOF;
        else
            lex->error->handler(lex->error->context, "Unexpected character or end of file");
    }
}


void lexNext(Lexer *lex)
{
    do
    {
        lexNextWithEOLN(lex);

        // Replace end of line with implicit semicolon
        if (lex->tok.kind == TOK_EOLN)
            if (lex->prevTok.kind == TOK_BREAK       ||
                lex->prevTok.kind == TOK_CONTINUE    ||
                lex->prevTok.kind == TOK_RETURN      ||
                lex->prevTok.kind == TOK_STR         ||
                lex->prevTok.kind == TOK_PLUSPLUS    ||
                lex->prevTok.kind == TOK_MINUSMINUS  ||
                lex->prevTok.kind == TOK_RPAR        ||
                lex->prevTok.kind == TOK_RBRACKET    ||
                lex->prevTok.kind == TOK_RBRACE      ||
                lex->prevTok.kind == TOK_CARET       ||
                lex->prevTok.kind == TOK_IDENT       ||
                lex->prevTok.kind == TOK_INTNUMBER   ||
                lex->prevTok.kind == TOK_REALNUMBER  ||
                lex->prevTok.kind == TOK_CHARLITERAL ||
                lex->prevTok.kind == TOK_STRLITERAL)
            {
               lex->tok.kind = TOK_IMPLICIT_SEMICOLON;
            }

        lex->prevTok = lex->tok;
    } while (lex->tok.kind == TOK_EOLN);
}


void lexNextForcedSemicolon(Lexer *lex)
{
    lexNextWithEOLN(lex);

    // Replace end of line with implicit semicolon
    if (lex->tok.kind == TOK_EOLN)
        lex->tok.kind = TOK_IMPLICIT_SEMICOLON;

    lex->prevTok = lex->tok;
}


bool lexCheck(Lexer *lex, TokenKind kind)
{
    bool res = lex->tok.kind == kind || (lex->tok.kind == TOK_IMPLICIT_SEMICOLON && kind == TOK_SEMICOLON);
    if (!res)
        lex->error->handler(lex->error->context, "%s expected but %s found", lexSpelling(kind), lexSpelling(lex->tok.kind));
    return res;
}


void lexEat(Lexer *lex, TokenKind kind)
{
    // Allow omitting semicolon before ")" or "}"
    if (!(kind == TOK_SEMICOLON && (lex->tok.kind == TOK_RPAR || lex->tok.kind == TOK_RBRACE)))
    {
        lexCheck(lex, kind);
        lexNext(lex);
    }
}


const char *lexSpelling(TokenKind kind)
{
    return spelling[kind];
}


TokenKind lexShortAssignment(TokenKind kind)
{
    // Full replacements for short assignment operators
    switch (kind)
    {
        case TOK_PLUSEQ:  return TOK_PLUS;
        case TOK_MINUSEQ: return TOK_MINUS;
        case TOK_MULEQ:   return TOK_MUL;
        case TOK_DIVEQ:   return TOK_DIV;
        case TOK_MODEQ:   return TOK_MOD;
        case TOK_ANDEQ:   return TOK_AND;
        case TOK_OREQ:    return TOK_OR;
        case TOK_XOREQ:   return TOK_XOR;
        case TOK_SHLEQ:   return TOK_SHL;
        case TOK_SHREQ:   return TOK_SHR;
        default:          return TOK_NONE;
    }

}





