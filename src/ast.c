#include "mar/ast.h"
#include <stdio.h>

MarType *type_new(TypeKind kind) {
    MarType *t = MAR_ALLOC(MarType);
    t->kind = kind;
    return t;
}

MarType *type_array(MarType *elem, int size) {
    MarType *t = MAR_ALLOC(MarType);
    t->kind = TY_ARRAY;
    t->elem = elem;
    t->size = size;
    return t;
}

Expr *expr_new(ExprKind kind, SrcLoc loc) {
    Expr *e = MAR_ALLOC(Expr);
    e->kind = kind;
    e->loc  = loc;
    return e;
}

Stmt *stmt_new(StmtKind kind, SrcLoc loc) {
    Stmt *s = MAR_ALLOC(Stmt);
    s->kind = kind;
    s->loc  = loc;
    return s;
}

static const char *type_str(MarType *t) {
    if (!t) return "?";
    switch (t->kind) {
        case TY_INT:   return "int";
        case TY_FLOAT: return "float";
        case TY_CHAR:  return "char";
        case TY_BOOL:  return "bool";
        case TY_VOID:  return "void";
        case TY_ARRAY: return "array";
        default:       return "unknown";
    }
}

void ast_print(Program *prog) {
    printf("Program: %d function(s)\n", prog->func_count);
    for (int i = 0; i < prog->func_count; i++) {
        FuncDecl *f = prog->funcs[i];
        printf("  fn %s() -> %s\n", f->name, type_str(f->return_type));
    }
}
