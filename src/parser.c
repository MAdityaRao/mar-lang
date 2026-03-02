#define _POSIX_C_SOURCE 200809L
#include "mar/parser.h"
#include "mar/arena.h"
#include "mar/lexer.h"
#include "mar/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ───────────────────────────────────────────────────────────────── */

Parser *parser_create(Token *tokens, ErrorCtx *ec) {
    Parser *p = calloc(1, sizeof(Parser));
    p->tokens = tokens;
    p->errors = ec;
    return p;
}

void program_merge(Program *dst, Program *src) {
    /* Merge functions */
    int new_fc = dst->func_count + src->func_count;
    FuncDecl **nf = malloc(sizeof(FuncDecl*) * (new_fc + 1));
    if (dst->func_count > 0 && dst->funcs)
        memcpy(nf, dst->funcs, sizeof(FuncDecl*) * dst->func_count);
    if (src->func_count > 0 && src->funcs)
        memcpy(nf + dst->func_count, src->funcs, sizeof(FuncDecl*) * src->func_count);
    dst->funcs = nf;
    dst->func_count = new_fc;
    /* Merge classes */
    int new_cc = dst->class_count + src->class_count;
    ClassDecl **nc = malloc(sizeof(ClassDecl*) * (new_cc + 1));
    if (dst->class_count > 0 && dst->classes)
        memcpy(nc, dst->classes, sizeof(ClassDecl*) * dst->class_count);
    if (src->class_count > 0 && src->classes)
        memcpy(nc + dst->class_count, src->classes, sizeof(ClassDecl*) * src->class_count);
    dst->classes = nc;
    dst->class_count = new_cc;
    /* Merge C includes */
    int new_ic = dst->c_include_count + src->c_include_count;
    char **ni = malloc(sizeof(char*) * (new_ic + 1));
    if (dst->c_include_count > 0 && dst->c_includes)
        memcpy(ni, dst->c_includes, sizeof(char*) * dst->c_include_count);
    if (src->c_include_count > 0 && src->c_includes)
        memcpy(ni + dst->c_include_count, src->c_includes, sizeof(char*) * src->c_include_count);
    dst->c_includes = ni;
    dst->c_include_count = new_ic;
}

static Token *peek(Parser *p)      { return &p->tokens[p->pos]; }
static Token *peek2(Parser *p)     { return &p->tokens[p->pos + 1]; }
static Token *peek3(Parser *p)     { return &p->tokens[p->pos + 2]; }
static Token *advance(Parser *p)   {
    Token *t = &p->tokens[p->pos];
    if (t->kind != TOK_EOF) p->pos++;
    return t;
}
static bool check(Parser *p, TokenKind k) { return peek(p)->kind == k; }
static bool match(Parser *p, TokenKind k) {
    if (check(p, k)) { advance(p); return true; }
    return false;
}
static Token *expect(Parser *p, TokenKind k) {
    if (check(p, k)) return advance(p);
    SrcLoc loc = {peek(p)->line, peek(p)->col, peek(p)->file};
    error_emit(p->errors, ERR_PARSER, loc,
        "Expected '%s' but got '%s'",
        token_kind_str(k), token_kind_str(peek(p)->kind));
    return peek(p);
}
static SrcLoc loc_of(Token *t) { SrcLoc l = {t->line, t->col, t->file}; return l; }

/* ── Forward declarations ───────────────────────────────────────────────── */
static Stmt     *parse_stmt(Parser *p);
static Stmt     *parse_block(Parser *p);
static Expr     *parse_expr(Parser *p);
static MarType  *parse_type(Parser *p);
static FuncDecl *parse_func(Parser *p);
static Stmt     *parse_var_decl(Parser *p);
static Stmt     *parse_class(Parser *p);

/* ── Type token check ───────────────────────────────────────────────────── */
static bool is_type_token(TokenKind k) {
    return k == TOK_INT    || k == TOK_FLOAT  || k == TOK_CHAR  ||
           k == TOK_BOOL   || k == TOK_VOID   || k == TOK_STRING ||
           k == TOK_IDENT; /* user-defined class types */
}

/* ── Type parser ─────────────────────────────────────────────────────────── */
static MarType *parse_type(Parser *p) {
    MarType *t = NULL;
    if      (match(p, TOK_INT))    t = type_new(TY_INT);
    else if (match(p, TOK_FLOAT))  t = type_new(TY_FLOAT);
    else if (match(p, TOK_CHAR))   t = type_new(TY_CHAR);
    else if (match(p, TOK_BOOL))   t = type_new(TY_BOOL);
    else if (match(p, TOK_VOID))   t = type_new(TY_VOID);
    else if (match(p, TOK_STRING)) t = type_new(TY_STRING); /* FEATURE 1 */
    else if (check(p, TOK_IDENT)) {
        Token *name = advance(p);
        t = type_new(TY_UNKNOWN);
        t->name = MAR_STRDUP(name->value);
    } else {
        SrcLoc loc = {peek(p)->line, peek(p)->col, peek(p)->file};
        error_emit(p->errors, ERR_PARSER, loc,
            "Expected type, got '%s'", token_kind_str(peek(p)->kind));
        t = type_new(TY_UNKNOWN);
        advance(p);
    }
    /* array suffix: int[5] or string[] */
    if (check(p, TOK_LBRACKET)) {
        advance(p);
        int size = 0;
        if (check(p, TOK_INT_LIT)) {
            Token *sz = advance(p);
            size = atoi(sz->value);
        }
        expect(p, TOK_RBRACKET);
        t = type_array(t, size);
    }
    return t;
}

