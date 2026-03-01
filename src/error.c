#define _POSIX_C_SOURCE 200809L
#include "mar/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const char *const KIND_STR[] = {
    "LexerError", "ParserError", "SemanticError", "CodegenError"
};

ErrorCtx *error_ctx_create(const char *filename) {
    ErrorCtx *ec = calloc(1, sizeof(ErrorCtx));
    ec->source_file = filename;
    return ec;
}

void error_ctx_load_source(ErrorCtx *ec, const char *src) {
    char *copy = strdup(src);
    int lines = 1;
    for (char *p = copy; *p; p++)
        if (*p == '\n') lines++;
    ec->source_lines = calloc(lines, sizeof(char*));
    ec->line_count   = lines;
    int i = 0;
    char *line = strtok(copy, "\n");
    while (line) {
        ec->source_lines[i++] = strdup(line);
        line = strtok(NULL, "\n");
    }
}

void error_emit(ErrorCtx *ec, ErrorKind kind,
                SrcLoc loc, const char *fmt, ...) {
    if (ec->count >= MAX_ERRORS) return;
    MarError *e = &ec->errors[ec->count++];
    e->kind = kind;
    e->file = loc.file;
    e->line = loc.line;
    e->col  = loc.col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->message, sizeof(e->message), fmt, ap);
    va_end(ap);
}

void error_print_all(ErrorCtx *ec) {
    for (int i = 0; i < ec->count; i++) {
        MarError *e = &ec->errors[i];
        fprintf(stderr, "\n\033[1;31m%s\033[0m at \033[1m%s:%d:%d\033[0m\n",
            KIND_STR[e->kind], e->file ? e->file : "?", e->line, e->col);
        fprintf(stderr, "  \033[1m%s\033[0m\n", e->message);
        if (ec->source_lines && e->line > 0 && e->line <= ec->line_count) {
            fprintf(stderr, "\n  %4d | %s\n", e->line, ec->source_lines[e->line-1]);
            fprintf(stderr, "       | ");
            for (int c = 1; c < e->col; c++) fputc(' ', stderr);
            fprintf(stderr, "\033[1;33m^\033[0m\n");
        }
    }
    fprintf(stderr, "\n\033[1m%d error(s) found.\033[0m\n\n", ec->count);
}

void error_ctx_destroy(ErrorCtx *ec) {
    if (ec->source_lines) {
        for (int i = 0; i < ec->line_count; i++)
            free(ec->source_lines[i]);
        free(ec->source_lines);
    }
    free(ec);
}
