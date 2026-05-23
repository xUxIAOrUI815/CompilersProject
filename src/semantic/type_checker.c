#include "compiler_project/semantic/type_checker.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    CompilerError error;
    int has_error;
} TypeCheckContext;

static void cp_set_type_error(TypeCheckContext *ctx, int code, const ASTNode *node, const char *message) {
    if (ctx == NULL || ctx->has_error) {
        return;
    }
    cp_make_error(&ctx->error, code, message, node == NULL ? -1 : node->line, node == NULL ? -1 : node->column, "semantic");
    ctx->has_error = 1;
}

static void cp_type_from_literal(const ASTNode *expr, TypeExpr *type) {
    if (expr != NULL && strchr(expr->lexeme, '.') != NULL) {
        cp_type_init_primitive(type, TYPE_FLOAT_T);
    } else {
        cp_type_init_primitive(type, TYPE_INT_T);
    }
}

static void cp_type_from_type_name_node(const ASTNode *node, TypeExpr *type) {
    if (node == NULL) {
        cp_type_init_error(type);
        return;
    }
    if (strcmp(node->lexeme, "int") == 0) {
        cp_type_init_primitive(type, TYPE_INT_T);
    } else if (strcmp(node->lexeme, "float") == 0) {
        cp_type_init_primitive(type, TYPE_FLOAT_T);
    } else if (strcmp(node->lexeme, "double") == 0) {
        cp_type_init_primitive(type, TYPE_DOUBLE_T);
    } else if (strcmp(node->lexeme, "void") == 0) {
        cp_type_init_primitive(type, TYPE_VOID_T);
    } else if (strcmp(node->lexeme, "bool") == 0) {
        cp_type_init_primitive(type, TYPE_BOOL_T);
    } else {
        cp_type_init_error(type);
    }
}

static TypeExpr *cp_shared_type_from_name(const char *name) {
    static TypeExpr int_t;
    static TypeExpr float_t;
    static TypeExpr double_t;
    static TypeExpr void_t;
    static TypeExpr bool_t;
    static int initialized;
    if (!initialized) {
        cp_type_init_primitive(&int_t, TYPE_INT_T);
        cp_type_init_primitive(&float_t, TYPE_FLOAT_T);
        cp_type_init_primitive(&double_t, TYPE_DOUBLE_T);
        cp_type_init_primitive(&void_t, TYPE_VOID_T);
        cp_type_init_primitive(&bool_t, TYPE_BOOL_T);
        initialized = 1;
    }
    if (name == NULL) return NULL;
    if (strcmp(name, "int") == 0) return &int_t;
    if (strcmp(name, "float") == 0) return &float_t;
    if (strcmp(name, "double") == 0) return &double_t;
    if (strcmp(name, "void") == 0) return &void_t;
    if (strcmp(name, "bool") == 0) return &bool_t;
    return NULL;
}

static TypeExpr cp_expr_type_internal(const ASTNode *expr, SymbolTable *table, TypeCheckContext *ctx);
static int cp_check_node(const ASTNode *node, SymbolTable *table, TypeCheckContext *ctx, const TypeExpr *function_return_type);

static int is_compare_expr(const ASTNode *expr) {
    if (expr == NULL || expr->node_type != AST_EXPRESSION) {
        return 0;
    }
    return strcmp(expr->symbol_name, "eq") == 0
        || strcmp(expr->symbol_name, "ne") == 0
        || strcmp(expr->symbol_name, "lt") == 0
        || strcmp(expr->symbol_name, "le") == 0
        || strcmp(expr->symbol_name, "gt") == 0
        || strcmp(expr->symbol_name, "ge") == 0;
}

static int is_boolean_expr(const ASTNode *expr) {
    if (expr == NULL || expr->node_type != AST_EXPRESSION) {
        return 0;
    }
    return strcmp(expr->symbol_name, "and") == 0
        || strcmp(expr->symbol_name, "or") == 0
        || strcmp(expr->symbol_name, "not") == 0;
}

TypeExprResult cp_synthesize_expr_type(const ASTNode *expr, SymbolTable *table) {
    TypeExprResult result;
    TypeCheckContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    RESULT_SUCCESS(&result.base);
    result.data = cp_expr_type_internal(expr, table, &ctx);
    if (ctx.has_error) {
        RESULT_FAILURE(&result.base, ctx.error);
    }
    return result;
}