/* ── Expression parsers ──────────────────────────────────────────────────── */
static Operator tok_to_op(TokenKind k) {
    switch (k) {
        case TOK_PLUS:    return OP_ADD; case TOK_MINUS:   return OP_SUB;
        case TOK_STAR:    return OP_MUL; case TOK_SLASH:   return OP_DIV;
        case TOK_PERCENT: return OP_MOD; case TOK_EQ:      return OP_EQ;
        case TOK_NEQ:     return OP_NEQ; case TOK_LT:      return OP_LT;
        case TOK_GT:      return OP_GT;  case TOK_LTE:     return OP_LTE;
        case TOK_GTE:     return OP_GTE; case TOK_AND:     return OP_AND;
        case TOK_OR:      return OP_OR;  default:          return OP_ADD;
    }
}

/* Build a function call expression (used by parser for method calls too) */
static Expr *finish_call(Parser *p, Expr *callee_expr, SrcLoc loc) {
    /* callee_expr is an EXPR_IDENT or EXPR_MEMBER_ACCESS */
    expect(p, TOK_LPAREN);
    Expr **args = malloc(sizeof(Expr*) * 32);
    int argc = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        args[argc++] = parse_expr(p);
        if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA);
    }
    expect(p, TOK_RPAREN);
    Expr *e;
    if (callee_expr->kind == EXPR_MEMBER_ACCESS) {
        /* obj.method(args) → EXPR_METHOD_CALL */
        e = expr_new(EXPR_METHOD_CALL, loc);
        e->method_call.obj    = callee_expr->member.left;
        e->method_call.method = callee_expr->member.name;
        e->method_call.args   = MAR_ALLOC_N(Expr*, argc);
        memcpy(e->method_call.args, args, sizeof(Expr*) * argc);
        e->method_call.argc   = argc;
    } else {
        e = expr_new(EXPR_CALL, loc);
        e->call.callee = callee_expr->name;
        e->call.args   = MAR_ALLOC_N(Expr*, argc);
        memcpy(e->call.args, args, sizeof(Expr*) * argc);
        e->call.argc   = argc;
    }
    free(args);
    return e;
}

static Expr *parse_primary(Parser *p) {
    Token *t   = peek(p);
    SrcLoc loc = loc_of(t);
    /* FEATURE: Type casts disguised as function calls (e.g., char(a)) */
    if (t->kind == TOK_INT || t->kind == TOK_FLOAT || t->kind == TOK_CHAR) {
        Token *type_tok = advance(p);
        Expr *e = expr_new(EXPR_CALL, loc);
        e->call.callee = MAR_STRDUP(type_tok->value); /* e.g., "char" */
        expect(p, TOK_LPAREN);
        e->call.args = MAR_ALLOC_N(Expr*, 1);
        e->call.args[0] = parse_expr(p);
        e->call.argc = 1;
        expect(p, TOK_RPAREN);
        return e;
    }
    /* null literal — FEATURE 5 */
    if (t->kind == TOK_NULL) {
        advance(p);
        return expr_new(EXPR_NULL, loc);
    }

    /* new ClassName(args) */
    if (match(p, TOK_NEW)) {
        Token *cls_tok = expect(p, TOK_IDENT);
        Expr *e = expr_new(EXPR_NEW, loc);
        e->new_obj.class_name = MAR_STRDUP(cls_tok->value);
        expect(p, TOK_LPAREN);
        Expr **args = malloc(sizeof(Expr*) * 32);
        int argc = 0;
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            args[argc++] = parse_expr(p);
            if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA);
        }
        expect(p, TOK_RPAREN);
        e->new_obj.args = MAR_ALLOC_N(Expr*, argc);
        memcpy(e->new_obj.args, args, sizeof(Expr*) * argc);
        e->new_obj.argc = argc;
        free(args);
        return e;
    }

    /* len(expr) — FEATURE 1 */
    if (t->kind == TOK_LEN) {
        advance(p);
        expect(p, TOK_LPAREN);
        Expr *e = expr_new(EXPR_LEN, loc);
        e->len_expr.arg = parse_expr(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    /* integer literal */
    if (t->kind == TOK_INT_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_INT_LIT, loc);
        e->ival = atoll(t->value);
        return e;
    }

    /* float literal */
    if (t->kind == TOK_FLOAT_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_FLOAT_LIT, loc);
        e->fval = atof(t->value);
        return e;
    }

    /* string literal */
    if (t->kind == TOK_STRING_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_STRING_LIT, loc);
        e->sval = MAR_STRDUP(t->value);
        return e;
    }

    /* char literal */
    if (t->kind == TOK_CHAR_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_CHAR_LIT, loc);
        if (t->value[0] == '\\') {
            switch (t->value[1]) {
                case 'n':  e->cval = '\n'; break;
                case 't':  e->cval = '\t'; break;
                case '\\': e->cval = '\\'; break;
                case '\'': e->cval = '\''; break;
                case '0':  e->cval = '\0'; break;
                default:   e->cval = t->value[1]; break;
            }
        } else {
            e->cval = t->value[0];
        }
        return e;
    }

    /* bool literals */
    if (t->kind == TOK_TRUE)  { advance(p); Expr *e = expr_new(EXPR_BOOL_LIT, loc); e->bval = 1; return e; }
    if (t->kind == TOK_FALSE) { advance(p); Expr *e = expr_new(EXPR_BOOL_LIT, loc); e->bval = 0; return e; }

    /* identifier: variable, function call, member access, method call */
    if (t->kind == TOK_IDENT) {
        advance(p);
        Expr *left = expr_new(EXPR_IDENT, loc);
        left->name = MAR_STRDUP(t->value);

        /* Handle postfix: dot chains and calls */
        while (1) {
            if (match(p, TOK_DOT)) {
                Token *member = expect(p, TOK_IDENT);
                Expr *dot = expr_new(EXPR_MEMBER_ACCESS, loc_of(member));
                dot->member.left = left;
                dot->member.name = MAR_STRDUP(member->value);
                left = dot;
                /* check for method call after member access */
                if (check(p, TOK_LPAREN)) {
                    left = finish_call(p, left, loc);
                }
            } else if (check(p, TOK_LBRACKET)) {
                /* array index */
                advance(p);
                Expr *e = expr_new(EXPR_INDEX, loc);
                e->idx.array = left;
                e->idx.index = parse_expr(p);
                expect(p, TOK_RBRACKET);
                left = e;
            } else if (check(p, TOK_LPAREN) && left->kind == EXPR_IDENT) {
                /* function call */
                left = finish_call(p, left, loc);
            } else {
                break;
            }
        }
        return left;
    }

    /* & address-of (for take()) */
    if (t->kind == TOK_AMPERSAND) {
        advance(p);
        Expr *e = expr_new(EXPR_ADDR_OF, loc);
        e->addr.operand = parse_primary(p);
        return e;
    }

    /* parenthesized expression */
    if (t->kind == TOK_LPAREN) {
        advance(p);
        Expr *e = parse_expr(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    SrcLoc eloc = {peek(p)->line, peek(p)->col, peek(p)->file};
    error_emit(p->errors, ERR_PARSER, eloc,
        "Unexpected token '%s' in expression", token_kind_str(t->kind));
    advance(p);
    return expr_new(EXPR_INT_LIT, loc);
}

static Expr *parse_unary(Parser *p) {
    Token *t   = peek(p);
    SrcLoc loc = loc_of(t);
    if (t->kind == TOK_NOT) {
        advance(p);
        Expr *e = expr_new(EXPR_UNARY, loc);
        e->unary.op      = OP_NOT;
        e->unary.operand = parse_unary(p);
        return e;
    }
    if (t->kind == TOK_MINUS) {
        advance(p);
        Expr *e = expr_new(EXPR_UNARY, loc);
        e->unary.op      = OP_NEG;
        e->unary.operand = parse_unary(p);
        return e;
    }
    return parse_primary(p);
}

static Expr *parse_mul(Parser *p) {
    Expr *left = parse_unary(p);
    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        Token *op = advance(p);
        Expr *e   = expr_new(EXPR_BINARY, loc_of(op));
        e->binary.op    = tok_to_op(op->kind);
        e->binary.left  = left;
        e->binary.right = parse_unary(p);
        left = e;
    }
    return left;
}

