#define _POSIX_C_SOURCE 200809L
/* ════════════════════════════════════════════════════════════════════════════
   Mar Language Server Protocol implementation
   Implements: initialize, textDocument/didOpen, textDocument/hover,
               textDocument/publishDiagnostics
   Transport: JSON-RPC 2.0 over stdin/stdout (LSP base protocol)
   ════════════════════════════════════════════════════════════════════════════ */
#include "mar/lsp.h"
#include "mar/arena.h"
#include "mar/error.h"
#include "mar/lexer.h"
#include "mar/parser.h"
#include "mar/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>

/* ── JSON output helpers ─────────────────────────────────────────────────── */

static void json_str(FILE *out, const char *s) {
    fputc('"', out);
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:   fputc(*p, out);     break;
        }
    }
    fputc('"', out);
}

/* Send a LSP response: Content-Length header + JSON body */
static void lsp_send(const char *json) {
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
    fflush(stdout);
}

static char *lsp_sprintf(const char *fmt, ...) {
    char *buf = malloc(8192);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 8192, fmt, ap);
    va_end(ap);
    return buf;
}

/* ── Simple JSON parser ───────────────────────────────────────────────────── */

/* Extract a string value for a given key from a flat JSON object.
   Not a full JSON parser — good enough for LSP messages. */
static char *json_get_str(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return NULL;
    pos += strlen(search);
    while (*pos && (*pos == ':' || *pos == ' ')) pos++;
    if (*pos != '"') return NULL;
    pos++; /* skip opening quote */
    char *result = malloc(8192);
    int i = 0;
    while (*pos && *pos != '"' && i < 8191) {
        if (*pos == '\\') {
            pos++;
            switch (*pos) {
                case '"':  result[i++] = '"';  break;
                case '\\': result[i++] = '\\'; break;
                case 'n':  result[i++] = '\n'; break;
                case 't':  result[i++] = '\t'; break;
                case 'r':  result[i++] = '\r'; break;
                default:   result[i++] = *pos; break;
            }
        } else {
            result[i++] = *pos;
        }
        pos++;
    }
    result[i] = '\0';
    return result;
}

static int json_get_int(const char *json, const char *key, int def) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return def;
    pos += strlen(search);
    while (*pos && (*pos == ':' || *pos == ' ')) pos++;
    if (!isdigit(*pos) && *pos != '-') return def;
    return atoi(pos);
}

/* ── LSP message reader ─────────────────────────────────────────────────────*/

/* Read one LSP message from stdin. Returns malloc'd JSON string, or NULL. */
static char *lsp_read_message(void) {
    char header[256];
    int content_length = -1;

    /* Read headers */
    while (fgets(header, sizeof(header), stdin)) {
        if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) break;
        if (strncasecmp(header, "Content-Length:", 15) == 0)
            content_length = atoi(header + 15);
    }

    if (content_length <= 0) return NULL;

    char *body = malloc((size_t)content_length + 1);
    int read_bytes = 0;
    while (read_bytes < content_length) {
        int n = (int)fread(body + read_bytes, 1,
                           (size_t)(content_length - read_bytes), stdin);
        if (n <= 0) break;
        read_bytes += n;
    }
    body[content_length] = '\0';
    return body;
}

/* ── Diagnostics: run parser on source, collect errors ──────────────────────*/

typedef struct {
    int  line;    /* 0-based (LSP convention) */
    int  col;     /* 0-based */
    int  severity;/* 1=error, 2=warning, 3=info */
    char message[512];
} LspDiag;

