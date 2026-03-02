#include "mar/ast.h"
#include "mar/arena.h"
#include <stdio.h>
#include <string.h>

/* ── Type constructors ────────────────────────────────────────────────────── */

MarType *type_new(TypeKind kind) {
    MarType *t = MAR_ALLOC(MarType);
    t->kind    = kind;
    t->name    = NULL;
    t->elem    = NULL;
    t->size    = 0;
    return t;
}

MarType *type_array(MarType *elem, int size) {
    MarType *t = MAR_ALLOC(MarType);
    t->kind    = TY_ARRAY;
    t->elem    = elem;
    t->size    = size;
    t->name    = NULL;
    return t;
}

/* ── Expression constructor ───────────────────────────────────────────────── */

Expr *expr_new(ExprKind kind, SrcLoc loc) {
    Expr *e  = MAR_ALLOC(Expr);
    e->kind  = kind;
    e->line  = loc.line;
    e->col   = loc.col;
    /* all other fields are zero-initialised by arena_alloc */
    return e;
}

/* ── Statement constructor ────────────────────────────────────────────────── */

Stmt *stmt_new(StmtKind kind, SrcLoc loc) {
    Stmt *s  = MAR_ALLOC(Stmt);
    s->kind  = kind;
    s->line  = loc.line;
    s->col   = loc.col;
    return s;
}

/* ── Debug helpers ────────────────────────────────────────────────────────── */

const char *typekind_str(TypeKind k) {
    switch (k) {
        case TY_INT:     return "int";
        case TY_FLOAT:   return "float";
        case TY_CHAR:    return "char";
        case TY_BOOL:    return "bool";
        case TY_VOID:    return "void";
        case TY_STRING:  return "string";
        case TY_ARRAY:   return "array";
        case TY_NULL:    return "null";
        case TY_UNKNOWN: return "class";
        default:         return "?";
    }
}

const char *op_str_dbg(Operator op) {
    switch (op) {
        case OP_ADD: return "+";  case OP_SUB: return "-";
        case OP_MUL: return "*";  case OP_DIV: return "/";
        case OP_MOD: return "%";  case OP_EQ:  return "==";
        case OP_NEQ: return "!="; case OP_LT:  return "<";
        case OP_GT:  return ">";  case OP_LTE: return "<=";
        case OP_GTE: return ">="; case OP_AND: return "&&";
        case OP_OR:  return "||"; case OP_NEG: return "-";
        case OP_NOT: return "!";
        default:     return "?";
    }
}

void ast_print(Program *prog) {
    printf("Program: %d function(s), %d class(es)\n",
           prog->func_count, prog->class_count);
    for (int i = 0; i < prog->class_count; i++) {
        ClassDecl *c = prog->classes[i];
        printf("  class %s", c->name);
        if (c->parent_name) printf(" extends %s", c->parent_name);
        printf(" { %d fields, %d methods }\n", c->field_count, c->method_count);
    }
    for (int i = 0; i < prog->func_count; i++) {
        FuncDecl *f = prog->funcs[i];
        printf("  fn %s(%d params) -> %s\n",
               f->name, f->param_count,
               f->return_type ? typekind_str(f->return_type->kind) : "void");
    }
}