/* FEATURE 6: string concatenation is handled here at the add level.
   We detect it in codegen based on operand types. */
static Expr *parse_add(Parser *p) {
    Expr *left = parse_mul(p);
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        Token *op = advance(p);
        Expr *e   = expr_new(EXPR_BINARY, loc_of(op));
        e->binary.op    = tok_to_op(op->kind);
        e->binary.left  = left;
        e->binary.right = parse_mul(p);
        left = e;
    }
    return left;
}

static Expr *parse_rel(Parser *p) {
    Expr *left = parse_add(p);
    while (check(p,TOK_LT)||check(p,TOK_GT)||check(p,TOK_LTE)||check(p,TOK_GTE)) {
        Token *op = advance(p);
        Expr *e   = expr_new(EXPR_BINARY, loc_of(op));
        e->binary.op    = tok_to_op(op->kind);
        e->binary.left  = left;
        e->binary.right = parse_add(p);
        left = e;
    }
    return left;
}

static Expr *parse_eq(Parser *p) {
    Expr *left = parse_rel(p);
    while (check(p, TOK_EQ) || check(p, TOK_NEQ)) {
        Token *op = advance(p);
        Expr *e   = expr_new(EXPR_BINARY, loc_of(op));
        e->binary.op    = tok_to_op(op->kind);
        e->binary.left  = left;
        e->binary.right = parse_rel(p);
        left = e;
    }
    return left;
}

static Expr *parse_and(Parser *p) {
    Expr *left = parse_eq(p);
    while (check(p, TOK_AND)) {
        Token *op = advance(p);
        Expr *e   = expr_new(EXPR_BINARY, loc_of(op));
        e->binary.op    = OP_AND;
        e->binary.left  = left;
        e->binary.right = parse_eq(p);
        left = e;
    }
    return left;
}

static Expr *parse_expr(Parser *p) {
    Expr *left = parse_and(p);
    while (check(p, TOK_OR)) {
        Token *op = advance(p);
        Expr *e   = expr_new(EXPR_BINARY, loc_of(op));
        e->binary.op    = OP_OR;
        e->binary.left  = left;
        e->binary.right = parse_and(p);
        left = e;
    }
    return left;
}

/* ── Block parser ─────────────────────────────────────────────────────────── */
static Stmt *parse_block(Parser *p) {
    Token *t  = expect(p, TOK_LBRACE);
    Stmt  *s  = stmt_new(STMT_BLOCK, loc_of(t));
    Stmt **stmts = malloc(sizeof(Stmt*) * 256);
    int count = 0, cap = 256;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (count >= cap) { cap *= 2; stmts = realloc(stmts, sizeof(Stmt*) * cap); }
        stmts[count++] = parse_stmt(p);
    }
    expect(p, TOK_RBRACE);
    s->block.stmts = MAR_ALLOC_N(Stmt*, count);
    memcpy(s->block.stmts, stmts, sizeof(Stmt*) * count);
    s->block.count = count;
    free(stmts);
    return s;
}