static int run_diagnostics(const char *source, const char *uri,
                            LspDiag *diags, int max_diags) {
    Arena    *arena = arena_create();
    Arena    *saved = g_arena;
    g_arena = arena;

    ErrorCtx *ec = error_ctx_create(uri);
    error_ctx_load_source(ec, source);
    Lexer  *lx = lexer_create(source, uri, ec);
    Token  *tk = lexer_tokenize(lx);
    Parser *pr = parser_create(tk, ec);
    /*Program *prog =*/ parser_parse(pr); /* run full parse */
    free(pr); free(lx);

    int n = ec->count < max_diags ? ec->count : max_diags;
    for (int i = 0; i < n; i++) {
        diags[i].line     = ec->errors[i].line > 0 ? ec->errors[i].line - 1 : 0;
        diags[i].col      = ec->errors[i].col  > 0 ? ec->errors[i].col  - 1 : 0;
        diags[i].severity = 1;
        strncpy(diags[i].message, ec->errors[i].message, 511);
    }

    error_ctx_destroy(ec);
    arena_destroy(arena);
    g_arena = saved;
    return n;
}

/* ── Build publishDiagnostics notification ───────────────────────────────── */

static char *make_diagnostics_notification(const char *uri,
                                           LspDiag *diags, int n) {
    /* Build JSON manually — no external deps */
    char *buf = malloc(65536);
    int  pos  = 0;

    pos += snprintf(buf + pos, 65536 - pos,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
        "\"params\":{\"uri\":");
    /* uri */
    int uri_len = (int)strlen(uri);
    buf[pos++] = '"';
    memcpy(buf + pos, uri, (size_t)uri_len); pos += uri_len;
    buf[pos++] = '"';

    pos += snprintf(buf + pos, 65536 - pos, ",\"diagnostics\":[");

    for (int i = 0; i < n; i++) {
        if (i) buf[pos++] = ',';
        pos += snprintf(buf + pos, 65536 - pos,
            "{\"range\":{"
                "\"start\":{\"line\":%d,\"character\":%d},"
                "\"end\":  {\"line\":%d,\"character\":%d}"
            "},\"severity\":%d,\"source\":\"mar\",\"message\":",
            diags[i].line, diags[i].col,
            diags[i].line, diags[i].col + 1,
            diags[i].severity);
        /* escape message */
        buf[pos++] = '"';
        for (const char *m = diags[i].message; *m; m++) {
            if (*m == '"')  { buf[pos++] = '\\'; buf[pos++] = '"'; }
            else if (*m == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
            else if (*m == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n'; }
            else buf[pos++] = *m;
        }
        buf[pos++] = '"';
        buf[pos++] = '}';
    }
    pos += snprintf(buf + pos, 65536 - pos, "]}}");
    buf[pos] = '\0';
    return buf;
}

/* ── Hover: provide keyword docs ────────────────────────────────────────── */

static const struct { const char *word; const char *doc; } HOVER_DOCS[] = {
    {"int",     "**int** — 64-bit signed integer type"},
    {"float",   "**float** — 64-bit IEEE 754 double precision float"},
    {"char",    "**char** — single character (8-bit)"},
    {"bool",    "**bool** — boolean: `true` or `false`"},
    {"string",  "**string** — immutable UTF-8 string (`const char*` in C)"},
    {"void",    "**void** — no return value"},
    {"null",    "**null** — null pointer literal"},
    {"if",      "**if** (cond) { … } — conditional branch"},
    {"else",    "**else** { … } — alternative branch"},
    {"while",   "**while** (cond) { … } — loop while condition is true"},
    {"for",     "**for** var **in** range(a, b) { … } — range-based loop\n"
                "**for** item **in** array { … } — array iteration loop"},
    {"return",  "**return** expr — return a value from a function"},
    {"break",   "**break** — exit from a loop or switch"},
    {"print",   "**print**(fmt, …) — formatted output (wraps `printf`)"},
    {"take",    "**take**(fmt, &var) — formatted input (wraps `scanf`)"},
    {"len",     "**len**(s) — length of a string (chars) or array (elements)"},
    {"class",   "**class** Name { fields; methods } — define an object type"},
    {"extends", "**class** Child **extends** Parent { … } — inherit from parent"},
    {"new",     "**new** ClassName(args) — allocate and initialize an object"},
    {"import",  "**import** \"path\" — include another .mar file"},
    {"switch",  "**switch** (expr) { case v { … } default { … } }"},
    {"true",    "**true** — boolean true"},
    {"false",   "**false** — boolean false"},
    {NULL, NULL}
};

static const char *hover_for_word(const char *word) {
    for (int i = 0; HOVER_DOCS[i].word; i++)
        if (strcmp(HOVER_DOCS[i].word, word) == 0)
            return HOVER_DOCS[i].doc;
    return NULL;
}

/* Extract word at (line, col) from source */
static char *word_at(const char *source, int line_0, int col_0) {
    int cur_line = 0, cur_col = 0;
    const char *p = source;
    /* advance to the line */
    while (*p && cur_line < line_0) {
        if (*p++ == '\n') cur_line++;
    }
    /* advance to col */
    while (*p && cur_col < col_0 && *p != '\n') { p++; cur_col++; }
    /* find word boundaries */
    const char *start = p;
    while (start > source && (isalnum(*(start-1)) || *(start-1) == '_'))
        start--;
    const char *end = p;
    while (*end && (isalnum(*end) || *end == '_')) end++;
    if (start == end) return NULL;
    char *word = malloc((size_t)(end - start) + 1);
    memcpy(word, start, (size_t)(end - start));
    word[end - start] = '\0';
    return word;
}

/* ── Main LSP server loop ────────────────────────────────────────────────── */

int lsp_run(void) {
    fprintf(stderr, "[mar-lsp] Language server started. Waiting for messages...\n");
    fflush(stderr);

    /* Track open documents: uri → content */
#define MAX_DOCS 64
    char *doc_uris[MAX_DOCS]    = {0};
    char *doc_sources[MAX_DOCS] = {0};
    int   doc_count = 0;

    while (1) {
        char *msg = lsp_read_message();
        if (!msg) {
            if (feof(stdin)) break;   /* editor disconnected — exit cleanly */
            continue;
        }

        /* Extract method */
        char *method = json_get_str(msg, "method");
        char *id_str = json_get_str(msg, "id");
        int   id     = id_str ? atoi(id_str) : -1;
        if (id_str) free(id_str);

        if (!method) { free(msg); continue; }

        /* ── initialize ── */
        if (strcmp(method, "initialize") == 0) {
            char resp[2048];
            snprintf(resp, sizeof(resp),
                "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{"
                    "\"capabilities\":{"
                        "\"textDocumentSync\":1,"
                        "\"hoverProvider\":true,"
                        "\"completionProvider\":{\"triggerCharacters\":[\".\"]}"
                    "},"
                    "\"serverInfo\":{\"name\":\"mar-lsp\",\"version\":\"1.0\"}"
                "}}",
                id);
            lsp_send(resp);
        }

        /* ── initialized ── */
        else if (strcmp(method, "initialized") == 0) {
            /* no-op */
        }

        /* ── shutdown ── */
        else if (strcmp(method, "shutdown") == 0) {
            char resp[128];
            snprintf(resp, sizeof(resp),
                "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":null}", id);
            lsp_send(resp);
        }

        /* ── exit ── */
        else if (strcmp(method, "exit") == 0) {
            free(msg); free(method);
            return 0;
        }

        /* ── textDocument/didOpen ── */
        else if (strcmp(method, "textDocument/didOpen") == 0) {
            char *uri  = json_get_str(msg, "uri");
            char *text = json_get_str(msg, "text");

            if (uri && text && doc_count < MAX_DOCS) {
                /* store document */
                doc_uris[doc_count]    = uri;
                doc_sources[doc_count] = text;
                doc_count++;

                /* run diagnostics */
                LspDiag diags[MAX_ERRORS];
                int nd = run_diagnostics(text, uri, diags, MAX_ERRORS);
                char *notif = make_diagnostics_notification(uri, diags, nd);
                lsp_send(notif);
                free(notif);
            } else {
                if (uri)  free(uri);
                if (text) free(text);
            }
        }

        /* ── textDocument/didChange ── */
        else if (strcmp(method, "textDocument/didChange") == 0) {
            char *uri  = json_get_str(msg, "uri");
            char *text = json_get_str(msg, "text");

            if (uri && text) {
                /* update stored doc */
                for (int i = 0; i < doc_count; i++) {
                    if (strcmp(doc_uris[i], uri) == 0) {
                        free(doc_sources[i]);
                        doc_sources[i] = text;
                        text = NULL;
                        break;
                    }
                }
                /* run diagnostics */
                const char *src = NULL;
                for (int i = 0; i < doc_count; i++)
                    if (strcmp(doc_uris[i], uri) == 0) { src = doc_sources[i]; break; }
                if (!src && text) src = text;

                if (src) {
                    LspDiag diags[MAX_ERRORS];
                    int nd = run_diagnostics(src, uri, diags, MAX_ERRORS);
                    char *notif = make_diagnostics_notification(uri, diags, nd);
                    lsp_send(notif);
                    free(notif);
                }
            }
            if (uri)  free(uri);
            if (text) free(text);
        }

        /* ── textDocument/hover ── */
        else if (strcmp(method, "textDocument/hover") == 0) {
            char *uri = json_get_str(msg, "uri");
            int   line = json_get_int(msg, "line", 0);
            int   col  = json_get_int(msg, "character", 0);

            const char *src = NULL;
            for (int i = 0; i < doc_count; i++)
                if (uri && strcmp(doc_uris[i], uri) == 0) { src = doc_sources[i]; break; }

            if (src) {
                char *word = word_at(src, line, col);
                const char *doc = word ? hover_for_word(word) : NULL;
                char resp[2048];
                if (doc) {
                    snprintf(resp, sizeof(resp),
                        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{"
                        "\"contents\":{\"kind\":\"markdown\",\"value\":", id);
                    char *buf = malloc(4096);
                    int p2 = (int)strlen(resp);
                    buf[0] = '"';
                    int bi = 1;
                    for (const char *c = doc; *c; c++) {
                        if (*c == '"') buf[bi++] = '\\';
                        buf[bi++] = *c;
                    }
                    buf[bi++] = '"';
                    buf[bi] = '\0';
                    strncat(resp, buf, sizeof(resp) - p2 - 10);
                    strncat(resp, "}}", sizeof(resp) - strlen(resp) - 2);
                    free(buf);
                } else {
                    snprintf(resp, sizeof(resp),
                        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":null}", id);
                }
                lsp_send(resp);
                if (word) free(word);
            } else {
                char resp[128];
                snprintf(resp, sizeof(resp),
                    "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":null}", id);
                lsp_send(resp);
            }
            if (uri) free(uri);
        }

        /* ── textDocument/completion ── */
        else if (strcmp(method, "textDocument/completion") == 0) {
            /* Return Mar keywords as completion items */
            const char *keywords[] = {
                "int","float","char","bool","void","string",
                "if","else","while","for","in","range",
                "switch","case","default","return","break",
                "print","take","len","class","new","extends",
                "null","import","true","false",NULL
            };
            char *buf = malloc(8192);
            int pos = 0;
            pos += snprintf(buf + pos, 8192 - pos,
                "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"items\":[", id);
            for (int i = 0; keywords[i]; i++) {
                if (i) buf[pos++] = ',';
                pos += snprintf(buf + pos, 8192 - pos,
                    "{\"label\":\"%s\",\"kind\":14}", keywords[i]);
            }
            pos += snprintf(buf + pos, 8192 - pos, "]}}");
            lsp_send(buf);
            free(buf);
        }

        /* ── unknown method — send empty response if it has an id ── */
        else if (id >= 0) {
            char resp[128];
            snprintf(resp, sizeof(resp),
                "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":null}", id);
            lsp_send(resp);
        }

        free(method);
        free(msg);
    }

    return 0;
}