BoolResult cp_check_assignment(const ASTNode *lhs, const ASTNode *rhs, SymbolTable *table) {
    BoolResult result;
    TypeCheckContext ctx;
    TypeExpr lhs_type;
    TypeExpr rhs_type;
    memset(&ctx, 0, sizeof(ctx));
    RESULT_SUCCESS(&result.base);
    lhs_type = cp_expr_type_internal(lhs, table, &ctx);
    rhs_type = cp_expr_type_internal(rhs, table, &ctx);
    if (ctx.has_error) {
        RESULT_FAILURE(&result.base, ctx.error);
        result.data = 0;
        return result;
    }
    if (!cp_type_can_assign(&lhs_type, &rhs_type)) {
        cp_set_type_error(&ctx, 8001, lhs, "assignment type mismatch");
        RESULT_FAILURE(&result.base, ctx.error);
        result.data = 0;
        return result;
    }
    result.data = 1;
    return result;
}

BoolResult cp_check_function_call(const ASTNode *call, SymbolTable *table) {
    BoolResult result;
    const SymbolEntry *entry;
    int index;
    RESULT_SUCCESS(&result.base);
    result.data = 1;
    if (call == NULL || call->child_count <= 0 || call->children[0] == NULL) {
        return result;
    }
    entry = cp_symbol_table_lookup(table, call->children[0]->lexeme);
    if (entry == NULL || entry->category != SYMBOL_FUNCTION) {
        CompilerError error;
        cp_make_error(&error, 8004, "called identifier is not a function", call->line, call->column, "semantic");
        RESULT_FAILURE(&result.base, error);
        result.data = 0;
        return result;
    }
    if (entry->type_expr.kind == TYPE_FUNCTION_T && entry->type_expr.param_count != call->child_count - 1) {
        CompilerError error;
        cp_make_error(&error, 8005, "function argument count mismatch", call->line, call->column, "semantic");
        RESULT_FAILURE(&result.base, error);
        result.data = 0;
        return result;
    }
    if (entry->type_expr.kind == TYPE_FUNCTION_T) {
        for (index = 0; index < entry->type_expr.param_count; ++index) {
            TypeCheckContext ctx;
            TypeExpr arg_type;
            TypeExpr *param_type = entry->type_expr.param_types[index];
            memset(&ctx, 0, sizeof(ctx));
            arg_type = cp_expr_type_internal(call->children[index + 1], table, &ctx);
            if (ctx.has_error) {
                RESULT_FAILURE(&result.base, ctx.error);
                result.data = 0;
                return result;
            }
            if (param_type != NULL && !cp_type_can_assign(param_type, &arg_type)) {
                CompilerError error;
                cp_make_error(&error, 8009, "function argument type mismatch", call->line, call->column, "semantic");
                RESULT_FAILURE(&result.base, error);
                result.data = 0;
                return result;
            }
        }
    }
    return result;
}

BoolResult cp_check_statement(const ASTNode *stmt, SymbolTable *table) {
    BoolResult result;
    TypeCheckContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    RESULT_SUCCESS(&result.base);
    result.data = cp_check_node(stmt, table, &ctx, NULL);
    if (ctx.has_error) {
        RESULT_FAILURE(&result.base, ctx.error);
        result.data = 0;
    }
    return result;
}