static Stmt *parse_var_decl(Parser *p) {
    Token  *first = peek(p);
    SrcLoc  loc   = loc_of(first);
    MarType *type = parse_type(p);

    Token *name_tok = expect(p, TOK_IDENT);

    /* FEATURE 7: Check for multi-assign like: int a, int b = swap() */
    if (check(p, TOK_COMMA)) {
        int save = p->pos;
        advance(p); /* consume comma */
        if (is_type_token(peek(p)->kind)) {
            int save2 = p->pos;
            MarType *type2 = parse_type(p);
            if (check(p, TOK_IDENT)) {
                Token *name2_tok = advance(p);
                if (check(p, TOK_ASSIGN)) {
                    char    **names = malloc(sizeof(char*)    * 16);
                    MarType **types = malloc(sizeof(MarType*) * 16);
                    int count = 2;
                    names[0] = MAR_STRDUP(name_tok->value);
                    types[0] = type;
                    names[1] = MAR_STRDUP(name2_tok->value);
                    types[1] = type2;
                    while (check(p, TOK_COMMA)) {
                        advance(p);
                        if (!is_type_token(peek(p)->kind)) { p->pos--; break; }
                        types[count] = parse_type(p);
                        names[count] = MAR_STRDUP(expect(p, TOK_IDENT)->value);
                        count++;
                    }
                    expect(p, TOK_ASSIGN);
                    Expr *rhs = parse_expr(p);
                    match(p, TOK_SEMICOLON);
                    Stmt *s = stmt_new(STMT_MULTI_ASSIGN, loc);
                    s->multi_assign.names = MAR_ALLOC_N(char*, count);
                    s->multi_assign.types = MAR_ALLOC_N(MarType*, count);
                    memcpy(s->multi_assign.names, names, sizeof(char*) * count);
                    memcpy(s->multi_assign.types, types, sizeof(MarType*) * count);
                    s->multi_assign.count = count;
                    s->multi_assign.rhs   = rhs;
                    free(names); free(types);
                    return s;
                }
                p->pos = save2;
            } else {
                p->pos = save2;
            }
        }
        p->pos = save; /* Not a multi-assign, backtrack the comma */
    }

    /* Standard variable declaration(s): parse one or more declarators */
    VarDeclItem *decls = malloc(sizeof(VarDeclItem) * 32);
    int decl_count = 0;

    while (1) {
        VarDeclItem *item = &decls[decl_count++];
        item->name = MAR_STRDUP(name_tok->value);
        item->init = NULL;
        item->array_init = NULL;
        item->array_init_count = 0;
        item->array_size = 0;

        if (type->kind != TY_ARRAY && check(p, TOK_LBRACKET)) {
            advance(p);
            if (check(p, TOK_INT_LIT)) {
                item->array_size = atoi(advance(p)->value);
            }
            expect(p, TOK_RBRACKET);
        } else if (type->kind == TY_ARRAY) {
            item->array_size = type->size;
        }

        if (match(p, TOK_ASSIGN)) {
            if (check(p, TOK_LBRACE)) {
                advance(p);
                Expr **elems = malloc(sizeof(Expr*) * 128);
                int ec = 0;
                while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                    elems[ec++] = parse_expr(p);
                    if (!check(p, TOK_RBRACE)) expect(p, TOK_COMMA);
                }
                expect(p, TOK_RBRACE);
                item->array_init = MAR_ALLOC_N(Expr*, ec);
                memcpy(item->array_init, elems, sizeof(Expr*) * ec);
                item->array_init_count = ec;
                free(elems);
            } else {
                item->init = parse_expr(p);
            }
        }

        /* Check if there are more variables on this line */
        if (match(p, TOK_COMMA)) {
            name_tok = expect(p, TOK_IDENT);
        } else {
            break;
        }
    }

    match(p, TOK_SEMICOLON);

    Stmt *s = stmt_new(STMT_VAR_DECL, loc);
    s->var_decl.type = type;
    s->var_decl.decls = MAR_ALLOC_N(VarDeclItem, decl_count);
    memcpy(s->var_decl.decls, decls, sizeof(VarDeclItem) * decl_count);
    s->var_decl.decl_count = decl_count;
    free(decls);

    return s;
}
/* ── Function declaration ─────────────────────────────────────────────────── */
static FuncDecl *parse_func(Parser *p) {
    FuncDecl *f = MAR_ALLOC(FuncDecl);

    /* FEATURE 7: detect multiple return types:  int, int funcname(...)
       Strategy: parse first type. If COMMA follows, try to parse more types.
       We keep peeking to see if it's "type COMMA type ... IDENT LPAREN". */
    MarType *ret_buf[8];
    int ret_count = 0;
    ret_buf[ret_count++] = parse_type(p);

    /* Look for additional return types */
    while (check(p, TOK_COMMA)) {
        int save = p->pos;
        advance(p); /* consume comma */
        /* Is next a type keyword followed eventually by an ident? */
        if (is_type_token(peek(p)->kind)) {
            MarType *rt = parse_type(p);
            /* If we hit IDENT (the function name), store this type */
            if (check(p, TOK_IDENT) || check(p, TOK_COMMA)) {
                ret_buf[ret_count++] = rt;
            } else {
                /* backtrack */
                p->pos = save;
                break;
            }
        } else {
            p->pos = save;
            break;
        }
    }

    f->return_type       = ret_buf[0];
    f->return_types      = MAR_ALLOC_N(MarType*, ret_count);
    memcpy(f->return_types, ret_buf, sizeof(MarType*) * ret_count);
    f->return_type_count = ret_count;

    Token *name = expect(p, TOK_IDENT);
    f->name     = MAR_STRDUP(name->value);
    f->loc      = loc_of(name);
    expect(p, TOK_LPAREN);

    char    **pnames = malloc(sizeof(char*)    * 32);
    MarType **ptypes = malloc(sizeof(MarType*) * 32);
    int pc = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        ptypes[pc] = parse_type(p);
        Token *pn  = expect(p, TOK_IDENT);
        pnames[pc] = MAR_STRDUP(pn->value);
        pc++;
        if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA);
    }
    expect(p, TOK_RPAREN);

    f->param_names = MAR_ALLOC_N(char*,    pc);
    f->param_types = MAR_ALLOC_N(MarType*, pc);
    memcpy(f->param_names, pnames, sizeof(char*)    * pc);
    memcpy(f->param_types, ptypes, sizeof(MarType*) * pc);
    f->param_count = pc;
    free(pnames); free(ptypes);

    f->body = parse_block(p);
    return f;
}

