#ifndef MAR_CODEGEN_C_H
#define MAR_CODEGEN_C_H

#include "mar/ast.h"
#include "mar/error.h"
#include <stdio.h>
#include <stdbool.h>

typedef struct {
    FILE       *out;
    ErrorCtx   *errors;
    int         indent;
    int         tmp_counter;
} CGenCtx;

bool codegen_c_program(Program *prog, FILE *out, ErrorCtx *ec);

#endif