static TypeExpr cp_expr_type_internal(const ASTNode *expr, SymbolTable *table, TypeCheckContext *ctx) {
    TypeExpr lhs_type;
    TypeExpr rhs_type;
    TypeExpr result_type;
    if (expr == NULL) {
        cp_type_init_error(&result_type);
        return result_type;
    }
    switch (expr->node_type) {
        case AST_IDENTIFIER: {
            const SymbolEntry *entry = cp_symbol_table_lookup(table, expr->lexeme);
            if (entry == NULL) {
                cp_set_type_error(ctx, 7002, expr, "use of undeclared identifier");
                cp_type_init_error(&result_type);
                return result_type;
            }
            return entry->type_expr;
        }
        case AST_LITERAL:
            cp_type_from_literal(expr, &result_type);
            return result_type;
        case AST_EXPRESSION:
            if (strcmp(expr->symbol_name, "assign") == 0 && expr->child_count >= 2) {
                return cp_expr_type_internal(expr->children[0], table, ctx);
            }
            if ((strcmp(expr->symbol_name, "add") == 0
                    || strcmp(expr->symbol_name, "sub") == 0
                    || strcmp(expr->symbol_name, "mul") == 0
                    || strcmp(expr->symbol_name, "div") == 0)
                && expr->child_count >= 2) {
                lhs_type = cp_expr_type_internal(expr->children[0], table, ctx);
                rhs_type = cp_expr_type_internal(expr->children[1], table, ctx);
                if (!cp_type_is_numeric(&lhs_type) || !cp_type_is_numeric(&rhs_type)) {
                    cp_set_type_error(ctx, 8002, expr, "arithmetic operands must be numeric");
                    cp_type_init_error(&result_type);
                    return result_type;
                }
                if (lhs_type.kind == TYPE_FLOAT_T || lhs_type.kind == TYPE_DOUBLE_T
                    || rhs_type.kind == TYPE_FLOAT_T || rhs_type.kind == TYPE_DOUBLE_T) {
                    cp_type_init_primitive(&result_type, TYPE_FLOAT_T);
                } else {
                    cp_type_init_primitive(&result_type, TYPE_INT_T);
                }
                return result_type;
            }
            if (is_compare_expr(expr) && expr->child_count >= 2) {
                lhs_type = cp_expr_type_internal(expr->children[0], table, ctx);
                rhs_type = cp_expr_type_internal(expr->children[1], table, ctx);
                if (!cp_type_is_numeric(&lhs_type) || !cp_type_is_numeric(&rhs_type)) {
                    cp_set_type_error(ctx, 8003, expr, "comparison operands must be numeric");
                    cp_type_init_error(&result_type);
                    return result_type;
                }
                cp_type_init_primitive(&result_type, TYPE_BOOL_T);
                return result_type;
            }
            if (is_boolean_expr(expr)) {
                lhs_type = cp_expr_type_internal(expr->children[0], table, ctx);
                if (strcmp(expr->symbol_name, "not") == 0) {
                    if (!cp_type_is_numeric(&lhs_type)) {
                        cp_set_type_error(ctx, 8010, expr, "logical operand must be numeric or boolean");
                        cp_type_init_error(&result_type);
                        return result_type;
                    }
                    cp_type_init_primitive(&result_type, TYPE_BOOL_T);
                    return result_type;
                }
                rhs_type = cp_expr_type_internal(expr->children[1], table, ctx);
                if (!cp_type_is_numeric(&lhs_type) || !cp_type_is_numeric(&rhs_type)) {
                    cp_set_type_error(ctx, 8010, expr, "logical operands must be numeric or boolean");
                    cp_type_init_error(&result_type);
                    return result_type;
                }
                cp_type_init_primitive(&result_type, TYPE_BOOL_T);
                return result_type;
            }
            if (strcmp(expr->symbol_name, "call") == 0) {
                BoolResult call_ok = cp_check_function_call(expr, table);
                if (!call_ok.base.ok) {
                    cp_set_type_error(ctx, call_ok.base.errors[0].code, expr, call_ok.base.errors[0].message);
                    cp_type_init_error(&result_type);
                    return result_type;
                }
                if (expr->children[0] != NULL) {
                    const SymbolEntry *entry = cp_symbol_table_lookup(table, expr->children[0]->lexeme);
                    if (entry != NULL && entry->type_expr.return_type != NULL) {
                        return *entry->type_expr.return_type;
                    }
                }
            }
            break;
        default:
            break;
    }
    cp_type_init_error(&result_type);
    return result_type;
}