/* ── Class declaration — FEATURE 4 (inheritance) ─────────────────────────── */
static Stmt *parse_class(Parser *p) {
    expect(p, TOK_CLASS);
    Token *name_tok = expect(p, TOK_IDENT);
    SrcLoc loc = loc_of(name_tok);

    ClassDecl *c = MAR_ALLOC(ClassDecl);
    c->name        = MAR_STRDUP(name_tok->value);
    c->parent_name = NULL;
    c->loc         = loc;

    /* FEATURE 4: extends ClassName */
    if (match(p, TOK_EXTENDS)) {
        Token *parent = expect(p, TOK_IDENT);
        c->parent_name = MAR_STRDUP(parent->value);
    }

    expect(p, TOK_LBRACE);

    Field  *fields  = malloc(sizeof(Field)  * 64);
    Method *methods = malloc(sizeof(Method) * 64);
    int fc = 0, mc = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        /* Peek to determine field vs method:
           field:  type IDENT SEMICOLON
           method: type IDENT LPAREN ...
           We look two tokens ahead. */
        int save = p->pos;
        MarType *t = parse_type(p);
        if (check(p, TOK_IDENT)) {
            Token *mname = advance(p);
            if (check(p, TOK_LPAREN)) {
                /* method: backtrack so parse_func reads the type and name */
                p->pos = save;
                methods[mc].method = parse_func(p);
                methods[mc].name   = MAR_STRDUP(methods[mc].method->name);
                mc++;
            } else {
                /* field */
                fields[fc].name = MAR_STRDUP(mname->value);
                fields[fc].type = t;
                fc++;
                match(p, TOK_SEMICOLON);
            }
        } else {
            /* error recovery */
            SrcLoc eloc = {peek(p)->line, peek(p)->col, peek(p)->file};
            error_emit(p->errors, ERR_PARSER, eloc,
                "Unexpected token '%s' in class body", token_kind_str(peek(p)->kind));
            advance(p);
        }
    }
    expect(p, TOK_RBRACE);

    c->fields       = fields;
    c->field_count  = fc;
    c->methods      = methods;
    c->method_count = mc;

    Stmt *s = stmt_new(STMT_CLASS_DECL, loc);
    s->class_decl = c;
    return s;
}

/* ── Switch statement parser ─────────────────────────────────────────────── */
static Stmt *parse_switch(Parser *p, SrcLoc loc) {
    expect(p, TOK_LPAREN);
    Expr *expr = parse_expr(p);
    expect(p, TOK_RPAREN);
    expect(p, TOK_LBRACE);

    CaseClause *cases = malloc(sizeof(CaseClause) * 64);
    int case_count = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        CaseClause *cl = &cases[case_count++];
        cl->value = NULL;

        if (match(p, TOK_CASE)) {
            cl->value = parse_expr(p);
            match(p, TOK_COLON);   /* optional colon */
        } else if (match(p, TOK_DEFAULT)) {
            match(p, TOK_COLON);   /* optional colon */
        } else {
            break;
        }

        /* Collect body statements until next case/default/} */
        Stmt **body = malloc(sizeof(Stmt*) * 64);
        int bc = 0;
        while (!check(p, TOK_CASE) && !check(p, TOK_DEFAULT) &&
               !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            body[bc++] = parse_stmt(p);
        }
        cl->body = MAR_ALLOC_N(Stmt*, bc);
        memcpy(cl->body, body, sizeof(Stmt*) * bc);
        cl->body_count = bc;
        free(body);
    }
    expect(p, TOK_RBRACE);

    Stmt *s = stmt_new(STMT_SWITCH, loc);
    s->switch_stmt.expr       = expr;
    s->switch_stmt.cases      = MAR_ALLOC_N(CaseClause, case_count);
    memcpy(s->switch_stmt.cases, cases, sizeof(CaseClause) * case_count);
    s->switch_stmt.case_count = case_count;
    free(cases);
    return s;
}

