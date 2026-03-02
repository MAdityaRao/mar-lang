#define _POSIX_C_SOURCE 200809L
#include "mar/codegen_c.h"
#include "mar/ast.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/*
 * codegen_c.c - Mar → C code generator
 *
 * Traverses a fully-parsed Program AST and writes equivalent C source to
 * an output FILE.  The generated file is self-contained: it includes its
 * own runtime helpers and requires only a standard C11 compiler + -lm.
 *
 * Constraints:
 *   - Class instances are always heap-allocated (pointer semantics).
 *   - String concatenation allocates from an 8 MB static bump arena.
 *   - len() on arrays relies on a companion _mar_len_<name> variable
 *     emitted alongside every array declaration.
 *   - Multi-return functions use a per-function typedef'd struct.
 */

/* ---------------------------------------------------------------------------
 * Indentation
 * ------------------------------------------------------------------------- */

static void ind(CGenCtx *c)
{
    for (int i = 0; i < c->indent * 4; i++)
        fputc(' ', c->out);
}

/* ---------------------------------------------------------------------------
 * Class lookup
 * ------------------------------------------------------------------------- */

static ClassDecl *find_class(CGenCtx *c, const char *name)
{
    if (!name || !c->prog)
        return NULL;
    for (int i = 0; i < c->prog->class_count; i++)
        if (strcmp(c->prog->classes[i]->name, name) == 0)
            return c->prog->classes[i];
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Type emission
 * ------------------------------------------------------------------------- */

static void emit_type(CGenCtx *c, MarType *t)
{
    if (!t) { fprintf(c->out, "void"); return; }
    switch (t->kind) {
        case TY_INT:     fprintf(c->out, "int64_t");      break;
        case TY_FLOAT:   fprintf(c->out, "double");       break;
        case TY_CHAR:    fprintf(c->out, "char");         break;
        case TY_BOOL:    fprintf(c->out, "bool");         break;
        case TY_VOID:    fprintf(c->out, "void");         break;
        case TY_STRING:  fprintf(c->out, "const char*");  break;
        case TY_NULL:    fprintf(c->out, "void*");        break;
        case TY_ARRAY:   emit_type(c, t->elem);           break;
        case TY_UNKNOWN:
            if (t->name) fprintf(c->out, "%s*", t->name);
            else         fprintf(c->out, "void*");
            break;
        default:         fprintf(c->out, "int64_t");      break;
    }
}

/* Emit a full declarator, e.g. "int64_t arr[8]" for array types. */
static void emit_type_decl(CGenCtx *c, MarType *t, const char *varname)
{
    if (t && t->kind == TY_ARRAY) {
        emit_type(c, t->elem);
        fprintf(c->out, " %s[%d]", varname, t->size > 0 ? t->size : 0);
    } else {
        emit_type(c, t);
        fprintf(c->out, " %s", varname);
    }
}

/*
 * Emit a function's return type.
 *
 * The C standard requires main() to return int.  Mar declares it as
 * returning int too, but the internal representation maps int → int64_t,
 * which causes a -Wmain-return-type warning.  We special-case it here.
 */
static void emit_return_type(CGenCtx *c, FuncDecl *f, FILE *out)
{
    if (strcmp(f->name, "main") == 0) {
        fprintf(out, "int");
    } else if (f->return_type_count > 1) {
        fprintf(out, "_mar_ret_%s", f->name);
    } else {
        emit_type(c, f->return_type);
    }
}

/* ---------------------------------------------------------------------------
 * Operator strings
 * ------------------------------------------------------------------------- */

static const char *op_str(Operator op)
{
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQ:  return "==";
        case OP_NEQ: return "!=";
        case OP_LT:  return "<";
        case OP_GT:  return ">";
        case OP_LTE: return "<=";
        case OP_GTE: return ">=";
        case OP_AND: return "&&";
        case OP_OR:  return "||";
        default:     return "+";
    }
}

/* ---------------------------------------------------------------------------
 * String literal escaping
 * ------------------------------------------------------------------------- */

