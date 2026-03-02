#include "mar/lexer.h"
#include "mar/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const struct { const char *word; TokenKind kind; } KEYWORDS[] = {
    /* types */
    {"int",     TOK_INT},
    {"float",   TOK_FLOAT},
    {"char",    TOK_CHAR},
    {"bool",    TOK_BOOL},
    {"void",    TOK_VOID},
    {"string",  TOK_STRING},    /* FEATURE 1 */
    /* control */
    {"if",      TOK_IF},
    {"else",    TOK_ELSE},
    {"while",   TOK_WHILE},
    {"for",     TOK_FOR},
    {"in",      TOK_IN},
    {"range",   TOK_RANGE},
    {"switch",  TOK_SWITCH},
    {"case",    TOK_CASE},
    {"default", TOK_DEFAULT},
    {"return",  TOK_RETURN},
    {"break",   TOK_BREAK},
    /* builtins */
    {"print",   TOK_PRINT},
    {"take",    TOK_TAKE},
    {"len",     TOK_LEN},       /* FEATURE 1 */
    /* OOP */
    {"class",   TOK_CLASS},
    {"new",     TOK_NEW},
    {"extends", TOK_EXTENDS},   /* FEATURE 4 */
    /* literals */
    {"true",    TOK_TRUE},
    {"false",   TOK_FALSE},
    {"null",    TOK_NULL},      /* FEATURE 5 */
    /* module */
    {"import",  TOK_IMPORT},    /* FEATURE 2 */
    {NULL, 0}
};

Lexer *lexer_create(const char *src, const char *filename, ErrorCtx *ec) {
    Lexer *l   = calloc(1, sizeof(Lexer));
    l->src      = src;
    l->filename = filename;
    l->errors   = ec;
    l->line     = 1;
    l->col      = 1;
    l->token_cap = 2048;
    l->tokens    = malloc(sizeof(Token) * l->token_cap);
    return l;
}

static char peek_ch(Lexer *l)   { return l->src[l->pos]; }
static char peek2_ch(Lexer *l)  { return l->src[l->pos + 1]; }
static char advance_ch(Lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; } else l->col++;
    return c;
}

static void push(Lexer *l, TokenKind k, char *val, int line, int col) {
    if (l->token_count >= l->token_cap) {
        l->token_cap *= 2;
        l->tokens = realloc(l->tokens, sizeof(Token) * l->token_cap);
    }
    Token *t = &l->tokens[l->token_count++];
    t->kind  = k;
    t->value = val ? MAR_STRDUP(val) : NULL;
    t->line  = line;
    t->col   = col;
    t->file  = l->filename;
}

static void skip_whitespace(Lexer *l) {
    while (peek_ch(l) && (peek_ch(l)==' ' || peek_ch(l)=='\t' ||
                          peek_ch(l)=='\r' || peek_ch(l)=='\n'))
        advance_ch(l);
}

static void skip_line_comment(Lexer *l) {
    while (peek_ch(l) && peek_ch(l) != '\n') advance_ch(l);
}

static void skip_block_comment(Lexer *l) {
    while (peek_ch(l)) {
        if (peek_ch(l) == '*' && peek2_ch(l) == '/') {
            advance_ch(l); advance_ch(l); return;
        }
        advance_ch(l);
    }
}

