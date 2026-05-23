#include "compiler_project/semantic/ir_builder.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    QuadrupleList *list;
    int next_temp_id;
    int next_label_id;
} IRContext;

void cp_ir_list_init(QuadrupleList *list) {
    if (list == NULL) {
        return;
    }
    memset(list, 0, sizeof(*list));
}

int cp_ir_emit(QuadrupleList *list, IROp op, const char *arg1, const char *arg2, const char *result) {
    Quadruple *quad;
    if (list == NULL || list->count >= CP_MAX_QUADRUPLES) {
        return -1;
    }
    quad = &list->items[list->count];
    quad->index = 100 + list->count;
    quad->op = op;
    snprintf(quad->arg1, sizeof(quad->arg1), "%s", arg1 == NULL ? "_" : arg1);
    snprintf(quad->arg2, sizeof(quad->arg2), "%s", arg2 == NULL ? "_" : arg2);
    snprintf(quad->result, sizeof(quad->result), "%s", result == NULL ? "_" : result);
    ++list->count;
    return quad->index;
}

static void cp_new_temp(IRContext *ctx, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "T%d", ++ctx->next_temp_id);
}

static void cp_new_label(IRContext *ctx, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "L%d", ++ctx->next_label_id);
}

static void cp_gen_expr(const ASTNode *node, IRContext *ctx, char *out_place, size_t out_place_size);
static void cp_gen_condition(const ASTNode *node, IRContext *ctx, const char *true_label, const char *false_label);

static int is_compare_expr(const ASTNode *node) {
    if (node == NULL || node->node_type != AST_EXPRESSION) {
        return 0;
    }
    return strcmp(node->symbol_name, "eq") == 0
        || strcmp(node->symbol_name, "ne") == 0
        || strcmp(node->symbol_name, "lt") == 0
        || strcmp(node->symbol_name, "le") == 0
        || strcmp(node->symbol_name, "gt") == 0
        || strcmp(node->symbol_name, "ge") == 0;
}

static int is_logical_expr(const ASTNode *node) {
    if (node == NULL || node->node_type != AST_EXPRESSION) {
        return 0;
    }
    return strcmp(node->symbol_name, "and") == 0
        || strcmp(node->symbol_name, "or") == 0
        || strcmp(node->symbol_name, "not") == 0;
}

static IROp compare_op(const char *name) {
    if (strcmp(name, "lt") == 0) return IR_IF_LT;
    if (strcmp(name, "le") == 0) return IR_IF_LE;
    if (strcmp(name, "gt") == 0) return IR_IF_GT;
    if (strcmp(name, "ge") == 0) return IR_IF_GE;
    if (strcmp(name, "eq") == 0) return IR_IF_EQ;
    return IR_IF_NE;
}

