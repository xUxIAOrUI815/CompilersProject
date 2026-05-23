#include "compiler_project/yacc/parser_runtime.h"

#include <stdio.h>
#include <string.h>

#include "compiler_project/common/error.h"
#include "compiler_project/yacc/lr_builder.h"

#define CP_PARSE_NODE_POOL 512
#define CP_PARSE_STACK_SIZE 512

static ASTNode node_pool[CP_PARSE_NODE_POOL];
static int node_pool_count = 0;
static ParseTrace parse_trace;

static ASTNode *alloc_node(ASTNodeType type, const char *symbol_name, const char *lexeme, int line, int column) {
    ASTNode *node;
    if (node_pool_count >= CP_PARSE_NODE_POOL) {
        return NULL;
    }
    node = &node_pool[node_pool_count++];
    cp_ast_init(node, type, symbol_name, lexeme, line, column);
    node->id = node_pool_count;
    return node;
}

static const Production *find_production(const Grammar *grammar, int production_id) {
    int i;
    if (production_id == 0) {
        return NULL;
    }
    for (i = 0; grammar != NULL && i < grammar->production_count; ++i) {
        if (grammar->productions[i].id == production_id) {
            return &grammar->productions[i];
        }
    }
    return NULL;
}

static void reset_trace(void) {
    memset(&parse_trace, 0, sizeof(parse_trace));
}

static void append_name(char *buffer, size_t buffer_size, const char *separator, const char *name) {
    size_t used = strlen(buffer);
    if (used >= buffer_size) {
        return;
    }
    snprintf(buffer + used, buffer_size - used, "%s%s", separator, name == NULL ? "" : name);
}

static void trace_action(const int *states, char symbols[][32], int stack_count, const Token *lookahead, const char *action) {
    char state_text[128];
    char symbol_text[192];
    char line[256];
    int i;
    size_t used = 0;
    state_text[0] = '\0';
    symbol_text[0] = '\0';
    for (i = 0; i < stack_count && used < sizeof(state_text); ++i) {
        used += snprintf(state_text + used, sizeof(state_text) - used, "%s%d", i == 0 ? "" : "/", states[i]);
    }
    for (i = 0; i < stack_count; ++i) {
        append_name(symbol_text, sizeof(symbol_text), i == 0 ? "" : "/", symbols[i]);
    }
    if (parse_trace.step_count >= (int) (sizeof(parse_trace.steps) / sizeof(parse_trace.steps[0]))) {
        return;
    }
    snprintf(line, sizeof(line), "states=%s symbols=%s lookahead=%s action=%s",
        state_text,
        symbol_text,
        lookahead == NULL ? "EOF" : lookahead->kind,
        action == NULL ? "" : action);
    snprintf(parse_trace.steps[parse_trace.step_count++], sizeof(parse_trace.steps[0]), "%s", line);
}

static int has_action(const LRTable *table, int state, int symbol_index) {
    return table != NULL
        && state >= 0
        && state < table->state_count
        && symbol_index >= 0
        && symbol_index < table->symbols.count
        && table->action[state][symbol_index].kind != CP_ACT_ERROR;
}

static void build_expected_tokens(const LRTable *table, int state, char *buffer, size_t buffer_size) {
    int i;
    int written = 0;
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';
    for (i = 0; table != NULL && i < table->symbols.count; ++i) {
        if (has_action(table, state, i)) {
            append_name(buffer, buffer_size, written ? ", " : "", table->symbols.names[i]);
            written = 1;
        }
    }
    if (!written) {
        snprintf(buffer, buffer_size, "<none>");
    }
}

static void make_unexpected_token_error(CompilerError *error, const LRTable *table, int state, const Token *lookahead) {
    char expected[192];
    char message[256];
    build_expected_tokens(table, state, expected, sizeof(expected));
    snprintf(message, sizeof(message), "unexpected token %s, expected: %s",
        lookahead == NULL ? "EOF" : lookahead->kind,
        expected);
    cp_make_error(error, 6002, message, lookahead == NULL ? -1 : lookahead->line, lookahead == NULL ? -1 : lookahead->column, "parser");
}

const ParseTrace *cp_last_parse_trace(void) {
    return &parse_trace;
}

