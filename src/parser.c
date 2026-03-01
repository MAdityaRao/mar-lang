#include "mar/parser.h"
#include "mar/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Constructor ───────────────────────────────────────────────────────────── */

Parser *parser_create(Token *tokens, ErrorCtx *ec) {
    Parser *p = calloc(1, sizeof(Parser));
    p->tokens = tokens;
    p->errors = ec;
    return p;
}

/* ── Low-level helpers ─────────────────────────────────────────────────────── */

static Token *peek(Parser *p)    { return &p->tokens[p->pos]; }
/* peek2 retained for potential future use; suppresses -Wunused via (void) cast */
static Token *peek2(Parser *p)   { return &p->tokens[p->pos + 1]; }

static Token *advance(Parser *p) {
    Token *t = &p->tokens[p->pos];
    if (t->kind != TOK_EOF) p->pos++;
    return t;
}

static bool check(Parser *p, TokenKind k) { return peek(p)->kind == k; }

static Token *expect(Parser *p, TokenKind k) {
    if (check(p, k)) return advance(p);
    SrcLoc loc = {peek(p)->line, peek(p)->col, peek(p)->file};
    error_emit(p->errors, ERR_PARSER, loc,
        "Expected '%s' but got '%s'",
        token_kind_str(k), token_kind_str(peek(p)->kind));
    return peek(p);
}

static bool match(Parser *p, TokenKind k) {
    if (check(p, k)) { advance(p); return true; }
    return false;
}

static SrcLoc loc_of(Token *t) {
    SrcLoc l = {t->line, t->col, t->file};
    return l;
}

/* ── Forward declarations ──────────────────────────────────────────────────── */

static Stmt    *parse_stmt(Parser *p);
static Stmt    *parse_block(Parser *p);
static Expr    *parse_expr(Parser *p);
static MarType *parse_type(Parser *p);
static FuncDecl *parse_func(Parser *p);
static Stmt    *parse_var_decl(Parser *p);

/* ── is_type_token (needed by parse_type and parse_stmt) ──────────────────── */

static bool is_type_token(TokenKind k) {
    return k == TOK_INT || k == TOK_FLOAT || k == TOK_CHAR ||
           k == TOK_BOOL || k == TOK_VOID;
}

/* ── Type parser ───────────────────────────────────────────────────────────── */

static MarType *parse_type(Parser *p) {
    MarType *t = NULL;
    if      (match(p, TOK_INT))   t = type_new(TY_INT);
    else if (match(p, TOK_FLOAT)) t = type_new(TY_FLOAT);
    else if (match(p, TOK_CHAR))  t = type_new(TY_CHAR);
    else if (match(p, TOK_BOOL))  t = type_new(TY_BOOL);
    else if (match(p, TOK_VOID))  t = type_new(TY_VOID);
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
    return t;
}

/* ── Expression parsers ────────────────────────────────────────────────────── */

static Operator tok_to_op(TokenKind k) {
    switch (k) {
        case TOK_PLUS:    return OP_ADD;
        case TOK_MINUS:   return OP_SUB;
        case TOK_STAR:    return OP_MUL;
        case TOK_SLASH:   return OP_DIV;
        case TOK_PERCENT: return OP_MOD;
        case TOK_EQ:      return OP_EQ;
        case TOK_NEQ:     return OP_NEQ;
        case TOK_LT:      return OP_LT;
        case TOK_GT:      return OP_GT;
        case TOK_LTE:     return OP_LTE;
        case TOK_GTE:     return OP_GTE;
        case TOK_AND:     return OP_AND;
        case TOK_OR:      return OP_OR;
        default:          return OP_ADD;
    }
}

