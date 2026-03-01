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
    char           *name; // Correctly added for class names
} MarType;

typedef enum {
    EXPR_INT_LIT, EXPR_FLOAT_LIT, EXPR_CHAR_LIT,
    EXPR_STRING_LIT, EXPR_BOOL_LIT,
    EXPR_IDENT, EXPR_INDEX,
    EXPR_BINARY, EXPR_UNARY,
    EXPR_ASSIGN, EXPR_COMPOUND_ASSIGN,
    EXPR_CALL, EXPR_ADDR_OF,
    EXPR_MEMBER_ACCESS, // For the '.' operator
    EXPR_NEW,           // For 'new ClassName()'
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

// Structure for Class Fields
typedef struct {
    char *name;
    MarType *type;
} Field;

// Structure for Class Methods
typedef struct {
    char *name;
    struct FuncDecl *method;
} Method;

typedef struct Expr Expr;

struct Expr {
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
        // Logic for object.member
        struct { struct Expr *left; char *name; } member; 
        // Logic for new Class()
        struct { char *class_name; struct Expr **args; int argc; } new_obj;
    };
};

typedef struct Stmt Stmt;

typedef struct {
    Expr  *value;
    Stmt **body;
    int    body_count;
} CaseClause;

// Structure for the Class Definition itself
typedef struct {
    char *name;
    Field *fields;
    int field_count;
    Method *methods;
    int method_count;
    SrcLoc loc;
} ClassDecl;

typedef enum {
    STMT_VAR_DECL, STMT_ASSIGN, STMT_IF,
    STMT_WHILE, STMT_FOR_RANGE, STMT_SWITCH,
    STMT_RETURN, STMT_BREAK,
    STMT_PRINT, STMT_TAKE,
    STMT_EXPR, STMT_BLOCK,
    STMT_CLASS_DECL, // Added to recognize class blocks
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
        ClassDecl *class_decl; // For STMT_CLASS_DECL
    };
};

typedef struct FuncDecl {
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
    ClassDecl **classes; // Array to store all classes in the file
    int         class_count;
    const char *filename;
} Program;

MarType *type_new(TypeKind kind);
MarType *type_array(MarType *elem, int size);
Expr    *expr_new(ExprKind kind, SrcLoc loc);
Stmt    *stmt_new(StmtKind kind, SrcLoc loc);
void     ast_print(Program *prog);

#endif