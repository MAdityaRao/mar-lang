#include "mar/codegen_c.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/* ── Indentation ───────────────────────────────────────────────────────────── */

static void ind(CGenCtx *c) {
    for (int i = 0; i < c->indent * 4; i++) fputc(' ', c->out);
}

/* ── Type emission ─────────────────────────────────────────────────────────── */

static void emit_type(CGenCtx *c, MarType *t) {
    if (!t) { fprintf(c->out, "void"); return; }
    switch (t->kind) {
        case TY_INT:     fprintf(c->out, "int");          break;
        case TY_FLOAT:   fprintf(c->out, "double");       break;
        case TY_CHAR:    fprintf(c->out, "char");         break;
        case TY_BOOL:    fprintf(c->out, "bool");         break;
        case TY_VOID:    fprintf(c->out, "void");         break;
        case TY_ARRAY:   emit_type(c, t->elem);           break;
        case TY_UNKNOWN: fprintf(c->out, "%s*", t->name); break; /* class pointer */
        default:         fprintf(c->out, "int");          break;
    }
}

/* ── Operator strings ──────────────────────────────────────────────────────── */

static const char *op_str(Operator op) {
    switch (op) {
        case OP_ADD: return "+";  case OP_SUB: return "-";
        case OP_MUL: return "*";  case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQ:  return "=="; case OP_NEQ: return "!=";
        case OP_LT:  return "<";  case OP_GT:  return ">";
        case OP_LTE: return "<="; case OP_GTE: return ">=";
        case OP_AND: return "&&"; case OP_OR:  return "||";
        default:     return "+";
    }
}

/* ── String literal escaping ───────────────────────────────────────────────── */

static void emit_str_escaped(CGenCtx *c, const char *s) {
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '\n': fprintf(c->out, "\\n");  break;
            case '\t': fprintf(c->out, "\\t");  break;
            case '"':  fprintf(c->out, "\\\""); break;
            case '\\': fprintf(c->out, "\\\\"); break;
            default:   fputc(*p, c->out);       break;
        }
    }
}

/* ── Expression emission ───────────────────────────────────────────────────── */

static void emit_expr(CGenCtx *c, Expr *e) {
    switch (e->kind) {
        case EXPR_INT_LIT:
            fprintf(c->out, "%" PRId64, e->ival); break;
        case EXPR_FLOAT_LIT:
            fprintf(c->out, "%g", e->fval); break;
        case EXPR_CHAR_LIT:
            fprintf(c->out, "'%c'", e->cval); break;
        case EXPR_BOOL_LIT:
            fprintf(c->out, "%s", e->bval ? "true" : "false"); break;
        case EXPR_STRING_LIT:
            fprintf(c->out, "\"");
            emit_str_escaped(c, e->sval);
            fprintf(c->out, "\"");
            break;
        case EXPR_IDENT:
            fprintf(c->out, "%s", e->name); break;
        case EXPR_INDEX:
            emit_expr(c, e->idx.array);
            fprintf(c->out, "[");
            emit_expr(c, e->idx.index);
            fprintf(c->out, "]");
            break;
        case EXPR_BINARY:
            fprintf(c->out, "(");
            emit_expr(c, e->binary.left);
            fprintf(c->out, " %s ", op_str(e->binary.op));
            emit_expr(c, e->binary.right);
            fprintf(c->out, ")");
            break;
        case EXPR_UNARY:
            fprintf(c->out, e->unary.op == OP_NOT ? "(!" : "(-");
            emit_expr(c, e->unary.operand);
            fprintf(c->out, ")");
            break;
        case EXPR_CALL:
            fprintf(c->out, "%s(", e->call.callee);
            for (int i = 0; i < e->call.argc; i++) {
                if (i) fprintf(c->out, ", ");
                emit_expr(c, e->call.args[i]);
            }
            fprintf(c->out, ")");
            break;
        case EXPR_ADDR_OF:
            fprintf(c->out, "&");
            emit_expr(c, e->addr.operand);
            break;
        case EXPR_MEMBER_ACCESS:
            /* dot operator on class pointers: p.health  →  p->health */
            emit_expr(c, e->member.left);
            fprintf(c->out, "->%s", e->member.name);
            break;
        case EXPR_NEW:
            /* 'new Foo(args)' → GNU statement expression allocating via arena */
            fprintf(c->out, "({ %s* _obj = arena_alloc(g_arena, sizeof(%s)); ",
                    e->new_obj.class_name, e->new_obj.class_name);
            fprintf(c->out, "%s_init(_obj", e->new_obj.class_name);
            for (int i = 0; i < e->new_obj.argc; i++) {
                fprintf(c->out, ", ");
                emit_expr(c, e->new_obj.args[i]);
            }
            fprintf(c->out, "); _obj; })");
            break;
        default:
            fprintf(c->out, "0"); break;
    }
}

/* ── Statement emission ────────────────────────────────────────────────────── */

