#define _POSIX_C_SOURCE 200809L
#include "mar/arena.h"
#include "mar/error.h"
#include "mar/lexer.h"
#include "mar/parser.h"
#include "mar/codegen_c.h"
#include "mar/ast.h"
#include "mar/lsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 2);
    if (!buf) { fprintf(stderr, "Out of memory\n"); exit(1); }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* Extract directory portion of a path */
static char *dir_of(const char *path) {
    const char *last_slash = strrchr(path, '/');
#ifdef _WIN32
    const char *last_bslash = strrchr(path, '\\');
    if (last_bslash > last_slash) last_slash = last_bslash;
#endif
    if (!last_slash) return strdup(".");
    char *dir = malloc((size_t)(last_slash - path) + 1);
    memcpy(dir, path, (size_t)(last_slash - path));
    dir[last_slash - path] = '\0';
    return dir;
}

static void print_usage(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "\033[1;36m  Mar Language Compiler v1.2.0\033[0m\n\n");
    fprintf(stderr, "  \033[1mUsage:\033[0m\n");
    fprintf(stderr, "    mar run  <file.mar>          Compile & run immediately\n");
    fprintf(stderr, "    mar      <file.mar> [opts]   Compile to C\n");
    fprintf(stderr, "    mar lsp                      Start language server (LSP)\n\n");
    fprintf(stderr, "  \033[1mOptions:\033[0m\n");
    fprintf(stderr, "    -o <file>          Output C file  (default: a.out.c)\n");
    fprintf(stderr, "    --dump-tokens      Print lexer tokens and exit\n");
    fprintf(stderr, "    --dump-ast         Print AST summary and exit\n\n");
    fprintf(stderr, "  \033[1mNew features:\033[0m\n");
    fprintf(stderr, "    string, null, import, extends, len(), for-in-array\n");
    fprintf(stderr, "    multiple return values, stdlib (mar/math, mar/str, mar/io)\n\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 1; }

    /* ── LSP mode — FEATURE 9 ── */
    if (strcmp(argv[1], "lsp") == 0) {
        /* Set binary mode on Windows */
        return lsp_run();
    }

    bool run_immediately = false;
    int  arg_offset      = 1;

    if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'mar run' requires a filename.\n");
            return 1;
        }
        run_immediately = true;
        arg_offset      = 2;
    }

    if (strcmp(argv[arg_offset], "--help") == 0 ||
        strcmp(argv[arg_offset], "-h") == 0) {
        print_usage();
        return 0;
    }

    const char *input  = argv[arg_offset];
    const char *output = run_immediately ? "/tmp/mar_temp.c" : "a.out.c";
    bool dump_tokens   = false;
    bool dump_ast      = false;

    for (int i = arg_offset + 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--dump-tokens") == 0) dump_tokens = true;
        else if (strcmp(argv[i], "--dump-ast")    == 0) dump_ast    = true;
    }

    /* ── Setup ── */
    g_arena = arena_create();
    ErrorCtx *ec = error_ctx_create(input);
    char *source  = read_file(input);
    error_ctx_load_source(ec, source);

    /* ── Lex ── */
    Lexer  *lexer  = lexer_create(source, input, ec);
    Token  *tokens = lexer_tokenize(lexer);

    if (dump_tokens) {
        for (int i = 0; tokens[i].kind != TOK_EOF; i++)
            printf("[%3d:%2d] %-20s %s\n",
                tokens[i].line, tokens[i].col,
                token_kind_str(tokens[i].kind),
                tokens[i].value ? tokens[i].value : "");
        arena_destroy(g_arena); free(source); free(lexer); return 0;
    }

    if (ec->count) {
        error_print_all(ec);
        arena_destroy(g_arena); free(source); free(lexer); return 1;
    }

    /* ── Parse ── */
    Parser  *parser = parser_create(tokens, ec);
    parser->source_dir = dir_of(input);  /* FEATURE 2: for import resolution */
    Program *prog   = parser_parse(parser);

    if (dump_ast) {
        ast_print(prog);
        arena_destroy(g_arena); free(source); free(lexer); free(parser); return 0;
    }

    if (ec->count) {
        error_print_all(ec);
        arena_destroy(g_arena); free(source); free(lexer); free(parser); return 1;
    }

    /* ── Codegen ── */
    FILE *out = fopen(output, "w");
    if (!out) { perror(output); return 1; }
    bool ok = codegen_c_program(prog, out, ec);
    fclose(out);

    if (!ok || ec->count) {
        error_print_all(ec);
        arena_destroy(g_arena); free(source); free(lexer); free(parser);
        return 1;
    }

    /* ── Run (mar run mode) ── */
    if (run_immediately) {
        char compile_cmd[2048];
        const char *bin_path = "/tmp/mar_bin";

        snprintf(compile_cmd, sizeof(compile_cmd),
            "cc %s -o %s -lm 2>&1", output, bin_path);

        int cc_ret = system(compile_cmd);
        if (cc_ret == 0) {
            int run_ret = system(bin_path);
            remove(bin_path);
            remove(output);
            arena_destroy(g_arena); free(source); free(lexer); free(parser);
            return run_ret;
        } else {
            fprintf(stderr,
                "\033[1;31mError:\033[0m C compilation failed. "
                "Generated C is at: %s\n", output);
            arena_destroy(g_arena); free(source); free(lexer); free(parser);
            return 1;
        }
    } else {
        /* We want to compile to a native binary and delete the intermediate C file */
        char compile_cmd[2048];
        char bin_name[256];
        
        /* Figure out the final binary name (e.g. change "a.out.c" to "a.out") */
        snprintf(bin_name, sizeof(bin_name), "%s", output);
        char *dot_c = strstr(bin_name, ".c");
        if (dot_c) *dot_c = '\0'; /* strip .c extension */
        else snprintf(bin_name, sizeof(bin_name), "mar_prog");

        /* Tell GCC to compile it silently */
        snprintf(compile_cmd, sizeof(compile_cmd), "cc %s -o %s -lm", output, bin_name);
        
        int cc_ret = system(compile_cmd);
        if (cc_ret == 0) {
            remove(output);
            remove(bin_name); /* Silently delete the intermediate .c file! */
            printf("\033[1;32m✓\033[0m Successfully built native binary: \033[1m%s\033[0m\n", bin_name);
        } else {
            fprintf(stderr, "\033[1;31mError:\033[0m C compilation failed. Generated C is at: %s\n", output);
            return 1;
        }
    }

    arena_destroy(g_arena);
    free(source);
    free(lexer);
    free(parser);
    return 0;
}