static ASTNode *make_token_node(const Token *token) {
    ASTNodeType type = AST_TERMINAL;
    const char *symbol_name = token == NULL ? "" : token->kind;
    if (token == NULL) {
        return NULL;
    }
    if (strcmp(token->kind, "ID") == 0) {
        type = AST_IDENTIFIER;
        symbol_name = "identifier";
    } else if (strcmp(token->kind, "NUM") == 0) {
        type = AST_LITERAL;
        symbol_name = "literal";
    } else if (strcmp(token->kind, "INT") == 0 || strcmp(token->kind, "FLOAT") == 0
        || strcmp(token->kind, "VOID") == 0 || strcmp(token->kind, "BOOL") == 0
        || strcmp(token->kind, "CHAR") == 0) {
        type = AST_TYPE_NAME;
        symbol_name = "type";
    }
    return alloc_node(type, symbol_name, token->lexeme, token->line, token->column);
}

static ASTNode *make_list_node(const char *name) {
    return alloc_node(AST_NON_TERMINAL, name, "", -1, -1);
}

static void append_child_if(ASTNode *parent, ASTNode *child) {
    if (parent != NULL && child != NULL) {
        cp_ast_add_child(parent, child);
    }
}

static void flatten_children(ASTNode *parent, ASTNode *list) {
    int i;
    if (parent == NULL || list == NULL) {
        return;
    }
    for (i = 0; i < list->child_count; ++i) {
        cp_ast_add_child(parent, list->children[i]);
    }
}