Token *lexer_tokenize(Lexer *l) {
    while (1) {
        skip_whitespace(l);
        if (!peek_ch(l)) break;
        int  line = l->line, col = l->col;
        char c    = peek_ch(l);

        /* comments */
        if (c == '/' && peek2_ch(l) == '/') {
            advance_ch(l); advance_ch(l);
            skip_line_comment(l);
            continue;
        }
        if (c == '/' && peek2_ch(l) == '*') {
            advance_ch(l); advance_ch(l);
            skip_block_comment(l);
            continue;
        }

        /* string literal */
        if (c == '"') {
            advance_ch(l);
            char buf[4096]; int bi = 0;
            while (peek_ch(l) && peek_ch(l) != '"') {
                if (peek_ch(l) == '\\') {
                    advance_ch(l);
                    char esc = advance_ch(l);
                    switch (esc) {
                        case 'n':  buf[bi++] = '\n'; break;
                        case 't':  buf[bi++] = '\t'; break;
                        case '\\': buf[bi++] = '\\'; break;
                        case '"':  buf[bi++] = '"';  break;
                        case '0':  buf[bi++] = '\0'; break;
                        default:   buf[bi++] = '\\'; buf[bi++] = esc; break;
                    }
                } else {
                    buf[bi++] = advance_ch(l);
                }
            }
            if (peek_ch(l) == '"') advance_ch(l);
            buf[bi] = '\0';
            push(l, TOK_STRING_LIT, buf, line, col);
            continue;
        }

        /* char literal */
        if (c == '\'') {
            advance_ch(l);
            char buf[8]; int bi = 0;
            if (peek_ch(l) == '\\') {
                advance_ch(l);
                buf[bi++] = '\\';
                buf[bi++] = advance_ch(l);
            } else {
                buf[bi++] = advance_ch(l);
            }
            if (peek_ch(l) == '\'') advance_ch(l);
            buf[bi] = '\0';
            push(l, TOK_CHAR_LIT, buf, line, col);
            continue;
        }

        /* numbers */
        if (isdigit(c)) {
            char buf[64]; int bi = 0;
            while (isdigit(peek_ch(l))) buf[bi++] = advance_ch(l);
            TokenKind k = TOK_INT_LIT;
            if (peek_ch(l) == '.') {
                buf[bi++] = advance_ch(l);
                while (isdigit(peek_ch(l))) buf[bi++] = advance_ch(l);
                k = TOK_FLOAT_LIT;
            }
            buf[bi] = '\0';
            push(l, k, buf, line, col);
            continue;
        }

        /* identifiers / keywords */
        if (isalpha(c) || c == '_') {
            char buf[256]; int bi = 0;
            while (isalnum(peek_ch(l)) || peek_ch(l) == '_')
                buf[bi++] = advance_ch(l);
            buf[bi] = '\0';
            TokenKind k = TOK_IDENT;
            for (int i = 0; KEYWORDS[i].word; i++) {
                if (strcmp(buf, KEYWORDS[i].word) == 0) {
                    k = KEYWORDS[i].kind;
                    break;
                }
            }
            push(l, k, buf, line, col);
            continue;
        }

        /* consume current char */
        advance_ch(l);
        char n = peek_ch(l);

        /* two-char operators */
        if (c=='='&&n=='=') { advance_ch(l); push(l,TOK_EQ,NULL,line,col); continue; }
        if (c=='!'&&n=='=') { advance_ch(l); push(l,TOK_NEQ,NULL,line,col); continue; }
        if (c=='<'&&n=='=') { advance_ch(l); push(l,TOK_LTE,NULL,line,col); continue; }
        if (c=='>'&&n=='=') { advance_ch(l); push(l,TOK_GTE,NULL,line,col); continue; }
        if (c=='&'&&n=='&') { advance_ch(l); push(l,TOK_AND,NULL,line,col); continue; }
        if (c=='|'&&n=='|') { advance_ch(l); push(l,TOK_OR,NULL,line,col); continue; }
        if (c=='+'&&n=='=') { advance_ch(l); push(l,TOK_PLUS_ASSIGN,NULL,line,col); continue; }
        if (c=='-'&&n=='=') { advance_ch(l); push(l,TOK_MINUS_ASSIGN,NULL,line,col); continue; }
        if (c=='*'&&n=='=') { advance_ch(l); push(l,TOK_STAR_ASSIGN,NULL,line,col); continue; }
        if (c=='/'&&n=='=') { advance_ch(l); push(l,TOK_SLASH_ASSIGN,NULL,line,col); continue; }
        if (c=='%'&&n=='=') { advance_ch(l); push(l,TOK_PERCENT_ASSIGN,NULL,line,col); continue; }

        /* single-char tokens */
        switch (c) {
            case '+': push(l, TOK_PLUS,     NULL, line, col); break;
            case '-': push(l, TOK_MINUS,    NULL, line, col); break;
            case '*': push(l, TOK_STAR,     NULL, line, col); break;
            case '/': push(l, TOK_SLASH,    NULL, line, col); break;
            case '%': push(l, TOK_PERCENT,  NULL, line, col); break;
            case '=': push(l, TOK_ASSIGN,   NULL, line, col); break;
            case '<': push(l, TOK_LT,       NULL, line, col); break;
            case '>': push(l, TOK_GT,       NULL, line, col); break;
            case '!': push(l, TOK_NOT,      NULL, line, col); break;
            case '(': push(l, TOK_LPAREN,   NULL, line, col); break;
            case ')': push(l, TOK_RPAREN,   NULL, line, col); break;
            case '{': push(l, TOK_LBRACE,   NULL, line, col); break;
            case '}': push(l, TOK_RBRACE,   NULL, line, col); break;
            case '[': push(l, TOK_LBRACKET, NULL, line, col); break;
            case ']': push(l, TOK_RBRACKET, NULL, line, col); break;
            case ',': push(l, TOK_COMMA,    NULL, line, col); break;
            case ';': push(l, TOK_SEMICOLON,NULL, line, col); break;
            case ':': push(l, TOK_COLON,    NULL, line, col); break;
            case '&': push(l, TOK_AMPERSAND,NULL, line, col); break;
            case '.': push(l, TOK_DOT,      NULL, line, col); break;
            default: {
                SrcLoc loc = {line, col, l->filename};
                error_emit(l->errors, ERR_LEXER, loc,
                    "Unknown character '%c' (0x%02x)", c, (unsigned char)c);
                push(l, TOK_UNKNOWN, NULL, line, col);
            }
        }
    }
    push(l, TOK_EOF, NULL, l->line, l->col);
    return l->tokens;
}

