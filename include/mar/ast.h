#pragma once
/*
 * Mar Language — AST definitions
 * Single source of truth for all node types, enums, and structs.
 * Every field name here EXACTLY matches what parser.c and codegen_c.c use.
 */
#include <stddef.h>
#include <stdint.h>
#include "arena.h"
#include "error.h"

/* ─── Type kinds ──────────────────────────────────────────────────────────── */
typedef enum {
    TY_UNKNOWN = 0,   /* user-defined class / unresolved */
    TY_INT,
    TY_FLOAT,
    TY_CHAR,
    TY_BOOL,
    TY_VOID,
    TY_STRING,        /* FEATURE 1  */
    TY_NULL,          /* FEATURE 5  */
    TY_ARRAY,
} TypeKind;

typedef struct MarType {
    TypeKind        kind;
    char           *name;      /* TY_UNKNOWN: class name */
    struct MarType *elem;      /* TY_ARRAY:  element type */
    int             size;      /* TY_ARRAY:  static size (0 = unknown) */
} MarType;

/* ─── Operators ────────────────────────────────────────────────────────────── */
typedef enum {
    /* arithmetic */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    /* comparison */
    OP_EQ,  OP_NEQ, OP_LT,  OP_GT,  OP_LTE, OP_GTE,
    /* logical */
    OP_AND, OP_OR,
    /* unary */
    OP_NEG, OP_NOT,
    /* assignment operators */
    OP_ASSIGN,
    OP_PLUS_ASSIGN, OP_MINUS_ASSIGN,
    OP_STAR_ASSIGN, OP_SLASH_ASSIGN, OP_PCT_ASSIGN,
} Operator;

/* ─── Forward declarations ────────────────────────────────────────────────── */
typedef struct Expr      Expr;
typedef struct Stmt      Stmt;
typedef struct FuncDecl  FuncDecl;
typedef struct ClassDecl ClassDecl;
typedef struct Program   Program;

/* ─── Expression kinds ────────────────────────────────────────────────────── */
typedef enum {
    EXPR_INT_LIT,
    EXPR_FLOAT_LIT,
    EXPR_CHAR_LIT,
    EXPR_STRING_LIT,
    EXPR_BOOL_LIT,
    EXPR_NULL,            /* FEATURE 5 */
    EXPR_IDENT,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_ADDR_OF,         /* &var  — used by take() */
    EXPR_INDEX,           /* arr[i] */
    EXPR_MEMBER_ACCESS,   /* obj.field */
    EXPR_METHOD_CALL,     /* obj.method(args) */
    EXPR_NEW,             /* new Foo(args) */
    EXPR_LEN,             /* len(x)  — FEATURE 1 */
    EXPR_STR_CONCAT,      /* a + b for strings — FEATURE 6 */
    EXPR_CALL,            /* func(args) */
} ExprKind;

struct Expr {
    ExprKind     kind;
    int          line, col;

    /* ── literal value fields (flat — not in a union so they're always accessible) */
    int64_t      ival;   /* EXPR_INT_LIT */
    double       fval;   /* EXPR_FLOAT_LIT */
    char         cval;   /* EXPR_CHAR_LIT */
    int          bval;   /* EXPR_BOOL_LIT */
    const char  *sval;   /* EXPR_STRING_LIT */

    /* EXPR_IDENT */
    char *name;

    /* EXPR_BINARY */
    struct { Operator op; Expr *left; Expr *right; } binary;

    /* EXPR_UNARY */
    struct { Operator op; Expr *operand; } unary;

    /* EXPR_ADDR_OF */
    struct { Expr *operand; } addr;

    /* EXPR_INDEX */
    struct { Expr *array; Expr *index; } idx;

    /* EXPR_MEMBER_ACCESS */
    struct { Expr *left; char *name; } member;

    /* EXPR_METHOD_CALL */
    struct {
        Expr  *obj;
        char  *method;
        Expr **args;
        int    argc;
    } method_call;

    /* EXPR_NEW */
    struct {
        char  *class_name;
        Expr **args;
        int    argc;
    } new_obj;

    /* EXPR_LEN */
    struct { Expr *arg; } len_expr;

    /* EXPR_STR_CONCAT */
    struct { Expr *left; Expr *right; } concat;

    /* EXPR_CALL */
    struct {
        char  *callee;
        Expr **args;
        int    argc;
    } call;
};

