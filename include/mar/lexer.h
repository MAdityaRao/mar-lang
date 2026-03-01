#ifndef MAR_LEXER_H
#define MAR_LEXER_H

#include "mar/error.h"
#include <stdbool.h>

typedef enum {
    TOK_INT_LIT, TOK_FLOAT_LIT, TOK_CHAR_LIT, TOK_STRING_LIT,
    TOK_TRUE, TOK_FALSE,
    TOK_IDENT,
    TOK_INT, TOK_FLOAT, TOK_CHAR, TOK_BOOL, TOK_VOID,
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_IN, TOK_RANGE,
    TOK_SWITCH, TOK_CASE, TOK_DEFAULT,
    TOK_RETURN, TOK_BREAK,
    TOK_PRINT, TOK_TAKE,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN, TOK_PERCENT_ASSIGN,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_AMPERSAND,
    TOK_EOF, TOK_UNKNOWN,
    TOK_CLASS, TOK_NEW, TOK_DOT
} TokenKind;

typedef struct {
    TokenKind   kind;
    char       *value;
    int         line;
    int         col;
    const char *file;
} Token;

typedef struct {
    const char *src;
    int         pos;
    int         line;
    int         col;
    const char *filename;
    ErrorCtx   *errors;
    Token      *tokens;
    int         token_count;
    int         token_cap;
} Lexer;

Lexer *lexer_create(const char *src, const char *filename, ErrorCtx *ec);
Token *lexer_tokenize(Lexer *l);
const char *token_kind_str(TokenKind k);

#endif