const char *token_kind_str(TokenKind k) {
    switch (k) {
        case TOK_INT_LIT:    return "INT_LIT";
        case TOK_FLOAT_LIT:  return "FLOAT_LIT";
        case TOK_STRING_LIT: return "STRING_LIT";
        case TOK_CHAR_LIT:   return "CHAR_LIT";
        case TOK_IDENT:      return "IDENT";
        case TOK_INT:        return "int";
        case TOK_FLOAT:      return "float";
        case TOK_CHAR:       return "char";
        case TOK_BOOL:       return "bool";
        case TOK_VOID:       return "void";
        case TOK_STRING:     return "string";
        case TOK_IF:         return "if";
        case TOK_ELSE:       return "else";
        case TOK_WHILE:      return "while";
        case TOK_FOR:        return "for";
        case TOK_IN:         return "in";
        case TOK_RANGE:      return "range";
        case TOK_SWITCH:     return "switch";
        case TOK_CASE:       return "case";
        case TOK_DEFAULT:    return "default";
        case TOK_RETURN:     return "return";
        case TOK_BREAK:      return "break";
        case TOK_PRINT:      return "print";
        case TOK_TAKE:       return "take";
        case TOK_LEN:        return "len";
        case TOK_CLASS:      return "class";
        case TOK_NEW:        return "new";
        case TOK_EXTENDS:    return "extends";
        case TOK_NULL:       return "null";
        case TOK_IMPORT:     return "import";
        case TOK_TRUE:       return "true";
        case TOK_FALSE:      return "false";
        case TOK_COMMA:      return ",";
        case TOK_SEMICOLON:  return ";";
        case TOK_COLON:      return ":";
        case TOK_DOT:        return ".";
        case TOK_LPAREN:     return "(";
        case TOK_RPAREN:     return ")";
        case TOK_LBRACE:     return "{";
        case TOK_RBRACE:     return "}";
        case TOK_ASSIGN:     return "=";
        case TOK_EQ:         return "==";
        case TOK_EOF:        return "EOF";
        default:             return "TOKEN";
    }
}