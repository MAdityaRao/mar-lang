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
        fprintf(stderr, "Mar Compiler v0.1\n");
        fprintf(stderr, "Usage: mar <file.mar> [-o output.c]\n");
        fprintf(stderr, "       mar <file.mar> --dump-tokens\n");
        fprintf(stderr, "       mar <file.mar> --dump-ast\n");
        return 1;
    }

    const char *input  = argv[1];
    const char *output = "a.out.c";
    bool dump_tokens   = false;
    bool dump_ast      = false;

    for (int i = 2; i < argc; i++) {
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
        printf("[%3d:%2d] EOF\n", tokens[lexer->token_count-1].line, 0);
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

    printf("Compiled: %s → %s\n", input, output);
    arena_destroy(g_arena);
    free(source);
    return 0;
}