static Expr *parse_primary(Parser *p) {
    Token *t   = peek(p);
    SrcLoc loc = loc_of(t);

    /* 'new ClassName(args)' */
    if (match(p, TOK_NEW)) {
        Token *className = expect(p, TOK_IDENT);
        Expr *e = expr_new(EXPR_NEW, loc);
        e->new_obj.class_name = MAR_STRDUP(className->value);
        expect(p, TOK_LPAREN);
        Expr **args = malloc(sizeof(Expr *) * 16);
        int argc = 0;
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            args[argc++] = parse_expr(p);
            if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA);
        }
        expect(p, TOK_RPAREN);
        e->new_obj.args = MAR_ALLOC_N(Expr *, argc);
        memcpy(e->new_obj.args, args, sizeof(Expr *) * argc);
        e->new_obj.argc = argc;
        free(args);
        return e;
    }

    if (t->kind == TOK_INT_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_INT_LIT, loc);
        e->ival = atoll(t->value);
        return e;
    }
    if (t->kind == TOK_FLOAT_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_FLOAT_LIT, loc);
        e->fval = atof(t->value);
        return e;
    }
    if (t->kind == TOK_STRING_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_STRING_LIT, loc);
        e->sval = MAR_STRDUP(t->value);
        return e;
    }
    if (t->kind == TOK_CHAR_LIT) {
        advance(p);
        Expr *e = expr_new(EXPR_CHAR_LIT, loc);
        e->cval = t->value[0];
        return e;
    }
    if (t->kind == TOK_TRUE)  { advance(p); Expr *e = expr_new(EXPR_BOOL_LIT, loc); e->bval = true;  return e; }
    if (t->kind == TOK_FALSE) { advance(p); Expr *e = expr_new(EXPR_BOOL_LIT, loc); e->bval = false; return e; }

    if (t->kind == TOK_IDENT) {
        advance(p);
        Expr *left = expr_new(EXPR_IDENT, loc);
        left->name = MAR_STRDUP(t->value);

        /* Member access chain: obj.field.field  →  EXPR_MEMBER_ACCESS nodes */
        while (match(p, TOK_DOT)) {
            Token *member = expect(p, TOK_IDENT);
            Expr *dot     = expr_new(EXPR_MEMBER_ACCESS, loc_of(member));
            dot->member.left = left;
            dot->member.name = MAR_STRDUP(member->value);
            left = dot;
        }

        /* Function call */
        if (check(p, TOK_LPAREN)) {
            advance(p);
            Expr *e = expr_new(EXPR_CALL, loc);
            e->call.callee = left->name; /* works when left is a plain IDENT */
            Expr **args = malloc(sizeof(Expr *) * 16);
            int argc = 0;
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                args[argc++] = parse_expr(p);
                if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA);
            }
            expect(p, TOK_RPAREN);
            e->call.args = MAR_ALLOC_N(Expr *, argc);
            memcpy(e->call.args, args, sizeof(Expr *) * argc);
            e->call.argc = argc;
            free(args);
            return e;
        }
        return left;
    }

    if (t->kind == TOK_AMPERSAND) {
        advance(p);
        Expr *e = expr_new(EXPR_ADDR_OF, loc);
        e->addr.operand = parse_primary(p);
        return e;
    }
    if (t->kind == TOK_LPAREN) {
        advance(p);
        Expr *e = parse_expr(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    error_emit(p->errors, ERR_PARSER, loc,
        "Unexpected token '%s'", token_kind_str(t->kind));
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
    while (check(p,TOK_STAR) || check(p,TOK_SLASH) || check(p,TOK_PERCENT)) {
        Token *op = advance(p);
        Expr *e   = expr_new(EXPR_BINARY, loc_of(op));
        e->binary.op    = tok_to_op(op->kind);
        e->binary.left  = left;
        e->binary.right = parse_unary(p);
        left = e;
    }
    return left;
}

static Expr *parse_add(Parser *p) {
    Expr *left = parse_mul(p);
    while (check(p,TOK_PLUS) || check(p,TOK_MINUS)) {
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
    while (check(p,TOK_EQ) || check(p,TOK_NEQ)) {
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

/* ── Block parser ──────────────────────────────────────────────────────────── */

static Stmt *parse_block(Parser *p) {
    Token *t  = expect(p, TOK_LBRACE);
    Stmt  *s  = stmt_new(STMT_BLOCK, loc_of(t));
    Stmt **stmts = malloc(sizeof(Stmt *) * 128);
    int count = 0, cap = 128;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (count >= cap) { cap *= 2; stmts = realloc(stmts, sizeof(Stmt *) * cap); }
        stmts[count++] = parse_stmt(p);
    }
    expect(p, TOK_RBRACE);
    s->block.stmts = MAR_ALLOC_N(Stmt *, count);
    memcpy(s->block.stmts, stmts, sizeof(Stmt *) * count);
    s->block.count = count;
    free(stmts);
    return s;
}

/* ── Variable declaration ──────────────────────────────────────────────────── */

static Stmt *parse_var_decl(Parser *p) {
    Token *first   = peek(p);
    SrcLoc loc     = loc_of(first);
    MarType *type  = parse_type(p);
    Token *name    = expect(p, TOK_IDENT);
    Stmt *s        = stmt_new(STMT_VAR_DECL, loc);
    s->var_decl.type = type;
    s->var_decl.name = MAR_STRDUP(name->value);
    s->var_decl.init = NULL;
    s->var_decl.array_init       = NULL;
    s->var_decl.array_init_count = 0;

    if (type->kind == TY_ARRAY && check(p, TOK_LBRACKET)) {
        /* int arr[N] = { ... } */
        advance(p);
        expect(p, TOK_RBRACKET);
    }

    if (match(p, TOK_ASSIGN)) {
        if (check(p, TOK_LBRACE)) {
            /* array initialiser: { e1, e2, ... } */
            advance(p);
            Expr **elems = malloc(sizeof(Expr *) * 64);
            int ec = 0;
            while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                elems[ec++] = parse_expr(p);
                if (!check(p, TOK_RBRACE)) expect(p, TOK_COMMA);
            }
            expect(p, TOK_RBRACE);
            s->var_decl.array_init       = MAR_ALLOC_N(Expr *, ec);
            memcpy(s->var_decl.array_init, elems, sizeof(Expr *) * ec);
            s->var_decl.array_init_count = ec;
            free(elems);
        } else {
            s->var_decl.init = parse_expr(p);
        }
    }
    expect(p, TOK_SEMICOLON);
    return s;
}

/* ── Function declaration ──────────────────────────────────────────────────── */

static FuncDecl *parse_func(Parser *p) {
    FuncDecl *f    = MAR_ALLOC(FuncDecl);
    f->return_type = parse_type(p);
    Token *name    = expect(p, TOK_IDENT);
    f->name        = MAR_STRDUP(name->value);
    f->loc         = loc_of(name);
    expect(p, TOK_LPAREN);

    char    **pnames = malloc(sizeof(char *)    * 16);
    MarType **ptypes = malloc(sizeof(MarType *) * 16);
    int pc = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        ptypes[pc] = parse_type(p);
        Token *pn  = expect(p, TOK_IDENT);
        pnames[pc] = MAR_STRDUP(pn->value);
        pc++;
        if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA);
    }
    expect(p, TOK_RPAREN);
    f->param_names = MAR_ALLOC_N(char *,    pc);
    f->param_types = MAR_ALLOC_N(MarType *, pc);
    memcpy(f->param_names, pnames, sizeof(char *)    * pc);
    memcpy(f->param_types, ptypes, sizeof(MarType *) * pc);
    f->param_count = pc;
    free(pnames);
    free(ptypes);

    f->body = parse_block(p);
    return f;
}