static void emit_stmt(CGenCtx *c, Stmt *s) {
    switch (s->kind) {
        case STMT_VAR_DECL:
            ind(c);
            emit_type(c, s->var_decl.type);
            fprintf(c->out, " %s", s->var_decl.name);
            if (s->var_decl.type && s->var_decl.type->kind == TY_ARRAY)
                fprintf(c->out, "[%d]", s->var_decl.type->size);
            if (s->var_decl.array_init) {
                fprintf(c->out, " = {");
                for (int i = 0; i < s->var_decl.array_init_count; i++) {
                    if (i) fprintf(c->out, ", ");
                    emit_expr(c, s->var_decl.array_init[i]);
                }
                fprintf(c->out, "}");
            } else if (s->var_decl.init) {
                fprintf(c->out, " = ");
                emit_expr(c, s->var_decl.init);
            }
            fprintf(c->out, ";\n");
            break;

        case STMT_ASSIGN:
            ind(c);
            emit_expr(c, s->assign.target);
            switch (s->assign.op) {
                case OP_ASSIGN:       fprintf(c->out, " = ");   break;
                case OP_PLUS_ASSIGN:  fprintf(c->out, " += ");  break;
                case OP_MINUS_ASSIGN: fprintf(c->out, " -= ");  break;
                case OP_STAR_ASSIGN:  fprintf(c->out, " *= ");  break;
                case OP_SLASH_ASSIGN: fprintf(c->out, " /= ");  break;
                case OP_PCT_ASSIGN:   fprintf(c->out, " %%= "); break;
                default:              fprintf(c->out, " = ");   break;
            }
            emit_expr(c, s->assign.value);
            fprintf(c->out, ";\n");
            break;

        case STMT_IF:
            ind(c);
            fprintf(c->out, "if (");
            emit_expr(c, s->if_stmt.cond);
            fprintf(c->out, ") ");
            if (s->if_stmt.then_branch->kind != STMT_BLOCK) {
                fprintf(c->out, "{\n");
                c->indent++;
                emit_stmt(c, s->if_stmt.then_branch);
                c->indent--;
                ind(c); fprintf(c->out, "}");
            } else {
                emit_stmt(c, s->if_stmt.then_branch);
            }
            if (s->if_stmt.else_branch) {
                ind(c); fprintf(c->out, "else ");
                if (s->if_stmt.else_branch->kind != STMT_BLOCK) {
                    fprintf(c->out, "{\n");
                    c->indent++;
                    emit_stmt(c, s->if_stmt.else_branch);
                    c->indent--;
                    ind(c); fprintf(c->out, "}\n");
                } else {
                    emit_stmt(c, s->if_stmt.else_branch);
                }
            }
            break;

        case STMT_WHILE:
            ind(c);
            fprintf(c->out, "while (");
            emit_expr(c, s->while_stmt.cond);
            fprintf(c->out, ") ");
            emit_stmt(c, s->while_stmt.body);
            break;

        case STMT_FOR_RANGE: {
            int tmp = c->tmp_counter++;
            ind(c); fprintf(c->out, "{\n");
            c->indent++;
            ind(c); fprintf(c->out, "int %s = ", s->for_range.var);
            emit_expr(c, s->for_range.start);
            fprintf(c->out, ";\n");
            ind(c); fprintf(c->out, "int _mar_end_%d = ", tmp);
            emit_expr(c, s->for_range.end);
            fprintf(c->out, ";\n");
            ind(c); fprintf(c->out, "while (%s < _mar_end_%d) {\n",
                s->for_range.var, tmp);
            c->indent++;
            if (s->for_range.body->kind == STMT_BLOCK) {
                for (int i = 0; i < s->for_range.body->block.count; i++)
                    emit_stmt(c, s->for_range.body->block.stmts[i]);
            } else {
                emit_stmt(c, s->for_range.body);
            }
            ind(c); fprintf(c->out, "%s += 1;\n", s->for_range.var);
            c->indent--;
            ind(c); fprintf(c->out, "}\n");
            c->indent--;
            ind(c); fprintf(c->out, "}\n");
            break;
        }

        case STMT_SWITCH:
            ind(c);
            fprintf(c->out, "switch (");
            emit_expr(c, s->switch_stmt.expr);
            fprintf(c->out, ") {\n");
            c->indent++;
            for (int i = 0; i < s->switch_stmt.case_count; i++) {
                CaseClause *cl = &s->switch_stmt.cases[i];
                ind(c);
                if (cl->value) {
                    fprintf(c->out, "case ");
                    emit_expr(c, cl->value);
                    fprintf(c->out, ":\n");
                } else {
                    fprintf(c->out, "default:\n");
                }
                c->indent++;
                bool has_break = false;
                for (int j = 0; j < cl->body_count; j++) {
                    emit_stmt(c, cl->body[j]);
                    if (cl->body[j]->kind == STMT_BREAK) has_break = true;
                }
                if (!has_break) { ind(c); fprintf(c->out, "break;\n"); }
                c->indent--;
            }
            c->indent--;
            ind(c); fprintf(c->out, "}\n");
            break;

        case STMT_RETURN:
            ind(c);
            if (s->ret.value) {
                fprintf(c->out, "return ");
                emit_expr(c, s->ret.value);
                fprintf(c->out, ";\n");
            } else {
                fprintf(c->out, "return;\n");
            }
            break;

        case STMT_BREAK:
            ind(c); fprintf(c->out, "break;\n"); break;

        case STMT_PRINT:
            ind(c);
            fprintf(c->out, "printf(\"");
            emit_str_escaped(c, s->print.fmt);
            fprintf(c->out, "\"");
            for (int i = 0; i < s->print.argc; i++) {
                fprintf(c->out, ", ");
                emit_expr(c, s->print.args[i]);
            }
            fprintf(c->out, ");\n");
            break;

        case STMT_TAKE:
            ind(c);
            fprintf(c->out, "scanf(\"");
            for (const char *p = s->take.fmt; *p; p++) {
                if (*p == '"') fprintf(c->out, "\\\"");
                else           fputc(*p, c->out);
            }
            fprintf(c->out, "\"");
            for (int i = 0; i < s->take.argc; i++) {
                fprintf(c->out, ", ");
                emit_expr(c, s->take.args[i]);
            }
            fprintf(c->out, ");\n");
            break;

        case STMT_BLOCK:
            fprintf(c->out, "{\n");
            c->indent++;
            for (int i = 0; i < s->block.count; i++)
                emit_stmt(c, s->block.stmts[i]);
            c->indent--;
            ind(c); fprintf(c->out, "}\n");
            break;

        case STMT_EXPR:
            ind(c);
            emit_expr(c, s->expr_stmt.expr);
            fprintf(c->out, ";\n");
            break;

        default: break;
    }
}

