#include "mar/arena.h"
#include "mar/error.h"
#include "mar/lexer.h"
#include "mar/parser.h"
#include "mar/codegen_c.h"
#include "mar/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 2);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Mar Compiler v1.0.1\n");
        fprintf(stderr, "Usage: mar <file.mar> [options]\n");
        fprintf(stderr, "       mar run <file.mar>   (Compile and run immediately)\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -o <file>        Output C file name\n");
        fprintf(stderr, "  --dump-tokens    Print lexer output\n");
        fprintf(stderr, "  --dump-ast       Print parser output\n");
        return 1;
    }

    bool run_immediately = false;
    int arg_offset = 1;

    // Check if the first argument is 'run'
    if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'mar run' requires a filename.\n");
            return 1;
        }
        run_immediately = true;
        arg_offset = 2; // Shift indices to ignore 'run'
    }

    const char *input  = argv[arg_offset];
    const char *output = run_immediately ? "/tmp/mar_temp.c" : "a.out.c";
    bool dump_tokens   = false;
    bool dump_ast      = false;

    // Start loop after the input filename
    for (int i = arg_offset + 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--dump-tokens") == 0) dump_tokens = true;
        else if (strcmp(argv[i], "--dump-ast")    == 0) dump_ast    = true;
    }

    /* setup */
    g_arena = arena_create();
    ErrorCtx *ec = error_ctx_create(input);
    char *source  = read_file(input);
    error_ctx_load_source(ec, source);

    /* lex */
    Lexer  *lexer  = lexer_create(source, input, ec);
    Token  *tokens = lexer_tokenize(lexer);

    if (dump_tokens) {
        for (int i = 0; tokens[i].kind != TOK_EOF; i++)
            printf("[%3d:%2d] %-20s %s\n",
                tokens[i].line, tokens[i].col,
                token_kind_str(tokens[i].kind),
                tokens[i].value ? tokens[i].value : "");
        arena_destroy(g_arena); free(source); return 0;
    }

    if (ec->count) { error_print_all(ec); arena_destroy(g_arena); free(source); return 1; }

    /* parse */
    Parser  *parser = parser_create(tokens, ec);
    Program *prog   = parser_parse(parser);

    if (dump_ast) { ast_print(prog); arena_destroy(g_arena); free(source); return 0; }
    if (ec->count) { error_print_all(ec); arena_destroy(g_arena); free(source); return 1; }

    /* codegen */
    FILE *out = fopen(output, "w");
    if (!out) { perror(output); return 1; }
    bool ok = codegen_c_program(prog, out, ec);
    fclose(out);

    if (!ok || ec->count) {
        error_print_all(ec);
        arena_destroy(g_arena); free(source);
        return 1;
    }

    /* Execution Logic for 'mar run' */
    if (run_immediately) {
        char cmd[512];
        // 1. Compile generated C to a binary in /tmp
        snprintf(cmd, sizeof(cmd), "cc %s -o /tmp/mar_bin", output);
        if (system(cmd) != 0) {
            fprintf(stderr, "Error: C compilation failed.\n");
            return 1;
        }
        // 2. Execute the binary
        system("/tmp/mar_bin");
    } else {
        printf("Compiled: %s → %s\n", input, output);
    }

    arena_destroy(g_arena);
    free(source);
    return 0;
}