static void emit_str_escaped(CGenCtx *c, const char *s)
{
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '\n': fprintf(c->out, "\\n");   break;
            case '\t': fprintf(c->out, "\\t");   break;
            case '"':  fprintf(c->out, "\\\"");  break;
            case '\\': fprintf(c->out, "\\\\");  break;
            default:   fputc(*p, c->out);        break;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Variable → class-name scope (for method call resolution)
 *
 * Maps local variable names to their declared class so that a call like
 * p.damage() can be lowered to Player_damage(p, ...).
 * ------------------------------------------------------------------------- */

#define MAX_SCOPE 512

typedef struct {
    char *var;
    char *cls;
} VarClass;

static VarClass g_scope[MAX_SCOPE];
static int      g_scope_depth = 0;

static void scope_push(const char *var, const char *cls)
{
    if (g_scope_depth < MAX_SCOPE && var && cls) {
        g_scope[g_scope_depth].var = strdup(var);
        g_scope[g_scope_depth].cls = strdup(cls);
        g_scope_depth++;
    }
}

static const char *scope_lookup(const char *var)
{
    if (!var) return NULL;
    for (int i = g_scope_depth - 1; i >= 0; i--)
        if (strcmp(g_scope[i].var, var) == 0)
            return g_scope[i].cls;
    return NULL;
}

static void scope_clear(void)
{
    for (int i = 0; i < g_scope_depth; i++) {
        free(g_scope[i].var);
        free(g_scope[i].cls);
    }
    g_scope_depth = 0;
}

/* ---------------------------------------------------------------------------
 * String variable tracker (for + → _mar_strcat promotion)
 *
 * Records which local variable names hold string values so binary OP_ADD
 * between two string operands is emitted as _mar_strcat() instead of +.
 * ------------------------------------------------------------------------- */

#define MAX_STRVARS 512

static char *g_strvars[MAX_STRVARS];
static int   g_strvar_count = 0;

static void strvar_push(const char *name)
{
    if (g_strvar_count < MAX_STRVARS && name)
        g_strvars[g_strvar_count++] = strdup(name);
}

static bool strvar_is_string(const char *name)
{
    if (!name) return false;
    for (int i = 0; i < g_strvar_count; i++)
        if (strcmp(g_strvars[i], name) == 0)
            return true;
    return false;
}

static void strvar_clear(void)
{
    for (int i = 0; i < g_strvar_count; i++)
        free(g_strvars[i]);
    g_strvar_count = 0;
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Return the class name of an object expression, or NULL if unknown. */
static const char *resolve_obj_class(Expr *obj)
{
    if (!obj) return NULL;
    if (obj->kind == EXPR_NEW)   return obj->new_obj.class_name;
    if (obj->kind == EXPR_IDENT) return scope_lookup(obj->name);
    return NULL;
}

/*
 * Return true if an expression is (or produces) a string value.
 * Used to decide whether binary + should become _mar_strcat.
 */
static bool is_string_expr(Expr *e)
{
    if (!e) return false;
    if (e->kind == EXPR_STRING_LIT) return true;
    if (e->kind == EXPR_STR_CONCAT) return true;
    if (e->kind == EXPR_IDENT)      return strvar_is_string(e->name);
    if (e->kind == EXPR_BINARY && e->binary.op == OP_ADD)
        return is_string_expr(e->binary.left) ||
               is_string_expr(e->binary.right);
    return false;
}

/* ---------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */

static void emit_expr(CGenCtx *c, Expr *e);
static void emit_stmt(CGenCtx *c, Stmt *s);

/* ---------------------------------------------------------------------------
 * Method call emission
 *
 * Mar method calls obj.method(args) lower to ClassName_method(obj, args).
 * If the class cannot be resolved at codegen time we emit a void no-op with
 * a diagnostic comment so the output at least compiles.
 * ------------------------------------------------------------------------- */

static void emit_method_call(CGenCtx *c, Expr *e)
{
    const char *cls = resolve_obj_class(e->method_call.obj);
    if (cls) {
        fprintf(c->out, "%s_%s(", cls, e->method_call.method);
        emit_expr(c, e->method_call.obj);
        for (int i = 0; i < e->method_call.argc; i++) {
            fprintf(c->out, ", ");
            emit_expr(c, e->method_call.args[i]);
        }
        fputc(')', c->out);
    } else {
        fprintf(c->out, "/* unresolved: ");
        emit_expr(c, e->method_call.obj);
        fprintf(c->out, ".%s */ ((void)0)", e->method_call.method);
    }
}

/* ---------------------------------------------------------------------------
 * Expression emission
 * ------------------------------------------------------------------------- */

static void emit_expr(CGenCtx *c, Expr *e)
{
    switch (e->kind) {

        case EXPR_INT_LIT:
            fprintf(c->out, "%" PRId64, e->ival);
            break;

        case EXPR_FLOAT_LIT:
            fprintf(c->out, "%g", e->fval);
            break;

        case EXPR_CHAR_LIT:
            if      (e->cval == '\'') fprintf(c->out, "'\\''");
            else if (e->cval == '\n') fprintf(c->out, "'\\n'");
            else if (e->cval == '\t') fprintf(c->out, "'\\t'");
            else if (e->cval == '\\') fprintf(c->out, "'\\\\'");
            else if (e->cval == '\0') fprintf(c->out, "'\\0'");
            else                      fprintf(c->out, "'%c'", e->cval);
            break;

        case EXPR_STRING_LIT:
            fputc('"', c->out);
            emit_str_escaped(c, e->sval);
            fputc('"', c->out);
            break;

        case EXPR_BOOL_LIT:
            fprintf(c->out, "%s", e->bval ? "true" : "false");
            break;

        case EXPR_NULL:
            fprintf(c->out, "NULL");
            break;

        case EXPR_IDENT:
            fprintf(c->out, "%s", e->name);
            break;

        case EXPR_INDEX:
            emit_expr(c, e->idx.array);
            fputc('[', c->out);
            emit_expr(c, e->idx.index);
            fputc(']', c->out);
            break;

        case EXPR_LEN:
            /*
             * len(x) dispatches on the variable's declared type:
             *   string variable  → strlen(x)
             *   array variable   → _mar_len_x  (companion var from STMT_VAR_DECL)
             *   other expression → strlen(expr) fallback
             */
            if (e->len_expr.arg && e->len_expr.arg->kind == EXPR_IDENT) {
                if (strvar_is_string(e->len_expr.arg->name))
                    fprintf(c->out, "(int64_t)strlen(%s)",
                            e->len_expr.arg->name);
                else
                    fprintf(c->out, "_mar_len_%s", e->len_expr.arg->name);
            } else {
                fprintf(c->out, "(int64_t)strlen(");
                emit_expr(c, e->len_expr.arg);
                fputc(')', c->out);
            }
            break;

        case EXPR_STR_CONCAT:
            fprintf(c->out, "_mar_strcat(");
            emit_expr(c, e->concat.left);
            fprintf(c->out, ", ");
            emit_expr(c, e->concat.right);
            fputc(')', c->out);
            break;

        case EXPR_BINARY:
            /* string + string → _mar_strcat */
            if (e->binary.op == OP_ADD &&
                (is_string_expr(e->binary.left) ||
                 is_string_expr(e->binary.right))) {
                fprintf(c->out, "_mar_strcat(");
                emit_expr(c, e->binary.left);
                fprintf(c->out, ", ");
                emit_expr(c, e->binary.right);
                fputc(')', c->out);
            } else {
                fputc('(', c->out);
                emit_expr(c, e->binary.left);
                fprintf(c->out, " %s ", op_str(e->binary.op));
                emit_expr(c, e->binary.right);
                fputc(')', c->out);
            }
            break;

        case EXPR_UNARY:
            fprintf(c->out, e->unary.op == OP_NOT ? "(!" : "(-");
            emit_expr(c, e->unary.operand);
            fputc(')', c->out);
            break;

        case EXPR_ADDR_OF:
            fputc('&', c->out);
            emit_expr(c, e->addr.operand);
            break;

        case EXPR_CALL:
            if (strcmp(e->call.callee, "char") == 0) {
                fprintf(c->out, "((char)(");
                emit_expr(c, e->call.args[0]);
                fprintf(c->out, "))");
            } else if (strcmp(e->call.callee, "int") == 0) {
                fprintf(c->out, "((int64_t)(");
                emit_expr(c, e->call.args[0]);
                fprintf(c->out, "))");
            } else if (strcmp(e->call.callee, "float") == 0) {
                fprintf(c->out, "((double)(");
                emit_expr(c, e->call.args[0]);
                fprintf(c->out, "))");
            } else {
                fprintf(c->out, "%s(", e->call.callee);
                for (int i = 0; i < e->call.argc; i++) {
                    if (i) fprintf(c->out, ", ");
                    emit_expr(c, e->call.args[i]);
                }
                fputc(')', c->out);
            }
            break;

        case EXPR_MEMBER_ACCESS:
            /* All Mar class instances are pointers; use -> not . */
            emit_expr(c, e->member.left);
            fprintf(c->out, "->%s", e->member.name);
            break;

        case EXPR_METHOD_CALL:
            emit_method_call(c, e);
            break;

        case EXPR_NEW: {
            /*
             * new Foo(args) →
             *   ({ Foo* _obj_N = (Foo*)_mar_arena_alloc(sizeof(Foo));
             *      Foo_init(_obj_N, args);
             *      _obj_N; })
             *
             * Uses GCC/Clang statement-expression extension.
             */
            int n = c->tmp_counter++;
            fprintf(c->out,
                "({ %s* _obj_%d = (%s*)_mar_arena_alloc(sizeof(%s)); "
                "%s_init(_obj_%d",
                e->new_obj.class_name, n,
                e->new_obj.class_name, e->new_obj.class_name,
                e->new_obj.class_name, n);
            for (int i = 0; i < e->new_obj.argc; i++) {
                fprintf(c->out, ", ");
                emit_expr(c, e->new_obj.args[i]);
            }
            fprintf(c->out, "); _obj_%d; })", n);
            break;
        }

        default:
            fprintf(c->out, "0 /* unknown expr kind %d */", (int)e->kind);
            break;
    }
}

/* ---------------------------------------------------------------------------
 * Statement emission
 * ------------------------------------------------------------------------- */

static void emit_stmt(CGenCtx *c, Stmt *s)
{
    switch (s->kind) {

        case STMT_VAR_DECL: {
            ind(c);
            MarType *t = s->var_decl.type;

            if (t && t->kind == TY_ARRAY) {
                emit_type(c, t->elem);
                fprintf(c->out, " %s[%d]", s->var_decl.name,
                        t->size > 0 ? t->size : 0);
                if (s->var_decl.array_init) {
                    fprintf(c->out, " = {");
                    for (int i = 0; i < s->var_decl.array_init_count; i++) {
                        if (i) fprintf(c->out, ", ");
                        emit_expr(c, s->var_decl.array_init[i]);
                    }
                    fputc('}', c->out);
                }
                fprintf(c->out, ";\n");
                /*
                 * Companion length variable so len(arr) and for-in-array
                 * loops can query the element count at runtime.
                 */
                ind(c);
                int arr_len = s->var_decl.array_init_count > 0
                                  ? s->var_decl.array_init_count
                                  : (t->size > 0 ? t->size : 0);
                fprintf(c->out, "int64_t _mar_len_%s = %d;\n",
                        s->var_decl.name, arr_len);
                return;
            }

            if (t && t->kind == TY_UNKNOWN && t->name)
                scope_push(s->var_decl.name, t->name);
            if (t && t->kind == TY_STRING)
                strvar_push(s->var_decl.name);

            emit_type(c, t);
            fprintf(c->out, " %s", s->var_decl.name);
            if (s->var_decl.init) {
                fprintf(c->out, " = ");
                emit_expr(c, s->var_decl.init);
            }
            fprintf(c->out, ";\n");
            break;
        }

        case STMT_ASSIGN: {
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
        }

        case STMT_IF: {
            ind(c);
            fprintf(c->out, "if (");
            emit_expr(c, s->if_stmt.cond);
            fprintf(c->out, ") ");

            if (s->if_stmt.then_branch->kind == STMT_BLOCK) {
                emit_stmt(c, s->if_stmt.then_branch);
            } else {
                fprintf(c->out, "{\n");
                c->indent++;
                emit_stmt(c, s->if_stmt.then_branch);
                c->indent--;
                ind(c); fputc('}', c->out);
            }

            if (s->if_stmt.else_branch) {
                fprintf(c->out, " else ");
                if (s->if_stmt.else_branch->kind == STMT_BLOCK) {
                    emit_stmt(c, s->if_stmt.else_branch);
                } else {
                    fprintf(c->out, "{\n");
                    c->indent++;
                    emit_stmt(c, s->if_stmt.else_branch);
                    c->indent--;
                    ind(c); fprintf(c->out, "}\n");
                }
            } else {
                fputc('\n', c->out);
            }
            break;
        }

        case STMT_WHILE: {
            ind(c);
            fprintf(c->out, "while (");
            emit_expr(c, s->while_stmt.cond);
            fprintf(c->out, ") ");
            emit_stmt(c, s->while_stmt.body);
            break;
        }

        case STMT_FOR_RANGE: {
            /*
             * for i in range(start, end) →
             *   { int64_t i = start;
             *     int64_t _mar_end_N = end;
             *     while (i < _mar_end_N) { body; i += 1; } }
             */
            int tmp = c->tmp_counter++;
            ind(c); fprintf(c->out, "{\n");
            c->indent++;
            ind(c); fprintf(c->out, "int64_t %s = ", s->for_range.var);
            emit_expr(c, s->for_range.start);
            fprintf(c->out, ";\n");
            ind(c); fprintf(c->out, "int64_t _mar_end_%d = ", tmp);
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

        case STMT_FOR_ARRAY: {
            /*
             * for item in arr →
             *   { for (int64_t _mar_i_N = 0;
             *          _mar_i_N < _mar_len_arr;
             *          _mar_i_N++) {
             *       __auto_type item = arr[_mar_i_N]; body; } }
             */
            int tmp = c->tmp_counter++;
            ind(c); fprintf(c->out, "{\n");
            c->indent++;
            ind(c); fprintf(c->out,
                "for (int64_t _mar_i_%d = 0; "
                "_mar_i_%d < _mar_len_%s; "
                "_mar_i_%d++) {\n",
                tmp, tmp, s->for_array.arr, tmp);
            c->indent++;
            ind(c); fprintf(c->out, "__auto_type %s = %s[_mar_i_%d];\n",
                            s->for_array.var, s->for_array.arr, tmp);
            if (s->for_array.body->kind == STMT_BLOCK) {
                for (int i = 0; i < s->for_array.body->block.count; i++)
                    emit_stmt(c, s->for_array.body->block.stmts[i]);
            } else {
                emit_stmt(c, s->for_array.body);
            }
            c->indent--;
            ind(c); fprintf(c->out, "}\n");
            c->indent--;
            ind(c); fprintf(c->out, "}\n");
            break;
        }

        case STMT_SWITCH: {
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
                    if (cl->body[j]->kind == STMT_BREAK)
                        has_break = true;
                }
                if (!has_break) {
                    ind(c);
                    fprintf(c->out, "break;\n");
                }
                c->indent--;
            }
            c->indent--;
            ind(c); fprintf(c->out, "}\n");
            break;
        }

        case STMT_RETURN: {
            ind(c);
            if (s->ret.value) {
                fprintf(c->out, "return ");
                emit_expr(c, s->ret.value);
                fprintf(c->out, ";\n");
            } else {
                fprintf(c->out, "return;\n");
            }
            break;
        }

        case STMT_MULTI_RETURN: {
            /* return a, b → return (_mar_ret_func){ ._r0 = a, ._r1 = b }; */
            ind(c);
            fprintf(c->out, "return (_mar_ret_%s){", c->current_func_name);
            for (int i = 0; i < s->multi_ret.value_count; i++) {
                if (i) fprintf(c->out, ", ");
                fprintf(c->out, "._r%d = ", i);
                emit_expr(c, s->multi_ret.values[i]);
            }
            fprintf(c->out, "};\n");
            break;
        }

        case STMT_MULTI_ASSIGN: {
            /*
             * int a, int b = swap(x, y) →
             *   _mar_ret_swap _mar_t_N = swap(x, y);
             *   int64_t a = _mar_t_N._r0;
             *   int64_t b = _mar_t_N._r1;
             */
            int tmp = c->tmp_counter++;
            const char *callee = "unknown";
            if (s->multi_assign.rhs &&
                s->multi_assign.rhs->kind == EXPR_CALL)
                callee = s->multi_assign.rhs->call.callee;
            ind(c);
            fprintf(c->out, "_mar_ret_%s _mar_t_%d = ", callee, tmp);
            emit_expr(c, s->multi_assign.rhs);
            fprintf(c->out, ";\n");
            for (int i = 0; i < s->multi_assign.count; i++) {
                ind(c);
                emit_type(c, s->multi_assign.types[i]);
                fprintf(c->out, " %s = _mar_t_%d._r%d;\n",
                        s->multi_assign.names[i], tmp, i);
            }
            break;
        }

        case STMT_BREAK:
            ind(c);
            fprintf(c->out, "break;\n");
            break;

        case STMT_PRINT: {
            ind(c);
            for (int i = 0; i < s->print.argc; i++) {
                fprintf(c->out, "_mar_print(");
                emit_expr(c, s->print.args[i]);
                fprintf(c->out, ");\n");
                
                if (i < s->print.argc - 1) {
                    ind(c);
                    fprintf(c->out, "printf(\" \");\n");
                }
            }
            ind(c);
            fprintf(c->out, "printf(\"\\n\");\n");
            break;
        }

        case STMT_TAKE: {
            ind(c);
            fprintf(c->out, "scanf(\"");
            for (const char *p = s->print.fmt; *p; p++) {
                if      (*p == '"')  fprintf(c->out, "\\\"");
                else if (*p == '\\') fprintf(c->out, "\\\\");
                else                 fputc(*p, c->out);
            }
            fputc('"', c->out);
            for (int i = 0; i < s->print.argc; i++) {
                fprintf(c->out, ", ");
                emit_expr(c, s->print.args[i]);
            }
            fprintf(c->out, ");\n");
            break;
        }

        case STMT_BLOCK: {
            fprintf(c->out, "{\n");
            c->indent++;
            for (int i = 0; i < s->block.count; i++)
                emit_stmt(c, s->block.stmts[i]);
            c->indent--;
            ind(c);
            fprintf(c->out, "}\n");
            break;
        }

        case STMT_EXPR: {
            ind(c);
            if (s->expr_stmt.expr->kind == EXPR_METHOD_CALL)
                emit_method_call(c, s->expr_stmt.expr);
            else
                emit_expr(c, s->expr_stmt.expr);
            fprintf(c->out, ";\n");
            break;
        }

        case STMT_IMPORT:
        case STMT_CLASS_DECL:
            /* Handled at parse time and by emit_class() respectively. */
            break;

        default:
            break;
    }
}

/* ---------------------------------------------------------------------------
 * Multi-return tuple struct
 *
 * Each function with multiple return values gets a typedef'd struct named
 * _mar_ret_<funcname> so both the prototype and body can reference it.
 * ------------------------------------------------------------------------- */

static void emit_tuple_struct(CGenCtx *c, FuncDecl *f)
{
    if (f->return_type_count <= 1) return;
    fprintf(c->out, "typedef struct { ");
    for (int i = 0; i < f->return_type_count; i++) {
        emit_type(c, f->return_types[i]);
        fprintf(c->out, " _r%d; ", i);
    }
    fprintf(c->out, "} _mar_ret_%s;\n", f->name);
}

/* ---------------------------------------------------------------------------
 * Class emission
 *
 * Emits the struct typedef, method prototypes, and method bodies.
 * For inherited classes the parent fields are embedded first so a child
 * pointer can be cast to a parent pointer safely.
 * ------------------------------------------------------------------------- */

static void emit_class(CGenCtx *c, ClassDecl *cls)
{
    fprintf(c->out, "/* class %s", cls->name);
    if (cls->parent_name)
        fprintf(c->out, " extends %s", cls->parent_name);
    fprintf(c->out, " */\n");
    fprintf(c->out, "typedef struct _%s {\n", cls->name);

    /* Inherited fields come first for ABI-compatible casting. */
    if (cls->parent_name) {
        ClassDecl *parent = find_class(c, cls->parent_name);
        if (parent) {
            for (int i = 0; i < parent->field_count; i++) {
                fprintf(c->out, "    ");
                if (parent->fields[i].type &&
                    parent->fields[i].type->kind == TY_ARRAY) {
                    emit_type(c, parent->fields[i].type->elem);
                    fprintf(c->out, " %s[%d]",
                            parent->fields[i].name,
                            parent->fields[i].type->size > 0
                                ? parent->fields[i].type->size : 1);
                } else {
                    emit_type_decl(c, parent->fields[i].type,
                                   parent->fields[i].name);
                }
                fprintf(c->out, "; /* inherited from %s */\n", parent->name);
            }
        }
    }

    for (int i = 0; i < cls->field_count; i++) {
        fprintf(c->out, "    ");
        if (cls->fields[i].type &&
            cls->fields[i].type->kind == TY_ARRAY) {
            emit_type(c, cls->fields[i].type->elem);
            fprintf(c->out, " %s[%d]",
                    cls->fields[i].name,
                    cls->fields[i].type->size > 0
                        ? cls->fields[i].type->size : 1);
        } else {
            emit_type_decl(c, cls->fields[i].type, cls->fields[i].name);
        }
        fprintf(c->out, ";\n");
    }
    fprintf(c->out, "} %s;\n\n", cls->name);

    /* Forward-declare all methods before their bodies. */
    for (int i = 0; i < cls->method_count; i++) {
        FuncDecl *m = cls->methods[i].method;
        emit_type(c, m->return_type);
        fprintf(c->out, " %s_%s(%s* this", cls->name, m->name, cls->name);
        for (int j = 0; j < m->param_count; j++) {
            fprintf(c->out, ", ");
            if (m->param_types[j]->kind == TY_ARRAY) {
                emit_type(c, m->param_types[j]->elem);
                fprintf(c->out, " %s[]", m->param_names[j]);
            } else {
                emit_type(c, m->param_types[j]);
                fprintf(c->out, " %s", m->param_names[j]);
            }
        }
        fprintf(c->out, ");\n");
    }
    fprintf(c->out, "\n");

    /* Method bodies. */
    for (int i = 0; i < cls->method_count; i++) {
        FuncDecl *m = cls->methods[i].method;
        c->current_func_name = m->name;

        emit_type(c, m->return_type);
        fprintf(c->out, " %s_%s(%s* this", cls->name, m->name, cls->name);
        for (int j = 0; j < m->param_count; j++) {
            fprintf(c->out, ", ");
            if (m->param_types[j]->kind == TY_ARRAY) {
                emit_type(c, m->param_types[j]->elem);
                fprintf(c->out, " %s[]", m->param_names[j]);
            } else {
                emit_type(c, m->param_types[j]);
                fprintf(c->out, " %s", m->param_names[j]);
            }
        }
        fprintf(c->out, ") {\n");
        c->indent++;

        /*
         * Emit #define macros that alias bare field names to this->field.
         * This allows method bodies to write  health = 100  instead of
         * this->health = 100.  Macros are #undef'd at the end of the body.
         */
        if (cls->parent_name) {
            ClassDecl *parent = find_class(c, cls->parent_name);
            if (parent) {
                for (int j = 0; j < parent->field_count; j++) {
                    ind(c);
                    fprintf(c->out, "#define %s (this->%s)\n",
                            parent->fields[j].name, parent->fields[j].name);
                }
            }
        }
        for (int j = 0; j < cls->field_count; j++) {
            ind(c);
            fprintf(c->out, "#define %s (this->%s)\n",
                    cls->fields[j].name, cls->fields[j].name);
        }

        if (m->body && m->body->kind == STMT_BLOCK) {
            for (int j = 0; j < m->body->block.count; j++)
                emit_stmt(c, m->body->block.stmts[j]);
        }

        if (cls->parent_name) {
            ClassDecl *parent = find_class(c, cls->parent_name);
            if (parent) {
                for (int j = 0; j < parent->field_count; j++) {
                    ind(c);
                    fprintf(c->out, "#undef %s\n", parent->fields[j].name);
                }
            }
        }
        for (int j = 0; j < cls->field_count; j++) {
            ind(c);
            fprintf(c->out, "#undef %s\n", cls->fields[j].name);
        }

        c->indent--;
        fprintf(c->out, "}\n\n");
    }

    /*
     * For each parent method not overridden in the child, emit a thin
     * wrapper that casts the child pointer and delegates to the parent
     * implementation.
     */
    if (!cls->parent_name) return;
    ClassDecl *parent = find_class(c, cls->parent_name);
    if (!parent) return;

    for (int i = 0; i < parent->method_count; i++) {
        bool overridden = false;
        for (int j = 0; j < cls->method_count; j++) {
            if (strcmp(cls->methods[j].name,
                       parent->methods[i].name) == 0) {
                overridden = true;
                break;
            }
        }
        if (overridden) continue;

        FuncDecl *m = parent->methods[i].method;
        emit_type(c, m->return_type);
        fprintf(c->out, " %s_%s(%s* this",
                cls->name, m->name, cls->name);
        for (int j = 0; j < m->param_count; j++) {
            fprintf(c->out, ", ");
            emit_type(c, m->param_types[j]);
            fprintf(c->out, " %s", m->param_names[j]);
        }
        fprintf(c->out, ") {\n");
        c->indent++;
        ind(c);
        bool has_ret = m->return_type && m->return_type->kind != TY_VOID;
        if (has_ret) fprintf(c->out, "return ");
        fprintf(c->out, "%s_%s((%s*)this",
                parent->name, m->name, parent->name);
        for (int j = 0; j < m->param_count; j++)
            fprintf(c->out, ", %s", m->param_names[j]);
        fprintf(c->out, ");\n");
        c->indent--;
        fprintf(c->out, "}\n\n");
    }
}

/* ---------------------------------------------------------------------------
 * Runtime helpers injected into every generated file
 *
 * Provides a small bump allocator, string concatenation, and wrappers for
 * the mar/math, mar/str, and mar/io standard library modules.
 * ------------------------------------------------------------------------- */

static void emit_stdlib_helpers(FILE *out)
{
    fprintf(out,
        "/* Mar runtime helpers */\n"
        "#define _mar_print(x) _Generic((x), \\\n"
        "    int64_t: printf(\"%%ld\", (int64_t)(x)), \\\n"
        "    double:  printf(\"%%lf\", (double)(x)), \\\n"
        "    char:    printf(\"%%c\", (char)(x)), \\\n"
        "    char*:   printf(\"%%s\", (char*)(x)), \\\n"
        "    const char*: printf(\"%%s\", (const char*)(x)), \\\n"
        "    default: printf(\"%%p\", (void*)(x)) \\\n"
        ")\n"
        "static char   _mar_heap[8 * 1024 * 1024];\n"
        "static size_t _mar_heap_used = 0;\n"
        "\n"
        "static void *_mar_arena_alloc(size_t sz) {\n"
        "    sz = (sz + 7u) & ~7u;\n"
        "    if (_mar_heap_used + sz > sizeof(_mar_heap))\n"
        "        return malloc(sz);\n"
        "    void *p = _mar_heap + _mar_heap_used;\n"
        "    _mar_heap_used += sz;\n"
        "    return p;\n"
        "}\n"
        "\n"
        "void *g_arena = NULL;\n"
        "void *arena_alloc(void *a, size_t sz) { (void)a; return _mar_arena_alloc(sz); }\n"
        "\n"
        "static const char *_mar_strcat(const char *a, const char *b) {\n"
        "    if (!a) a = \"\";\n"
        "    if (!b) b = \"\";\n"
        "    size_t la = strlen(a), lb = strlen(b);\n"
        "    char *buf = (char *)_mar_arena_alloc(la + lb + 1);\n"
        "    memcpy(buf, a, la);\n"
        "    memcpy(buf + la, b, lb);\n"
        "    buf[la + lb] = '\\0';\n"
        "    return buf;\n"
        "}\n"
        "\n"
        "/* mar/math */\n"
        "static int64_t mar_min(int64_t a, int64_t b)  { return a < b ? a : b; }\n"
        "static int64_t mar_max(int64_t a, int64_t b)  { return a > b ? a : b; }\n"
        "static int64_t mar_abs(int64_t a)              { return a < 0 ? -a : a; }\n"
        "static double  mar_sqrt(double a)              { return sqrt(a); }\n"
        "static double  mar_pow(double a, double b)     { return pow(a, b); }\n"
        "static double  mar_floor(double a)             { return floor(a); }\n"
        "static double  mar_ceil(double a)              { return ceil(a); }\n"
        "\n"
        "/* mar/str */\n"
        "static int64_t     mar_str_len(const char *s)            { return (int64_t)strlen(s); }\n"
        "static int64_t     mar_str_cmp(const char *a, const char *b) { return strcmp(a, b); }\n"
        "static int64_t     mar_str_to_int(const char *s)         { return atoll(s); }\n"
        "static const char *mar_int_to_str(int64_t n) {\n"
        "    char *buf = (char *)_mar_arena_alloc(32);\n"
        "    snprintf(buf, 32, \"%%lld\", (long long)n);\n"
        "    return buf;\n"
        "}\n"
        "\n"
        "/* mar/io */\n"
        "static const char *mar_file_read(const char *path) {\n"
        "    FILE *f = fopen(path, \"r\");\n"
        "    if (!f) return \"\";\n"
        "    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);\n"
        "    char *buf = (char *)_mar_arena_alloc((size_t)sz + 1);\n"
        "    fread(buf, 1, (size_t)sz, f); buf[sz] = '\\0';\n"
        "    fclose(f);\n"
        "    return buf;\n"
        "}\n"
        "static void mar_file_write(const char *path, const char *content) {\n"
        "    FILE *f = fopen(path, \"w\");\n"
        "    if (f) { fprintf(f, \"%%s\", content); fclose(f); }\n"
        "}\n"
        "\n"
    );
}

/* ---------------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------------- */

bool codegen_c_program(Program *prog, FILE *out, ErrorCtx *ec)
{
    CGenCtx ctx = {
        .out               = out,
        .errors            = ec,
        .indent            = 0,
        .tmp_counter       = 0,
        .prog              = prog,
        .current_func_name = "main",
        .tuple_types       = malloc(sizeof(char *) * 64),
        .tuple_count       = 0,
        .tuple_cap         = 64,
    };

    /* Standard headers. */
    fprintf(out, "/* Generated by Mar compiler */\n");
    fprintf(out, "#pragma GCC diagnostic ignored \"-Wformat\"\n");
    fprintf(out, "#pragma GCC diagnostic ignored \"-Wformat-extra-args\"\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdbool.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n");
    fprintf(out, "#include <math.h>\n");
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <inttypes.h>\n");
    fprintf(out, "extern void *arena_alloc(void *a, size_t size);\n");
    fprintf(out, "extern void *g_arena;\n");

    /* Extra includes from import statements, deduplicated. */
    for (int i = 0; i < prog->c_include_count; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(prog->c_includes[j], prog->c_includes[i]) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) fprintf(out, "%s\n", prog->c_includes[i]);
    }
    fprintf(out, "\n");

    emit_stdlib_helpers(out);

    /* Multi-return structs must precede any function that uses them. */
    bool any_tuple = false;
    for (int i = 0; i < prog->func_count; i++) {
        if (prog->funcs[i]->return_type_count > 1) {
            emit_tuple_struct(&ctx, prog->funcs[i]);
            any_tuple = true;
        }
    }
    if (any_tuple) fprintf(out, "\n");

    /* Class structs and method bodies. */
    for (int i = 0; i < prog->class_count; i++)
        emit_class(&ctx, prog->classes[i]);

    /* Forward-declare all functions so call order in source doesn't matter. */
    for (int i = 0; i < prog->func_count; i++) {
        FuncDecl *f = prog->funcs[i];
        emit_return_type(&ctx, f, out);
        fprintf(out, " %s(", f->name);
        if (f->param_count == 0) fprintf(out, "void");
        for (int j = 0; j < f->param_count; j++) {
            if (j) fprintf(out, ", ");
            if (f->param_types[j]->kind == TY_ARRAY) {
                emit_type(&ctx, f->param_types[j]->elem);
                fprintf(out, " %s[]", f->param_names[j]);
            } else {
                emit_type(&ctx, f->param_types[j]);
                fprintf(out, " %s", f->param_names[j]);
            }
        }
        fprintf(out, ");\n");
    }
    fprintf(out, "\n");

    /* Function bodies. */
    for (int i = 0; i < prog->func_count; i++) {
        FuncDecl *f = prog->funcs[i];
        scope_clear();
        strvar_clear();
        ctx.current_func_name = f->name;

        for (int j = 0; j < f->param_count; j++) {
            if (f->param_types[j]->kind == TY_UNKNOWN &&
                f->param_types[j]->name)
                scope_push(f->param_names[j], f->param_types[j]->name);
            if (f->param_types[j]->kind == TY_STRING)
                strvar_push(f->param_names[j]);
        }

        emit_return_type(&ctx, f, out);
        fprintf(out, " %s(", f->name);
        if (f->param_count == 0) fprintf(out, "void");
        for (int j = 0; j < f->param_count; j++) {
            if (j) fprintf(out, ", ");
            if (f->param_types[j]->kind == TY_ARRAY) {
                emit_type(&ctx, f->param_types[j]->elem);
                fprintf(out, " %s[]", f->param_names[j]);
            } else {
                emit_type(&ctx, f->param_types[j]);
                fprintf(out, " %s", f->param_names[j]);
            }
        }
        fprintf(out, ") ");
        emit_stmt(&ctx, f->body);
        fprintf(out, "\n");
    }

    free(ctx.tuple_types);
    return ec->count == 0;
}