/* ── Class emission ────────────────────────────────────────────────────────── */

static void emit_class(CGenCtx *c, ClassDecl *cls) {
    fprintf(c->out, "/* Class: %s */\n", cls->name);
    fprintf(c->out, "typedef struct {\n");
    for (int i = 0; i < cls->field_count; i++) {
        fprintf(c->out, "    ");
        emit_type(c, cls->fields[i].type);
        fprintf(c->out, " %s;\n", cls->fields[i].name);
    }
    fprintf(c->out, "} %s;\n\n", cls->name);

    for (int i = 0; i < cls->method_count; i++) {
        FuncDecl *m = cls->methods[i].method;
        emit_type(c, m->return_type);
        fprintf(c->out, " %s_%s(%s* this", cls->name, m->name, cls->name);
        for (int j = 0; j < m->param_count; j++) {
            fprintf(c->out, ", ");
            emit_type(c, m->param_types[j]);
            fprintf(c->out, " %s", m->param_names[j]);
        }
        fprintf(c->out, ") ");
        emit_stmt(c, m->body);
        fprintf(c->out, "\n");
    }
}

/* ── Public entry point ────────────────────────────────────────────────────── */

bool codegen_c_program(Program *prog, FILE *out, ErrorCtx *ec) {
    CGenCtx ctx = {.out=out, .errors=ec, .indent=0, .tmp_counter=0};

    fprintf(out, "/* Generated by mar compiler */\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdbool.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <math.h>\n");
    fprintf(out, "extern void* arena_alloc(void*, size_t);\n");
    fprintf(out, "extern void* g_arena;\n\n");

    /* 1. Emit class structs and methods first (functions may reference them) */
    for (int i = 0; i < prog->class_count; i++)
        emit_class(&ctx, prog->classes[i]);

    /* 2. Emit function prototypes */
    for (int i = 0; i < prog->func_count; i++) {
        FuncDecl *f = prog->funcs[i];
        emit_type(&ctx, f->return_type);
        fprintf(out, " %s(", f->name);
        if (f->param_count == 0) fprintf(out, "void");
        for (int j = 0; j < f->param_count; j++) {
            if (j) fprintf(out, ", ");
            emit_type(&ctx, f->param_types[j]);
            fprintf(out, " %s", f->param_names[j]);
        }
        fprintf(out, ");\n");
    }

    /* 3. Emit function bodies */
    fprintf(out, "\n");
    for (int i = 0; i < prog->func_count; i++) {
        FuncDecl *f = prog->funcs[i];
        emit_type(&ctx, f->return_type);
        fprintf(out, " %s(", f->name);
        if (f->param_count == 0) fprintf(out, "void");
        for (int j = 0; j < f->param_count; j++) {
            if (j) fprintf(out, ", ");
            emit_type(&ctx, f->param_types[j]);
            fprintf(out, " %s", f->param_names[j]);
        }
        fprintf(out, ") ");
        emit_stmt(&ctx, f->body);
        fprintf(out, "\n");
    }

    return ec->count == 0;
}