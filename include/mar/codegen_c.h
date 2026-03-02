#ifndef MAR_CODEGEN_C_H
#define MAR_CODEGEN_C_H

#include <stdio.h>
#include <stdbool.h>
#include "mar/ast.h"
#include "mar/error.h"

typedef struct {
    FILE        *out;
    ErrorCtx    *errors;
    int          indent;
    int          tmp_counter;
    const char  *current_func_name; /* used by STMT_MULTI_RETURN to name the struct */
    /* multi-return struct registry (for deduplication) */
    char       **tuple_types;
    int          tuple_count;
    int          tuple_cap;
    /* full program reference (for class lookup / inheritance) */
    Program     *prog;
} CGenCtx;

bool codegen_c_program(Program *prog, FILE *out, ErrorCtx *ec);

#endif