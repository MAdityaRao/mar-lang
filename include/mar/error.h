#ifndef MAR_ERROR_H
#define MAR_ERROR_H

#include <stdbool.h>

typedef struct {
    int         line;
    int         col;
    const char *file;
} SrcLoc;

typedef enum {
    ERR_LEXER,
    ERR_PARSER,
    ERR_SEMANTIC,
    ERR_CODEGEN,
} ErrorKind;

typedef struct {
    ErrorKind   kind;
    const char *file;
    int         line;
    int         col;
    char        message[512];
} MarError;

#define MAX_ERRORS 256

typedef struct {
    MarError    errors[MAX_ERRORS];
    int         count;
    const char *source_file;
    char      **source_lines;
    int         line_count;
} ErrorCtx;

ErrorCtx *error_ctx_create(const char *filename);
void      error_ctx_load_source(ErrorCtx *ec, const char *src);
void      error_emit(ErrorCtx *ec, ErrorKind kind,
                     SrcLoc loc, const char *fmt, ...);
void      error_print_all(ErrorCtx *ec);
void      error_ctx_destroy(ErrorCtx *ec);

#endif