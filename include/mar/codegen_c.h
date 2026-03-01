#ifndef MAR_CODEGEN_C_H
#define MAR_CODEGEN_C_H

#include <stdio.h>
#include <stdbool.h>
#include "mar/ast.h"    /* Program, FuncDecl, ClassDecl, Stmt, Expr, MarType */
#include "mar/error.h"  /* ErrorCtx */

/* Code generation context */
typedef struct {
    FILE     *out;
    ErrorCtx *errors;
    int       indent;
    int       tmp_counter;
} CGenCtx;

/* Public API — implemented in codegen_c.c */
bool codegen_c_program(Program *prog, FILE *out, ErrorCtx *ec);

#endif /* MAR_CODEGEN_C_H */