/* ─── Statement kinds ─────────────────────────────────────────────────────── */
typedef enum {
    STMT_VAR_DECL,
    STMT_ASSIGN,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR_RANGE,     /* for i in range(a,b) */
    STMT_FOR_ARRAY,     /* for item in arr     — FEATURE 8 */
    STMT_SWITCH,
    STMT_RETURN,
    STMT_MULTI_RETURN,  /* return a, b         — FEATURE 7 */
    STMT_BREAK,
    STMT_PRINT,
    STMT_TAKE,
    STMT_EXPR,
    STMT_BLOCK,
    STMT_IMPORT,        /* import "path"       — FEATURE 2 */
    STMT_MULTI_ASSIGN,  /* int a, int b = fn() — FEATURE 7 */
    STMT_CLASS_DECL,    /* class Foo { }       — stored on Program too */
} StmtKind;
/* ─── Variable Declarator ─────────────────────────────────────────────────── */
typedef struct {
    char  *name;
    Expr  *init;
    Expr **array_init;
    int    array_init_count;
    int    array_size;
} VarDeclItem;
/* ─── Switch case clause ──────────────────────────────────────────────────── */
typedef struct {
    Expr  *value;       /* NULL  → default */
    Stmt **body;
    int    body_count;
} CaseClause;

struct Stmt {
    StmtKind kind;
    int      line, col;

    union {

        /* STMT_VAR_DECL */
        struct {
            MarType     *type;
            VarDeclItem *decls;
            int          decl_count;
        } var_decl;

        /* STMT_ASSIGN */
        struct {
            Operator  op;    /* OP_ASSIGN for plain, OP_*_ASSIGN for compound */
            Expr     *target;
            Expr     *value;
        } assign;

        /* STMT_IF */
        struct {
            Expr *cond;
            Stmt *then_branch;
            Stmt *else_branch;   /* NULL if no else */
        } if_stmt;

        /* STMT_WHILE */
        struct {
            Expr *cond;
            Stmt *body;
        } while_stmt;

        /* STMT_FOR_RANGE */
        struct {
            char *var;
            Expr *start;
            Expr *end;
            Stmt *body;
        } for_range;

        /* STMT_FOR_ARRAY  — FEATURE 8 */
        struct {
            char *var;   /* loop variable name */
            char *arr;   /* array variable name */
            Stmt *body;
        } for_array;

        /* STMT_SWITCH */
        struct {
            Expr       *expr;
            CaseClause *cases;
            int         case_count;
        } switch_stmt;

        /* STMT_RETURN */
        struct {
            Expr *value;   /* NULL for bare return */
        } ret;

        /* STMT_MULTI_RETURN  — FEATURE 7 */
        struct {
            Expr **values;
            int    value_count;
        } multi_ret;

        /* STMT_PRINT and STMT_TAKE share the same layout */
        struct {
            char  *fmt;
            Expr **args;
            int    argc;
        } print;   /* also used for take */

        /* STMT_BLOCK */
        struct {
            Stmt **stmts;
            int    count;
        } block;

        /* STMT_EXPR */
        struct {
            Expr *expr;
        } expr_stmt;

        /* STMT_IMPORT  — FEATURE 2 */
        struct {
            char *path;
        } import_stmt;

        /* STMT_MULTI_ASSIGN  — FEATURE 7 */
        struct {
            char    **names;
            MarType **types;
            int       count;
            Expr     *rhs;    /* must be EXPR_CALL */
        } multi_assign;

        /* STMT_CLASS_DECL */
        ClassDecl *class_decl;
    };
};

/* ─── Class field ─────────────────────────────────────────────────────────── */
typedef struct {
    char    *name;
    MarType *type;
} Field;

/* ─── Class method slot ───────────────────────────────────────────────────── */
typedef struct {
    char     *name;     /* method name (redundant with method->name, kept for fast lookup) */
    FuncDecl *method;
} Method;

/* ─── Function declaration ────────────────────────────────────────────────── */
struct FuncDecl {
    char      *name;
    MarType   *return_type;         /* primary / only return type */
    MarType  **return_types;        /* all return types for multi-return */
    int        return_type_count;   /* 1 = normal, >1 = multi-return */
    char     **param_names;
    MarType  **param_types;
    int        param_count;
    Stmt      *body;               /* STMT_BLOCK */
    SrcLoc     loc;
};

/* ─── Class declaration ───────────────────────────────────────────────────── */
struct ClassDecl {
    char    *name;
    char    *parent_name;   /* NULL if no inheritance  — FEATURE 4 */
    Field   *fields;
    int      field_count;
    Method  *methods;
    int      method_count;
    SrcLoc   loc;
};

/* ─── Top-level program ───────────────────────────────────────────────────── */
struct Program {
    FuncDecl  **funcs;
    int         func_count;
    ClassDecl **classes;
    int         class_count;
    /* FEATURE 2/3: extra C #include lines emitted at top of generated file */
    char      **c_includes;
    int         c_include_count;
};

/* ─── Constructors (implemented in ast.c) ─────────────────────────────────── */
MarType *type_new(TypeKind kind);
MarType *type_array(MarType *elem, int size);
Expr    *expr_new(ExprKind kind, SrcLoc loc);
Stmt    *stmt_new(StmtKind kind, SrcLoc loc);

/* ─── Debug helpers ───────────────────────────────────────────────────────── */
const char *typekind_str(TypeKind k);
const char *op_str_dbg(Operator op);
void        ast_print(Program *prog);