static int cp_check_declaration(const ASTNode *node, SymbolTable *table, TypeCheckContext *ctx) {
    SymbolEntry entry;
    if (node == NULL || node->child_count < 2 || node->children[0] == NULL || node->children[1] == NULL) {
        return 1;
    }
    if (cp_symbol_table_lookup_current(table, node->children[1]->lexeme) != NULL) {
        cp_set_type_error(ctx, 7001, node, "symbol redefinition in current scope");
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", node->children[1]->lexeme);
    entry.category = SYMBOL_VARIABLE;
    cp_type_from_type_name_node(node->children[0], &entry.type_expr);
    entry.initialized = node->child_count >= 3;
    entry.decl_line = node->line;
    entry.decl_column = node->column;
    cp_symbol_table_insert(table, &entry);
    if (node->child_count >= 3) {
        BoolResult assign_ok = cp_check_assignment(node->children[1], node->children[2], table);
        if (!assign_ok.base.ok) {
            ctx->error = assign_ok.base.errors[0];
            ctx->has_error = 1;
            return 0;
        }
    }
    return 1;
}

static int cp_make_function_entry(const ASTNode *node, SymbolEntry *fn_entry) {
    int index;
    TypeExpr *param_ptrs[CP_MAX_TYPE_PARAMS];
    if (node == NULL || fn_entry == NULL || node->child_count < 2 || node->children[1] == NULL) {
        return 0;
    }
    memset(fn_entry, 0, sizeof(*fn_entry));
    snprintf(fn_entry->name, sizeof(fn_entry->name), "%s", node->children[1]->lexeme);
    fn_entry->category = SYMBOL_FUNCTION;
    for (index = 0; index < CP_MAX_TYPE_PARAMS; ++index) {
        param_ptrs[index] = NULL;
    }
    if (node->child_count > 2 && node->children[2] != NULL && node->children[2]->node_type == AST_PARAM_LIST) {
        for (index = 0; index < node->children[2]->child_count && index < CP_MAX_TYPE_PARAMS; ++index) {
            param_ptrs[index] = cp_shared_type_from_name(node->children[2]->children[index]->children[0]->lexeme);
        }
    }
    cp_type_init_function(
        &fn_entry->type_expr,
        node->child_count >= 1 ? cp_shared_type_from_name(node->children[0]->lexeme) : cp_shared_type_from_name("void"),
        param_ptrs,
        node->child_count > 2 && node->children[2] != NULL ? node->children[2]->child_count : 0
    );
    fn_entry->decl_line = node->line;
    fn_entry->decl_column = node->column;
    return 1;
}

static int cp_predeclare_functions(const ASTNode *ast, SymbolTable *table, TypeCheckContext *ctx) {
    int index;
    if (ast == NULL || table == NULL || ctx == NULL || ast->node_type != AST_PROGRAM) {
        return 1;
    }
    for (index = 0; index < ast->child_count; ++index) {
        const ASTNode *child = ast->children[index];
        SymbolEntry fn_entry;
        if (child == NULL || child->node_type != AST_FUNCTION_DEF) {
            continue;
        }
        if (!cp_make_function_entry(child, &fn_entry)) {
            continue;
        }
        if (!cp_symbol_table_insert(table, &fn_entry)) {
            cp_set_type_error(ctx, 7001, child, "symbol redefinition in current scope");
            return 0;
        }
    }
    return 1;
}

static int cp_check_node(const ASTNode *node, SymbolTable *table, TypeCheckContext *ctx, const TypeExpr *function_return_type) {
    int index;
    TypeExpr return_type;
    if (node == NULL || table == NULL || ctx == NULL) {
        return 1;
    }
    if (ctx->has_error) {
        return 0;
    }
    switch (node->node_type) {
        case AST_PROGRAM:
            for (index = 0; index < node->child_count; ++index) {
                if (!cp_check_node(node->children[index], table, ctx, function_return_type)) {
                    return 0;
                }
            }
            return 1;
        case AST_DECLARATION:
            return cp_check_declaration(node, table, ctx);
        case AST_BLOCK:
            cp_symbol_table_enter_scope(table, SCOPE_BLOCK, node->symbol_name[0] == '\0' ? "block" : node->symbol_name);
            for (index = 0; index < node->child_count; ++index) {
                if (!cp_check_node(node->children[index], table, ctx, function_return_type)) {
                    cp_symbol_table_exit_scope(table);
                    return 0;
                }
            }
            cp_symbol_table_exit_scope(table);
            return 1;
        case AST_FUNCTION_DEF:
            if (node->child_count >= 2 && node->children[1] != NULL
                && cp_symbol_table_lookup_current(table, node->children[1]->lexeme) == NULL) {
                SymbolEntry fn_entry;
                if (cp_make_function_entry(node, &fn_entry)) {
                    cp_symbol_table_insert(table, &fn_entry);
                }
            }
            memset(&return_type, 0, sizeof(return_type));
            if (node->child_count >= 1) {
                cp_type_from_type_name_node(node->children[0], &return_type);
            } else {
                cp_type_init_primitive(&return_type, TYPE_VOID_T);
            }
            cp_symbol_table_enter_scope(table, SCOPE_FUNCTION, node->child_count > 1 ? node->children[1]->lexeme : "function");
            if (node->child_count > 2 && node->children[2] != NULL && node->children[2]->node_type == AST_PARAM_LIST) {
                for (index = 0; index < node->children[2]->child_count; ++index) {
                    ASTNode *param = node->children[2]->children[index];
                    if (!cp_check_declaration(param, table, ctx)) {
                        cp_symbol_table_exit_scope(table);
                        return 0;
                    }
                }
            }
            for (index = 3; index < node->child_count; ++index) {
                if (!cp_check_node(node->children[index], table, ctx, &return_type)) {
                    cp_symbol_table_exit_scope(table);
                    return 0;
                }
            }
            cp_symbol_table_exit_scope(table);
            return 1;
        case AST_STATEMENT:
            if (strcmp(node->symbol_name, "return") == 0 && node->child_count >= 1) {
                TypeExpr expr_type = cp_expr_type_internal(node->children[0], table, ctx);
                if (!ctx->has_error && function_return_type != NULL && !cp_type_can_assign(function_return_type, &expr_type)) {
                    cp_set_type_error(ctx, 8006, node, "return type mismatch");
                    return 0;
                }
                return !ctx->has_error;
            }
            if (strcmp(node->symbol_name, "expr") == 0 && node->child_count >= 1) {
                if (node->children[0] != NULL && node->children[0]->node_type == AST_EXPRESSION
                    && strcmp(node->children[0]->symbol_name, "assign") == 0 && node->children[0]->child_count >= 2) {
                    BoolResult assign_ok = cp_check_assignment(node->children[0]->children[0], node->children[0]->children[1], table);
                    if (!assign_ok.base.ok) {
                        ctx->error = assign_ok.base.errors[0];
                        ctx->has_error = 1;
                        return 0;
                    }
                } else {
                    cp_expr_type_internal(node->children[0], table, ctx);
                }
                return !ctx->has_error;
            }
            if (strcmp(node->symbol_name, "if") == 0 && node->child_count >= 2) {
                TypeExpr cond_type = cp_expr_type_internal(node->children[0], table, ctx);
                if (!ctx->has_error && !cp_type_is_numeric(&cond_type)) {
                    cp_set_type_error(ctx, 8007, node, "if condition must be numeric or boolean");
                    return 0;
                }
                if (!cp_check_node(node->children[1], table, ctx, function_return_type)) {
                    return 0;
                }
                if (node->child_count >= 3 && !cp_check_node(node->children[2], table, ctx, function_return_type)) {
                    return 0;
                }
                return !ctx->has_error;
            }
            if (strcmp(node->symbol_name, "while") == 0 && node->child_count >= 2) {
                TypeExpr cond_type = cp_expr_type_internal(node->children[0], table, ctx);
                if (!ctx->has_error && !cp_type_is_numeric(&cond_type)) {
                    cp_set_type_error(ctx, 8008, node, "while condition must be numeric or boolean");
                    return 0;
                }
                return cp_check_node(node->children[1], table, ctx, function_return_type);
            }
            break;
        default:
            break;
    }
    for (index = 0; index < node->child_count; ++index) {
        if (!cp_check_node(node->children[index], table, ctx, function_return_type)) {
            return 0;
        }
    }
    return 1;
}

BoolResult cp_check_types(const ASTNode *ast) {
    BoolResult result;
    SymbolTable table;
    TypeCheckContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    cp_symbol_table_init(&table);
    RESULT_SUCCESS(&result.base);
    if (!cp_predeclare_functions(ast, &table, &ctx)) {
        RESULT_FAILURE(&result.base, ctx.error);
        result.data = 0;
        return result;
    }
    result.data = cp_check_node(ast, &table, &ctx, NULL);
    if (ctx.has_error) {
        RESULT_FAILURE(&result.base, ctx.error);
        result.data = 0;
    }
    return result;
}
