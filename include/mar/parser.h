#ifndef MAR_PARSER_H
#define MAR_PARSER_H

#include "mar/lexer.h"
#include "mar/ast.h"
#include "mar/error.h"

typedef struct {
    Token    *tokens;
    int       pos;
    ErrorCtx *errors;
} Parser;

Parser  *parser_create(Token *tokens, ErrorCtx *ec);
Program *parser_parse(Parser *p);

#endif