static void cp_gen_stmt(const ASTNode *node, IRContext *ctx) {
    int index;
    char value[64];
    if (node == NULL || ctx == NULL) {
        return;
    }
    if (node->node_type == AST_FUNCTION_DEF) {
        const char *function_name = node->child_count > 1 && node->children[1] != NULL ? node->children[1]->lexeme : "function";
        cp_ir_emit(ctx->list, IR_FUNC_BEGIN, function_name, "_", "_");
        for (index = 3; index < node->child_count; ++index) {
            cp_gen_stmt(node->children[index], ctx);
        }
        cp_ir_emit(ctx->list, IR_FUNC_END, function_name, "_", "_");
        return;
    }
    if (node->node_type == AST_DECLARATION) {
        if (node->child_count >= 3 && node->children[1] != NULL) {
            cp_gen_expr(node->children[2], ctx, value, sizeof(value));
            cp_ir_emit(ctx->list, IR_ASSIGN, value, "_", node->children[1]->lexeme);
        }
        return;
    }
    if (node->node_type == AST_STATEMENT && strcmp(node->symbol_name, "return") == 0 && node->child_count >= 1) {
        cp_gen_expr(node->children[0], ctx, value, sizeof(value));
        cp_ir_emit(ctx->list, IR_RET, value, "_", "_");
        return;
    }
    if (node->node_type == AST_STATEMENT && strcmp(node->symbol_name, "expr") == 0 && node->child_count >= 1) {
        cp_gen_expr(node->children[0], ctx, value, sizeof(value));
        return;
    }
    if (node->node_type == AST_STATEMENT && strcmp(node->symbol_name, "if") == 0 && node->child_count >= 2) {
        char then_label[64];
        char else_label[64];
        char end_label[64];
        cp_new_label(ctx, then_label, sizeof(then_label));
        cp_new_label(ctx, else_label, sizeof(else_label));
        cp_new_label(ctx, end_label, sizeof(end_label));
        cp_gen_condition(node->children[0], ctx, then_label, node->child_count >= 3 ? else_label : end_label);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", then_label);
        cp_gen_stmt(node->children[1], ctx);
        if (node->child_count >= 3) {
            cp_ir_emit(ctx->list, IR_GOTO, "_", "_", end_label);
            cp_ir_emit(ctx->list, IR_LABEL, "_", "_", else_label);
            cp_gen_stmt(node->children[2], ctx);
        }
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", end_label);
        return;
    }
    if (node->node_type == AST_STATEMENT && strcmp(node->symbol_name, "while") == 0 && node->child_count >= 2) {
        char begin_label[64];
        char body_label[64];
        char end_label[64];
        cp_new_label(ctx, begin_label, sizeof(begin_label));
        cp_new_label(ctx, body_label, sizeof(body_label));
        cp_new_label(ctx, end_label, sizeof(end_label));
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", begin_label);
        cp_gen_condition(node->children[0], ctx, body_label, end_label);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", body_label);
        cp_gen_stmt(node->children[1], ctx);
        cp_ir_emit(ctx->list, IR_GOTO, "_", "_", begin_label);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", end_label);
        return;
    }
    for (index = 0; index < node->child_count; ++index) {
        cp_gen_stmt(node->children[index], ctx);
    }
}

static void cp_gen_condition(const ASTNode *node, IRContext *ctx, const char *true_label, const char *false_label) {
    char lhs[64];
    char rhs[64];
    char place[64];
    if (node == NULL || ctx == NULL) {
        return;
    }
    if (is_compare_expr(node) && node->child_count >= 2) {
        cp_gen_expr(node->children[0], ctx, lhs, sizeof(lhs));
        cp_gen_expr(node->children[1], ctx, rhs, sizeof(rhs));
        cp_ir_emit(ctx->list, compare_op(node->symbol_name), lhs, rhs, true_label);
        cp_ir_emit(ctx->list, IR_GOTO, "_", "_", false_label);
        return;
    }
    if (is_logical_expr(node)) {
        if (strcmp(node->symbol_name, "and") == 0 && node->child_count >= 2) {
            char rhs_label[64];
            cp_new_label(ctx, rhs_label, sizeof(rhs_label));
            cp_gen_condition(node->children[0], ctx, rhs_label, false_label);
            cp_ir_emit(ctx->list, IR_LABEL, "_", "_", rhs_label);
            cp_gen_condition(node->children[1], ctx, true_label, false_label);
            return;
        }
        if (strcmp(node->symbol_name, "or") == 0 && node->child_count >= 2) {
            char rhs_label[64];
            cp_new_label(ctx, rhs_label, sizeof(rhs_label));
            cp_gen_condition(node->children[0], ctx, true_label, rhs_label);
            cp_ir_emit(ctx->list, IR_LABEL, "_", "_", rhs_label);
            cp_gen_condition(node->children[1], ctx, true_label, false_label);
            return;
        }
        if (strcmp(node->symbol_name, "not") == 0 && node->child_count >= 1) {
            cp_gen_condition(node->children[0], ctx, false_label, true_label);
            return;
        }
    }
    cp_gen_expr(node, ctx, place, sizeof(place));
    cp_ir_emit(ctx->list, IR_IF_NE, place, "0", true_label);
    cp_ir_emit(ctx->list, IR_GOTO, "_", "_", false_label);
}

