#ifndef MAR_AST_H
#define MAR_AST_H

#include "mar/error.h"
#include "mar/arena.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TY_INT, TY_FLOAT, TY_CHAR, TY_BOOL, TY_VOID, TY_ARRAY, TY_UNKNOWN,
} TypeKind;

typedef struct MarType {
    TypeKind        kind;
    struct MarType *elem;
    int             size;
} MarType;

typedef enum {
    EXPR_INT_LIT, EXPR_FLOAT_LIT, EXPR_CHAR_LIT,
    EXPR_STRING_LIT, EXPR_BOOL_LIT,
    EXPR_IDENT, EXPR_INDEX,
    EXPR_BINARY, EXPR_UNARY,
    EXPR_ASSIGN, EXPR_COMPOUND_ASSIGN,
    EXPR_CALL, EXPR_ADDR_OF,
} ExprKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LTE, OP_GTE,
    OP_AND, OP_OR,
    OP_NOT, OP_NEG, OP_POS,
    OP_ASSIGN,
    OP_PLUS_ASSIGN, OP_MINUS_ASSIGN,
    OP_STAR_ASSIGN, OP_SLASH_ASSIGN, OP_PCT_ASSIGN,
} Operator;

typedef struct Expr {
    ExprKind  kind;
    MarType  *type;
    SrcLoc    loc;
    union {
        int64_t  ival;
        double   fval;
        char     cval;
        bool     bval;
        char    *sval;
        char    *name;
        struct { struct Expr *array; struct Expr *index; } idx;
        struct { Operator op; struct Expr *left; struct Expr *right; } binary;
        struct { Operator op; struct Expr *operand; } unary;
        struct { Operator op; struct Expr *target; struct Expr *value; } assign;
        struct { char *callee; struct Expr **args; int argc; } call;
        struct { struct Expr *operand; } addr;
    };
} Expr;

typedef struct Stmt Stmt;

typedef struct {
    Expr  *value;
    Stmt **body;
    int    body_count;
} CaseClause;

typedef enum {
    STMT_VAR_DECL, STMT_ASSIGN, STMT_IF,
    STMT_WHILE, STMT_FOR_RANGE, STMT_SWITCH,
    STMT_RETURN, STMT_BREAK,
    STMT_PRINT, STMT_TAKE,
    STMT_EXPR, STMT_BLOCK,
} StmtKind;

struct Stmt {
    StmtKind kind;
    SrcLoc   loc;
    union {
        struct { MarType *type; char *name; Expr *init;
                 Expr **array_init; int array_init_count; } var_decl;
        struct { Expr *target; Expr *value; Operator op; } assign;
        struct { Expr *cond; Stmt *then_branch; Stmt *else_branch; } if_stmt;
        struct { Expr *cond; Stmt *body; } while_stmt;
        struct { char *var; Expr *start; Expr *end; Stmt *body; } for_range;
        struct { Expr *expr; CaseClause *cases; int case_count; } switch_stmt;
        struct { Expr *value; } ret;
        struct { char *fmt; Expr **args; int argc; } print;
        struct { char *fmt; Expr **args; int argc; } take;
        struct { Expr *expr; } expr_stmt;
        struct { Stmt **stmts; int count; } block;
    };
};

typedef struct {
    char     *name;
    MarType  *return_type;
    char    **param_names;
    MarType **param_types;
    int       param_count;
    Stmt     *body;
    SrcLoc    loc;
} FuncDecl;

typedef struct {
    FuncDecl  **funcs;
    int         func_count;
    const char *filename;
} Program;

MarType *type_new(TypeKind kind);
MarType *type_array(MarType *elem, int size);
Expr    *expr_new(ExprKind kind, SrcLoc loc);
Stmt    *stmt_new(StmtKind kind, SrcLoc loc);
void     ast_print(Program *prog);

#endif