/* ── Class declaration ─────────────────────────────────────────────────────── */

static Stmt *parse_class(Parser *p) {
    expect(p, TOK_CLASS);
    Token *name = expect(p, TOK_IDENT);
    SrcLoc loc  = loc_of(name);

    ClassDecl *c = MAR_ALLOC(ClassDecl);
    c->name = MAR_STRDUP(name->value);
    c->loc  = loc;

    expect(p, TOK_LBRACE);

    Field  *fields  = malloc(sizeof(Field)  * 64);
    Method *methods = malloc(sizeof(Method) * 64);
    int fc = 0, mc = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        MarType *t     = parse_type(p);
        Token   *mname = expect(p, TOK_IDENT);

        if (check(p, TOK_LPAREN)) {
            /* Method: backtrack so parse_func re-reads the type and name */
            p->pos -= 2;
            methods[mc].name   = MAR_STRDUP(mname->value);
            methods[mc].method = parse_func(p);
            mc++;
        } else {
            /* Field */
            fields[fc].name = MAR_STRDUP(mname->value);
            fields[fc].type = t;
            fc++;
            expect(p, TOK_SEMICOLON);
        }
    }
    expect(p, TOK_RBRACE);

    c->fields       = fields;  c->field_count  = fc;
    c->methods      = methods; c->method_count = mc;

    Stmt *s = stmt_new(STMT_CLASS_DECL, loc);
    s->class_decl = c;
    return s;
}