static void cp_gen_expr(const ASTNode *node, IRContext *ctx, char *out_place, size_t out_place_size) {
    char lhs[64];
    char rhs[64];
    char true_label[64];
    char end_label[64];
    if (node == NULL || ctx == NULL || out_place == NULL) {
        return;
    }
    if (node->node_type == AST_IDENTIFIER || node->node_type == AST_LITERAL) {
        snprintf(out_place, out_place_size, "%s", node->lexeme);
        return;
    }
    if (node->node_type == AST_EXPRESSION && strcmp(node->symbol_name, "assign") == 0 && node->child_count >= 2) {
        cp_gen_expr(node->children[1], ctx, rhs, sizeof(rhs));
        cp_ir_emit(ctx->list, IR_ASSIGN, rhs, "_", node->children[0]->lexeme);
        snprintf(out_place, out_place_size, "%s", node->children[0]->lexeme);
        return;
    }
    if (is_compare_expr(node) && node->child_count >= 2) {
        cp_new_temp(ctx, out_place, out_place_size);
        cp_new_label(ctx, true_label, sizeof(true_label));
        cp_new_label(ctx, end_label, sizeof(end_label));
        cp_ir_emit(ctx->list, IR_ASSIGN, "0", "_", out_place);
        cp_gen_condition(node, ctx, true_label, end_label);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", true_label);
        cp_ir_emit(ctx->list, IR_ASSIGN, "1", "_", out_place);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", end_label);
        return;
    }
    if (is_logical_expr(node)) {
        char false_label[64];
        cp_new_temp(ctx, out_place, out_place_size);
        cp_new_label(ctx, true_label, sizeof(true_label));
        cp_new_label(ctx, false_label, sizeof(false_label));
        cp_new_label(ctx, end_label, sizeof(end_label));
        cp_ir_emit(ctx->list, IR_ASSIGN, "0", "_", out_place);
        cp_gen_condition(node, ctx, true_label, false_label);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", true_label);
        cp_ir_emit(ctx->list, IR_ASSIGN, "1", "_", out_place);
        cp_ir_emit(ctx->list, IR_GOTO, "_", "_", end_label);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", false_label);
        cp_ir_emit(ctx->list, IR_LABEL, "_", "_", end_label);
        return;
    }
    if (node->node_type == AST_EXPRESSION && strcmp(node->symbol_name, "call") == 0 && node->child_count >= 1) {
        int index;
        const char *function_name = node->children[0] == NULL ? "call" : node->children[0]->lexeme;
        for (index = 1; index < node->child_count; ++index) {
            cp_gen_expr(node->children[index], ctx, lhs, sizeof(lhs));
            cp_ir_emit(ctx->list, IR_PARAM, lhs, "_", "_");
        }
        cp_new_temp(ctx, out_place, out_place_size);
        snprintf(rhs, sizeof(rhs), "%d", node->child_count - 1);
        cp_ir_emit(ctx->list, IR_CALL, function_name, rhs, out_place);
        return;
    }
    if (node->node_type == AST_EXPRESSION && node->child_count >= 2) {
        IROp op = IR_ADD;
        cp_gen_expr(node->children[0], ctx, lhs, sizeof(lhs));
        cp_gen_expr(node->children[1], ctx, rhs, sizeof(rhs));
        if (strcmp(node->symbol_name, "sub") == 0) op = IR_SUB;
        else if (strcmp(node->symbol_name, "mul") == 0) op = IR_MUL;
        else if (strcmp(node->symbol_name, "div") == 0) op = IR_DIV;
        cp_new_temp(ctx, out_place, out_place_size);
        cp_ir_emit(ctx->list, op, lhs, rhs, out_place);
        return;
    }
    snprintf(out_place, out_place_size, "_");
}

QuadrupleListResult cp_build_ir(const ASTNode *ast) {
    QuadrupleListResult result;
    IRContext ctx;
    RESULT_SUCCESS(&result.base);
    cp_ir_list_init(&result.data);
    ctx.list = &result.data;
    ctx.next_temp_id = 0;
    ctx.next_label_id = 0;
    cp_gen_stmt(ast, &ctx);
    return result;
}