/* ── Statement parser ─────────────────────────────────────────────────────── */
static Stmt *parse_stmt(Parser *p) {
    Token *t   = peek(p);
    SrcLoc loc = loc_of(t);

    /* class declaration */
    if (t->kind == TOK_CLASS) return parse_class(p);

    /* FEATURE 2: import "file.mar" */
    if (t->kind == TOK_IMPORT) {
        advance(p);
        Token *path_tok = expect(p, TOK_STRING_LIT);
        match(p, TOK_SEMICOLON);
        Stmt *s = stmt_new(STMT_IMPORT, loc);
        s->import_stmt.path = MAR_STRDUP(path_tok->value);
        return s;
    }

    /* type-led statement: variable declaration or multi-assign.
       DISAMBIGUATION: built-in type keywords (int/float/etc) always start a decl.
       For TOK_IDENT (user class types), only treat as type if followed by IDENT
       (i.e. "Player p = ..."). Otherwise "x = 5" falls through to assignment. */
    if (t->kind != TOK_IDENT && is_type_token(t->kind)) {
        return parse_var_decl(p);
    }
    if (t->kind == TOK_IDENT && p->pos + 1 < 65536) {
        Token *t2 = &p->tokens[p->pos + 1];
        if (t2->kind == TOK_IDENT) {
            /* "ClassName varName ..." -> class variable declaration */
            return parse_var_decl(p);
        }
    }

    /* if */
    if (t->kind == TOK_IF) {
        advance(p);
        Stmt *s = stmt_new(STMT_IF, loc);
        expect(p, TOK_LPAREN);
        s->if_stmt.cond        = parse_expr(p);
        expect(p, TOK_RPAREN);
        s->if_stmt.then_branch = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        s->if_stmt.else_branch = NULL;
        if (check(p, TOK_ELSE)) {
            advance(p);
            s->if_stmt.else_branch = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        }
        return s;
    }

    /* while */
    if (t->kind == TOK_WHILE) {
        advance(p);
        Stmt *s = stmt_new(STMT_WHILE, loc);
        expect(p, TOK_LPAREN);
        s->while_stmt.cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        s->while_stmt.body = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        return s;
    }

    /* for — FEATURE 8: detect  for item in array  vs  for i in range(a,b) */
    if (t->kind == TOK_FOR) {
        advance(p);
        Token *var = expect(p, TOK_IDENT);
        expect(p, TOK_IN);

        if (match(p, TOK_RANGE)) {
            /* for i in range(start, end) */
            Stmt *s = stmt_new(STMT_FOR_RANGE, loc);
            s->for_range.var = MAR_STRDUP(var->value);
            expect(p, TOK_LPAREN);
            s->for_range.start = parse_expr(p);
            expect(p, TOK_COMMA);
            s->for_range.end   = parse_expr(p);
            expect(p, TOK_RPAREN);
            s->for_range.body  = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
            return s;
        } else {
            /* FEATURE 8: for item in arr { } */
            Token *arr = expect(p, TOK_IDENT);
            Stmt *s = stmt_new(STMT_FOR_ARRAY, loc);
            s->for_array.var  = MAR_STRDUP(var->value);
            s->for_array.arr  = MAR_STRDUP(arr->value);
            s->for_array.body = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
            return s;
        }
    }

    /* switch */
    if (t->kind == TOK_SWITCH) {
        advance(p);
        return parse_switch(p, loc);
    }

    /* return */
    if (t->kind == TOK_RETURN) {
        advance(p);
        /* FEATURE 7: return a, b */
        if (!check(p, TOK_SEMICOLON) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            Expr *first = parse_expr(p);
            if (check(p, TOK_COMMA)) {
                /* multi-return */
                Expr **vals = malloc(sizeof(Expr*) * 8);
                int vc = 0;
                vals[vc++] = first;
                while (match(p, TOK_COMMA)) vals[vc++] = parse_expr(p);
                match(p, TOK_SEMICOLON);
                Stmt *s = stmt_new(STMT_MULTI_RETURN, loc);
                s->multi_ret.values      = MAR_ALLOC_N(Expr*, vc);
                memcpy(s->multi_ret.values, vals, sizeof(Expr*) * vc);
                s->multi_ret.value_count = vc;
                free(vals);
                return s;
            }
            match(p, TOK_SEMICOLON);
            Stmt *s = stmt_new(STMT_RETURN, loc);
            s->ret.value = first;
            return s;
        }
        match(p, TOK_SEMICOLON);
        Stmt *s = stmt_new(STMT_RETURN, loc);
        s->ret.value = NULL;
        return s;
    }

    /* break */
    if (t->kind == TOK_BREAK) {
        advance(p);
        match(p, TOK_SEMICOLON);
        return stmt_new(STMT_BREAK, loc);
    }

   /* print */
    if (t->kind == TOK_PRINT) {
        advance(p);
        expect(p, TOK_LPAREN);
        Stmt *s = stmt_new(STMT_PRINT, loc);
        s->print.fmt = NULL; /* We no longer require a format string! */
        
        Expr **args  = malloc(sizeof(Expr*) * 32);
        int ac = 0;
        
        /* Parse any number of arguments separated by commas */
        if (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            do {
                args[ac++] = parse_expr(p);
            } while (match(p, TOK_COMMA));
        }
        
        expect(p, TOK_RPAREN);
        match(p, TOK_SEMICOLON);
        
        s->print.args = MAR_ALLOC_N(Expr*, ac);
        memcpy(s->print.args, args, sizeof(Expr*) * ac);
        s->print.argc = ac;
        free(args);
        return s;
    }

  if (t->kind == TOK_TAKE) {
        advance(p);
        expect(p, TOK_LPAREN);
        
        Stmt *s = stmt_new(STMT_TAKE, loc);
        s->print.fmt = NULL; /* We no longer require a format string! */
        
        Expr **args  = malloc(sizeof(Expr*) * 32);
        int ac = 0;
        
        /* Parse any number of arguments separated by commas */
        if (!check(p, TOK_RPAREN)) {
            do {
                args[ac++] = parse_expr(p);
            } while (match(p, TOK_COMMA));
        }
        
        expect(p, TOK_RPAREN);
        match(p, TOK_SEMICOLON);
        
        s->print.args = MAR_ALLOC_N(Expr*, ac);
        memcpy(s->print.args, args, sizeof(Expr*) * ac);
        s->print.argc = ac;
        free(args);
        return s;
    }

    /* block */
    if (t->kind == TOK_LBRACE) return parse_block(p);

    /* Identifier-led statement: assignment, member assign, or expr stmt */
    if (t->kind == TOK_IDENT) {
        Token *name = advance(p);

        /* member assignment chain: obj.field.field = expr */
        if (check(p, TOK_DOT)) {
            /* Build up the member access chain */
            Expr *left = expr_new(EXPR_IDENT, loc);
            left->name = MAR_STRDUP(name->value);
            while (check(p, TOK_DOT)) {
                advance(p);
                Token *field = expect(p, TOK_IDENT);
                /* check if it's a method call */
                if (check(p, TOK_LPAREN)) {
                    Expr *mc_expr = expr_new(EXPR_METHOD_CALL, loc_of(field));
                    mc_expr->method_call.obj    = left;
                    mc_expr->method_call.method = MAR_STRDUP(field->value);
                    advance(p); /* ( */
                    Expr **args = malloc(sizeof(Expr*) * 32);
                    int argc = 0;
                    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                        args[argc++] = parse_expr(p);
                        if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA);
                    }
                    expect(p, TOK_RPAREN);
                    mc_expr->method_call.args = MAR_ALLOC_N(Expr*, argc);
                    memcpy(mc_expr->method_call.args, args, sizeof(Expr*) * argc);
                    mc_expr->method_call.argc = argc;
                    free(args);
                    left = mc_expr;
                    match(p, TOK_SEMICOLON);
                    Stmt *s = stmt_new(STMT_EXPR, loc);
                    s->expr_stmt.expr = left;
                    return s;
                }
                /* Field access */
                Expr *dot = expr_new(EXPR_MEMBER_ACCESS, loc_of(field));
                dot->member.left = left;
                dot->member.name = MAR_STRDUP(field->value);
                left = dot;
            }
            /* check for compound/simple assignment */
            TokenKind k = peek(p)->kind;
            if (k == TOK_ASSIGN || k == TOK_PLUS_ASSIGN || k == TOK_MINUS_ASSIGN ||
                k == TOK_STAR_ASSIGN || k == TOK_SLASH_ASSIGN || k == TOK_PERCENT_ASSIGN) {
                Token *op = advance(p);
                Operator aop;
                switch (op->kind) {
                    case TOK_PLUS_ASSIGN:    aop = OP_PLUS_ASSIGN;  break;
                    case TOK_MINUS_ASSIGN:   aop = OP_MINUS_ASSIGN; break;
                    case TOK_STAR_ASSIGN:    aop = OP_STAR_ASSIGN;  break;
                    case TOK_SLASH_ASSIGN:   aop = OP_SLASH_ASSIGN; break;
                    case TOK_PERCENT_ASSIGN: aop = OP_PCT_ASSIGN;   break;
                    default:                 aop = OP_ASSIGN;       break;
                }
                Stmt *s = stmt_new(STMT_ASSIGN, loc);
                s->assign.op     = aop;
                s->assign.target = left;
                s->assign.value  = parse_expr(p);
                match(p, TOK_SEMICOLON);
                return s;
            }
            /* Expression statement (e.g. standalone member access) */
            match(p, TOK_SEMICOLON);
            Stmt *s = stmt_new(STMT_EXPR, loc);
            s->expr_stmt.expr = left;
            return s;
        }

        /* compound / simple assignment: x = e, x += e, etc. */
        TokenKind k = peek(p)->kind;
        if (k == TOK_ASSIGN || k == TOK_PLUS_ASSIGN || k == TOK_MINUS_ASSIGN ||
            k == TOK_STAR_ASSIGN || k == TOK_SLASH_ASSIGN || k == TOK_PERCENT_ASSIGN) {
            Token *op = advance(p);
            Operator aop;
            switch (op->kind) {
                case TOK_PLUS_ASSIGN:    aop = OP_PLUS_ASSIGN;  break;
                case TOK_MINUS_ASSIGN:   aop = OP_MINUS_ASSIGN; break;
                case TOK_STAR_ASSIGN:    aop = OP_STAR_ASSIGN;  break;
                case TOK_SLASH_ASSIGN:   aop = OP_SLASH_ASSIGN; break;
                case TOK_PERCENT_ASSIGN: aop = OP_PCT_ASSIGN;   break;
                default:                 aop = OP_ASSIGN;       break;
            }
            Stmt *s   = stmt_new(STMT_ASSIGN, loc);
            s->assign.op     = aop;
            s->assign.target = expr_new(EXPR_IDENT, loc);
            s->assign.target->name = MAR_STRDUP(name->value);
            s->assign.value  = parse_expr(p);
            match(p, TOK_SEMICOLON);
            return s;
        }

        /* arr[i] = expr  (index assignment) */
        if (check(p, TOK_LBRACKET)) {
            advance(p);
            Expr *idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            TokenKind ak = peek(p)->kind;
            if (ak == TOK_ASSIGN || ak == TOK_PLUS_ASSIGN || ak == TOK_MINUS_ASSIGN ||
                ak == TOK_STAR_ASSIGN || ak == TOK_SLASH_ASSIGN || ak == TOK_PERCENT_ASSIGN) {
                Token *op = advance(p);
                Operator aop;
                switch (op->kind) {
                    case TOK_PLUS_ASSIGN:    aop = OP_PLUS_ASSIGN;  break;
                    case TOK_MINUS_ASSIGN:   aop = OP_MINUS_ASSIGN; break;
                    case TOK_STAR_ASSIGN:    aop = OP_STAR_ASSIGN;  break;
                    case TOK_SLASH_ASSIGN:   aop = OP_SLASH_ASSIGN; break;
                    case TOK_PERCENT_ASSIGN: aop = OP_PCT_ASSIGN;   break;
                    default:                 aop = OP_ASSIGN;       break;
                }
                Stmt *s = stmt_new(STMT_ASSIGN, loc);
                s->assign.op = aop;
                s->assign.target = expr_new(EXPR_INDEX, loc);
                s->assign.target->idx.array = expr_new(EXPR_IDENT, loc);
                s->assign.target->idx.array->name = MAR_STRDUP(name->value);
                s->assign.target->idx.index = idx;
                s->assign.value = parse_expr(p);
                match(p, TOK_SEMICOLON);
                return s;
            }
        }

        /* fall through to expression statement — backtrack */
        p->pos--;
    }

    /* Expression statement */
    Stmt *s = stmt_new(STMT_EXPR, loc);
    s->expr_stmt.expr = parse_expr(p);
    match(p, TOK_SEMICOLON);
    return s;
}