static ASTNode *build_reduction_node(int production_id, ASTNode **rhs, int rhs_count, const Production *production) {
    ASTNode *node = NULL;
    ASTNode *list = NULL;
    int line = rhs_count > 0 && rhs[0] != NULL ? rhs[0]->line : -1;
    int column = rhs_count > 0 && rhs[0] != NULL ? rhs[0]->column : -1;
    (void) production;
    switch (production_id) {
        case 1:
            node = alloc_node(AST_PROGRAM, "program", "", line, column);
            if (rhs_count > 0) {
                flatten_children(node, rhs[0]);
            }
            return node;
        case 2:
            list = make_list_node("ext_list");
            append_child_if(list, rhs[0]);
            if (rhs_count > 1) {
                flatten_children(list, rhs[1]);
            }
            return list;
        case 3:
            list = make_list_node("ext_list");
            append_child_if(list, rhs[0]);
            return list;
        case 4:
        case 5:
        case 9:
        case 20:
        case 26:
        case 29:
        case 30:
        case 31:
        case 34:
            return rhs_count > 0 ? rhs[0] : NULL;
        case 6:
        case 7:
            node = alloc_node(AST_DECLARATION, "declaration", "", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[1]);
            if (production_id == 6) {
                append_child_if(node, rhs[3]);
            }
            return node;
        case 8:
            node = alloc_node(AST_FUNCTION_DEF, "function", "", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[1]);
            append_child_if(node, rhs[3]);
            append_child_if(node, rhs[5]);
            return node;
        case 10:
            return alloc_node(AST_PARAM_LIST, "params", "", line, column);
        case 11:
            node = alloc_node(AST_PARAM_LIST, "params", "", line, column);
            append_child_if(node, rhs[0]);
            if (rhs_count > 2) {
                flatten_children(node, rhs[2]);
            }
            return node;
        case 12:
            node = alloc_node(AST_PARAM_LIST, "params", "", line, column);
            append_child_if(node, rhs[0]);
            return node;
        case 13:
            node = alloc_node(AST_DECLARATION, "parameter", "", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[1]);
            return node;
        case 14:
        case 15:
        case 16:
            return rhs_count > 0 ? rhs[0] : NULL;
        case 17:
            node = alloc_node(AST_BLOCK, "block", "", line, column);
            if (rhs_count > 1) {
                flatten_children(node, rhs[1]);
            }
            return node;
        case 18:
            list = make_list_node("stmt_list");
            append_child_if(list, rhs[0]);
            if (rhs_count > 1) {
                flatten_children(list, rhs[1]);
            }
            return list;
        case 19:
            list = make_list_node("stmt_list");
            append_child_if(list, rhs[0]);
            return list;
        case 21:
            node = alloc_node(AST_STATEMENT, "return", "", line, column);
            append_child_if(node, rhs[1]);
            return node;
        case 22:
            node = alloc_node(AST_STATEMENT, "expr", "", line, column);
            append_child_if(node, rhs[0]);
            return node;
        case 23:
            node = alloc_node(AST_EXPRESSION, "assign", "=", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 24:
            node = alloc_node(AST_EXPRESSION, "add", "+", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 25:
            node = alloc_node(AST_EXPRESSION, "sub", "-", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 27:
            node = alloc_node(AST_EXPRESSION, "mul", "*", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 28:
            node = alloc_node(AST_EXPRESSION, "div", "/", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 32:
            node = alloc_node(AST_EXPRESSION, "call", "call", line, column);
            append_child_if(node, rhs[0]);
            if (rhs_count > 2) {
                flatten_children(node, rhs[2]);
            }
            return node;
        case 33:
            return rhs_count > 1 ? rhs[1] : NULL;
        case 35:
            return make_list_node("args");
        case 36:
            list = make_list_node("args");
            append_child_if(list, rhs[0]);
            if (rhs_count > 2) {
                flatten_children(list, rhs[2]);
            }
            return list;
        case 37:
            list = make_list_node("args");
            append_child_if(list, rhs[0]);
            return list;
        case 38:
            return rhs_count > 0 ? rhs[0] : NULL;
        case 39:
            node = alloc_node(AST_STATEMENT, "if", "", line, column);
            append_child_if(node, rhs[2]);
            append_child_if(node, rhs[4]);
            return node;
        case 40:
            node = alloc_node(AST_STATEMENT, "if", "", line, column);
            append_child_if(node, rhs[2]);
            append_child_if(node, rhs[4]);
            append_child_if(node, rhs[6]);
            return node;
        case 41:
            node = alloc_node(AST_STATEMENT, "while", "", line, column);
            append_child_if(node, rhs[2]);
            append_child_if(node, rhs[4]);
            return node;
        case 42:
            node = alloc_node(AST_EXPRESSION, "eq", "==", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 43:
            node = alloc_node(AST_EXPRESSION, "ne", "!=", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 44:
            node = alloc_node(AST_EXPRESSION, "lt", "<", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 45:
            node = alloc_node(AST_EXPRESSION, "le", "<=", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 46:
            node = alloc_node(AST_EXPRESSION, "gt", ">", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 47:
            node = alloc_node(AST_EXPRESSION, "ge", ">=", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 48:
            node = alloc_node(AST_EXPRESSION, "and", "&&", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 49:
            node = alloc_node(AST_EXPRESSION, "or", "||", line, column);
            append_child_if(node, rhs[0]);
            append_child_if(node, rhs[2]);
            return node;
        case 50:
            node = alloc_node(AST_EXPRESSION, "not", "!", line, column);
            append_child_if(node, rhs[1]);
            return node;
        case 51:
            return rhs_count > 0 ? rhs[0] : NULL;
        default:
            node = alloc_node(AST_NON_TERMINAL, production == NULL ? "node" : production->lhs, "", line, column);
            if (node != NULL) {
                int i;
                for (i = 0; i < rhs_count; ++i) {
                    append_child_if(node, rhs[i]);
                }
            }
            return node;
    }
}

ASTNodeResult cp_parse_tokens(const Grammar *grammar, const TokenStream *stream) {
    ASTNodeResult result;
    static LRTableResult table_result;
    int state_stack[CP_PARSE_STACK_SIZE];
    char symbol_stack[CP_PARSE_STACK_SIZE][32];
    ASTNode *value_stack[CP_PARSE_STACK_SIZE];
    int top = 0;
    int lookahead_index = 0;

    RESULT_SUCCESS(&result.base);
    memset(&result.data, 0, sizeof(result.data));
    reset_trace();
    node_pool_count = 0;
    memset(value_stack, 0, sizeof(value_stack));
    memset(symbol_stack, 0, sizeof(symbol_stack));

    if (grammar == NULL || stream == NULL) {
        CompilerError error;
        cp_make_error(&error, 6000, "missing grammar or token stream", -1, -1, "parser");
        RESULT_FAILURE(&result.base, error);
        return result;
    }

    table_result = cp_build_lr_table(grammar);
    if (!table_result.base.ok) {
        RESULT_FAILURE(&result.base, table_result.base.errors[0]);
        return result;
    }

    state_stack[0] = 0;
    snprintf(symbol_stack[0], sizeof(symbol_stack[0]), "$");
    while (result.base.ok) {
        const Token *lookahead = lookahead_index < stream->count ? &stream->tokens[lookahead_index] : NULL;
        int symbol_index = cp_grammar_find_symbol(&table_result.data.symbols, lookahead == NULL ? "EOF" : lookahead->kind);
        LRAction action;

        if (symbol_index < 0) {
            CompilerError error;
            make_unexpected_token_error(&error, &table_result.data, state_stack[top], lookahead);
            error.code = 6001;
            RESULT_FAILURE(&result.base, error);
            break;
        }
        action = table_result.data.action[state_stack[top]][symbol_index];
        if (action.kind == CP_ACT_ERROR) {
            CompilerError error;
            make_unexpected_token_error(&error, &table_result.data, state_stack[top], lookahead);
            RESULT_FAILURE(&result.base, error);
            break;
        }

        if (action.kind == CP_ACT_SHIFT) {
            char step[32];
            if (top + 1 >= CP_PARSE_STACK_SIZE) {
                CompilerError error;
                cp_make_error(&error, 6003, "parser stack overflow", lookahead == NULL ? -1 : lookahead->line, lookahead == NULL ? -1 : lookahead->column, "parser");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            snprintf(step, sizeof(step), "shift s%d", action.target);
            trace_action(state_stack, symbol_stack, top + 1, lookahead, step);
            state_stack[++top] = action.target;
            snprintf(symbol_stack[top], sizeof(symbol_stack[top]), "%s", lookahead == NULL ? "EOF" : lookahead->kind);
            value_stack[top] = make_token_node(lookahead);
            lookahead_index++;
            continue;
        }

        if (action.kind == CP_ACT_REDUCE) {
            const Production *production = find_production(grammar, action.target);
            ASTNode *rhs_nodes[CP_MAX_SYMBOLS_PER_RULE];
            ASTNode *reduced;
            int rhs_count;
            int goto_symbol_index;
            int next_state;
            char step[32];
            int i;

            if (production == NULL) {
                CompilerError error;
                cp_make_error(&error, 6004, "invalid reduce production", -1, -1, "parser");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            rhs_count = production->rhs_count;
            snprintf(step, sizeof(step), "reduce p%d", production->id);
            trace_action(state_stack, symbol_stack, top + 1, lookahead, step);
            if (top < rhs_count) {
                CompilerError error;
                cp_make_error(&error, 6005, "parser stack underflow", -1, -1, "parser");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            for (i = rhs_count - 1; i >= 0; --i) {
                rhs_nodes[i] = value_stack[top];
                value_stack[top] = NULL;
                symbol_stack[top][0] = '\0';
                top--;
            }
            reduced = build_reduction_node(production->id, rhs_nodes, rhs_count, production);
            if (reduced == NULL && production->rhs_count > 0) {
                CompilerError error;
                cp_make_error(&error, 6003, "ast node pool exhausted", -1, -1, "parser");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            goto_symbol_index = cp_grammar_find_symbol(&table_result.data.symbols, production->lhs);
            if (goto_symbol_index < 0) {
                CompilerError error;
                cp_make_error(&error, 6006, "missing goto symbol", -1, -1, "parser");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            next_state = table_result.data.go_to[state_stack[top]][goto_symbol_index];
            if (next_state < 0 || top + 1 >= CP_PARSE_STACK_SIZE) {
                CompilerError error;
                cp_make_error(&error, 6007, "invalid goto transition", -1, -1, "parser");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            state_stack[++top] = next_state;
            snprintf(symbol_stack[top], sizeof(symbol_stack[top]), "%s", production->lhs);
            value_stack[top] = reduced;
            parse_trace.reduced_count++;
            continue;
        }

        if (action.kind == CP_ACT_ACCEPT) {
            trace_action(state_stack, symbol_stack, top + 1, lookahead, "accept");
            if (top < 1 || value_stack[top] == NULL) {
                CompilerError error;
                cp_make_error(&error, 6008, "missing parse result", -1, -1, "parser");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            result.data = *value_stack[top];
            break;
        }
    }

    return result;
}