/* ── Statement parser ──────────────────────────────────────────────────────── */

static Stmt *parse_stmt(Parser *p) {
    Token *t   = peek(p);
    SrcLoc loc = loc_of(t);

    if (t->kind == TOK_CLASS)           return parse_class(p);
    if (is_type_token(t->kind))         return parse_var_decl(p);

    if (t->kind == TOK_IF) {
        advance(p);
        Stmt *s = stmt_new(STMT_IF, loc);
        expect(p, TOK_LPAREN);
        s->if_stmt.cond        = parse_expr(p);
        expect(p, TOK_RPAREN);
        s->if_stmt.then_branch = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        if (check(p, TOK_ELSE)) {
            advance(p);
            s->if_stmt.else_branch = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        }
        return s;
    }

    if (t->kind == TOK_WHILE) {
        advance(p);
        Stmt *s = stmt_new(STMT_WHILE, loc);
        expect(p, TOK_LPAREN);
        s->while_stmt.cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        s->while_stmt.body = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        return s;
    }

    if (t->kind == TOK_FOR) {
        advance(p);
        Stmt *s = stmt_new(STMT_FOR_RANGE, loc);
        Token *var = expect(p, TOK_IDENT);
        s->for_range.var = MAR_STRDUP(var->value);
        expect(p, TOK_IN);
        expect(p, TOK_RANGE);
        expect(p, TOK_LPAREN);
        s->for_range.start = parse_expr(p);
        expect(p, TOK_COMMA);
        s->for_range.end   = parse_expr(p);
        expect(p, TOK_RPAREN);
        s->for_range.body  = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        return s;
    }

    if (t->kind == TOK_PRINT) {
        advance(p);
        expect(p, TOK_LPAREN);
        Token *fmt = expect(p, TOK_STRING_LIT);
        Stmt *s    = stmt_new(STMT_PRINT, loc);
        s->print.fmt = MAR_STRDUP(fmt->value);
        Expr **args  = malloc(sizeof(Expr *) * 16);
        int ac = 0;
        while (match(p, TOK_COMMA)) args[ac++] = parse_expr(p);
        expect(p, TOK_RPAREN);
        s->print.args = MAR_ALLOC_N(Expr *, ac);
        memcpy(s->print.args, args, sizeof(Expr *) * ac);
        s->print.argc = ac;
        free(args);
        return s;
    }

    if (t->kind == TOK_RETURN) {
        advance(p);
        Stmt *s = stmt_new(STMT_RETURN, loc);
        if (!check(p, TOK_SEMICOLON) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF))
            s->ret.value = parse_expr(p);
        expect(p, TOK_SEMICOLON);
        return s;
    }

    if (t->kind == TOK_BREAK) {
        advance(p);
        expect(p, TOK_SEMICOLON);
        return stmt_new(STMT_BREAK, loc);
    }

    if (t->kind == TOK_LBRACE) return parse_block(p);

    /* Identifier: could be assignment, member assignment, or expression statement */
    if (t->kind == TOK_IDENT) {
        Token *name = advance(p);

        /* Member assignment:  obj.field = expr; */
        if (match(p, TOK_DOT)) {
            Token *member = expect(p, TOK_IDENT);
            expect(p, TOK_ASSIGN);
            Stmt *s = stmt_new(STMT_ASSIGN, loc);
            s->assign.op = OP_ASSIGN;
            s->assign.target = expr_new(EXPR_MEMBER_ACCESS, loc);
            s->assign.target->member.left = expr_new(EXPR_IDENT, loc);
            s->assign.target->member.left->name = MAR_STRDUP(name->value);
            s->assign.target->member.name = MAR_STRDUP(member->value);
            s->assign.value = parse_expr(p);
            expect(p, TOK_SEMICOLON);
            return s;
        }

        /* Regular assignment variants: x = e;  x += e;  etc. */
        TokenKind k = peek(p)->kind;
        if (k == TOK_ASSIGN     || k == TOK_PLUS_ASSIGN  || k == TOK_MINUS_ASSIGN ||
            k == TOK_STAR_ASSIGN || k == TOK_SLASH_ASSIGN || k == TOK_PCT_ASSIGN) {
            Token *op = advance(p);
            Stmt *s   = stmt_new(STMT_ASSIGN, loc);
            Operator aop;
            switch (op->kind) {
                case TOK_PLUS_ASSIGN:  aop = OP_PLUS_ASSIGN;  break;
                case TOK_MINUS_ASSIGN: aop = OP_MINUS_ASSIGN; break;
                case TOK_STAR_ASSIGN:  aop = OP_STAR_ASSIGN;  break;
                case TOK_SLASH_ASSIGN: aop = OP_SLASH_ASSIGN; break;
                case TOK_PCT_ASSIGN:   aop = OP_PCT_ASSIGN;   break;
                default:               aop = OP_ASSIGN;       break;
            }
            s->assign.op     = aop;
            s->assign.target = expr_new(EXPR_IDENT, loc);
            s->assign.target->name = MAR_STRDUP(name->value);
            s->assign.value  = parse_expr(p);
            expect(p, TOK_SEMICOLON);
            return s;
        }

        /* Index assignment: arr[i] = e; */
        if (check(p, TOK_LBRACKET)) {
            advance(p);
            Expr *idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            expect(p, TOK_ASSIGN);
            Stmt *s = stmt_new(STMT_ASSIGN, loc);
            s->assign.op = OP_ASSIGN;
            s->assign.target = expr_new(EXPR_INDEX, loc);
            s->assign.target->idx.array = expr_new(EXPR_IDENT, loc);
            s->assign.target->idx.array->name = MAR_STRDUP(name->value);
            s->assign.target->idx.index = idx;
            s->assign.value = parse_expr(p);
            expect(p, TOK_SEMICOLON);
            return s;
        }

        /* Fall through to expression statement — backtrack */
        p->pos--;
    }

    /* Expression statement */
    Stmt *s = stmt_new(STMT_EXPR, loc);
    s->expr_stmt.expr = parse_expr(p);
    expect(p, TOK_SEMICOLON);
    return s;
}

/* ── Top-level program parser ──────────────────────────────────────────────── */

Program *parser_parse(Parser *p) {
    Program    *prog    = MAR_ALLOC(Program);
    FuncDecl  **funcs   = malloc(sizeof(FuncDecl *)  * 64);
    ClassDecl **classes = malloc(sizeof(ClassDecl *) * 64);
    int fc = 0, cc = 0;

    while (!check(p, TOK_EOF)) {
        if (check(p, TOK_CLASS)) {
            Stmt *s = parse_class(p);
            classes[cc++] = s->class_decl;
        } else {
            funcs[fc++] = parse_func(p);
        }
    }

    prog->funcs = MAR_ALLOC_N(FuncDecl *, fc);
    memcpy(prog->funcs, funcs, sizeof(FuncDecl *) * fc);
    prog->func_count = fc;

    prog->classes = MAR_ALLOC_N(ClassDecl *, cc);
    memcpy(prog->classes, classes, sizeof(ClassDecl *) * cc);
    prog->class_count = cc;

    free(funcs);
    free(classes);
    return prog;
}