/* ── Stdlib path resolution — FEATURE 3 ─────────────────────────────────── */
static void handle_stdlib_import(Program *prog, const char *path) {
    /* Map "mar/math" → inject synthetic C include + helper funcs */
    if (strcmp(path, "mar/math") == 0) {
        /* Register C include */
        prog->c_includes = realloc(prog->c_includes,
            sizeof(char*) * (prog->c_include_count + 1));
        prog->c_includes[prog->c_include_count++] = strdup("#include <math.h>");
        /* inject min/max/abs as inline MAR functions via synthetic tokens */
        /* Actually we just note them; codegen will emit helpers */
    } else if (strcmp(path, "mar/str") == 0) {
        prog->c_includes = realloc(prog->c_includes,
            sizeof(char*) * (prog->c_include_count + 1));
        prog->c_includes[prog->c_include_count++] = strdup("#include <string.h>");
    } else if (strcmp(path, "mar/io") == 0) {
        prog->c_includes = realloc(prog->c_includes,
            sizeof(char*) * (prog->c_include_count + 1));
        prog->c_includes[prog->c_include_count++] = strdup("#include <stdio.h>");
    }
}

/* ── Top-level program parser ─────────────────────────────────────────────── */
Program *parser_parse(Parser *p) {
    Program    *prog    = MAR_ALLOC(Program);
    FuncDecl  **funcs   = malloc(sizeof(FuncDecl*)  * 256);
    ClassDecl **classes = malloc(sizeof(ClassDecl*) * 64);
    int fc = 0, cc = 0;
    prog->c_includes     = malloc(sizeof(char*) * 32);
    prog->c_include_count = 0;

    while (!check(p, TOK_EOF)) {
        /* FEATURE 2: import statement at top level */
        if (check(p, TOK_IMPORT)) {
            advance(p);
            Token *path_tok = expect(p, TOK_STRING_LIT);
            match(p, TOK_SEMICOLON);
            handle_stdlib_import(prog, path_tok->value);
            /* For user imports, resolve and parse the file here.
               Searching relative to source_dir if set. */
            if (p->source_dir && strncmp(path_tok->value, "mar/", 4) != 0) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s",
                         p->source_dir, path_tok->value);
                FILE *f = fopen(full_path, "r");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long sz = ftell(f); rewind(f);
                    char *src = malloc(sz + 2);
                    fread(src, 1, sz, f); src[sz] = '\0';
                    fclose(f);

                    ErrorCtx *iec = error_ctx_create(full_path);
                    error_ctx_load_source(iec, src);
                    Lexer  *il = lexer_create(src, full_path, iec);
                    Token  *it = lexer_tokenize(il);
                    Parser *ip = parser_create(it, iec);
                    ip->source_dir = p->source_dir;
                    Program *sub = parser_parse(ip);
                    /* merge into prog */
                    for (int i = 0; i < sub->func_count;  i++) funcs[fc++]   = sub->funcs[i];
                    for (int i = 0; i < sub->class_count; i++) classes[cc++] = sub->classes[i];
                    free(src);
                    free(ip); free(il);
                } else {
                    SrcLoc loc = {path_tok->line, path_tok->col, path_tok->file};
                    error_emit(p->errors, ERR_PARSER, loc,
                        "Cannot open import '%s'", full_path);
                }
            }
            continue;
        }

        if (check(p, TOK_CLASS)) {
            Stmt *s = parse_class(p);
            classes[cc++] = s->class_decl;
        } else {
            funcs[fc++] = parse_func(p);
        }
    }

    prog->funcs = MAR_ALLOC_N(FuncDecl*, fc);
    memcpy(prog->funcs, funcs, sizeof(FuncDecl*) * fc);
    prog->func_count = fc;

    prog->classes = MAR_ALLOC_N(ClassDecl*, cc);
    memcpy(prog->classes, classes, sizeof(ClassDecl*) * cc);
    prog->class_count = cc;

    free(funcs); free(classes);
    return prog;
}