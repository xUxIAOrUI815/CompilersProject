#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compiler_project/common/token.h"
#include "compiler_project/lex/automata.h"
#include "compiler_project/lex/lex_parser.h"
#include "compiler_project/lex/lexer_runtime.h"
#include "compiler_project/semantic/ir_builder.h"
#include "compiler_project/semantic/semantic_action.h"
#include "compiler_project/semantic/type_checker.h"
#include "compiler_project/yacc/lr_builder.h"
#include "compiler_project/yacc/parser_runtime.h"
#include "compiler_project/yacc/yacc_parser.h"

static int read_file(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "r");
    size_t read_size;
    if (file == NULL) {
        return 0;
    }
    read_size = fread(buffer, 1, size - 1, file);
    buffer[read_size] = '\0';
    fclose(file);
    return 1;
}

static void print_usage(void) {
    printf("Usage:\n");
    printf("  compiler_project check-layout\n");
    printf("  compiler_project lex --spec <path> --source <path>\n");
    printf("  compiler_project yacc --spec <path>\n");
    printf("  compiler_project --lex <path> --yacc <path> --input <path> --out <dir>\n");
    printf("  compiler_project --parse-lex <path>\n");
    printf("  compiler_project --build-dfa <path>\n");
    printf("  compiler_project --tokenize <spec> <input>\n");
    printf("  compiler_project --parse-yacc <path>\n");
    printf("  compiler_project --build-table <path>\n");
    printf("  compiler_project --parse <lex> <yacc> <input>\n");
    printf("  compiler_project --semantic <lex> <yacc> <input>\n");
    printf("  compiler_project --ir <lex> <yacc> <input>\n");
    printf("  compiler_project parse --lex-spec <path> --yacc-spec <path> --source <path>\n");
    printf("  compiler_project semantic --lex-spec <path> --yacc-spec <path> --source <path>\n");
    printf("  compiler_project ir --lex-spec <path> --yacc-spec <path> --source <path>\n");
}

static int ensure_dir(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (access(path, F_OK) == 0) {
        return 1;
    }
    return mkdir(path, 0777) == 0;
}

static int ensure_output_tree(const char *base) {
    char path[512];
    const char *subs[] = {
        "",
        "/token_stream",
        "/automata",
        "/ast",
        "/symbol_table",
        "/ir",
        "/log"
    };
    size_t i;
    for (i = 0; i < sizeof(subs) / sizeof(subs[0]); ++i) {
        snprintf(path, sizeof(path), "%s%s", base, subs[i]);
        if (!ensure_dir(path)) {
            return 0;
        }
    }
    return 1;
}

static int write_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return 0;
    }
    fputs(text == NULL ? "" : text, file);
    fclose(file);
    return 1;
}

static int cmd_check_layout(void) {
    const char *paths[] = {
        "docs",
        "samples",
        "include/compiler_project/common",
        "include/compiler_project/lex",
        "include/compiler_project/yacc",
        "include/compiler_project/semantic",
        "src",
        "tests",
        "output"
    };
    size_t i;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        FILE *probe = fopen(paths[i], "r");
        if (probe != NULL) {
            fclose(probe);
            continue;
        }
        if (access(paths[i], 0) != 0) {
            printf("{\"ok\":false,\"missing\":\"%s\"}\n", paths[i]);
            return 1;
        }
    }
    printf("{\"ok\":true}\n");
    return 0;
}

static int cmd_lex(const char *spec_path, const char *source_path) {
    LexSpecResult spec_result;
    TokenStreamResult token_result;
    char source[4096];
    int i;

    spec_result = cp_parse_lex_spec(spec_path);
    if (!spec_result.base.ok) {
        printf("lex spec error: %s\n", spec_result.base.errors[0].message);
        return 1;
    }
    if (!read_file(source_path, source, sizeof(source))) {
        printf("cannot read source file\n");
        return 1;
    }
    token_result = cp_tokenize_source(&spec_result.data, source);
    if (!token_result.base.ok) {
        printf("tokenize error: %s\n", token_result.base.errors[0].message);
        return 1;
    }
    for (i = 0; i < token_result.data.count; ++i) {
        const Token *token = &token_result.data.tokens[i];
        printf("(%s, \"%s\", %d, %d, %s)\n",
            token->kind,
            token->lexeme,
            token->line,
            token->column,
            token->attr[0] == '\0' ? "null" : token->attr);
    }
    return 0;
}

static int cmd_yacc(const char *spec_path) {
    GrammarResult grammar_result = cp_parse_yacc_spec(spec_path);
    int i;
    if (!grammar_result.base.ok) {
        printf("yacc spec error: %s\n", grammar_result.base.errors[0].message);
        return 1;
    }
    printf("start_symbol: %s\n", grammar_result.data.start_symbol);
    for (i = 0; i < grammar_result.data.production_count; ++i) {
        int j;
        const Production *production = &grammar_result.data.productions[i];
        printf("%s ->", production->lhs);
        for (j = 0; j < production->rhs_count; ++j) {
            printf(" %s", production->rhs[j]);
        }
        printf("\n");
    }
    return 0;
}

static void print_ast(const ASTNode *node, int depth) {
    int i;
    if (node == NULL) {
        return;
    }
    for (i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("%s", node->symbol_name[0] == '\0' ? cp_ast_node_type_name(node->node_type) : node->symbol_name);
    if (node->lexeme[0] != '\0') {
        printf(" [%s]", node->lexeme);
    }
    printf("\n");
    for (i = 0; i < node->child_count; ++i) {
        print_ast(node->children[i], depth + 1);
    }
}

static void append_ast_text(const ASTNode *node, int depth, char *buffer, size_t buffer_size) {
    int i;
    size_t used = strlen(buffer);
    if (node == NULL || used >= buffer_size) {
        return;
    }
    for (i = 0; i < depth && used + 2 < buffer_size; ++i) {
        used += snprintf(buffer + used, buffer_size - used, "  ");
    }
    used += snprintf(buffer + used, buffer_size - used, "%s", node->symbol_name[0] == '\0' ? cp_ast_node_type_name(node->node_type) : node->symbol_name);
    if (node->lexeme[0] != '\0') {
        used += snprintf(buffer + used, buffer_size - used, " [%s]", node->lexeme);
    }
    used += snprintf(buffer + used, buffer_size - used, "\n");
    for (i = 0; i < node->child_count; ++i) {
        append_ast_text(node->children[i], depth + 1, buffer, buffer_size);
    }
}

static void build_ast_text(const ASTNode *node, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';
    append_ast_text(node, 0, buffer, buffer_size);
}

static void append_tokens_text(const TokenStream *stream, char *buffer, size_t buffer_size) {
    int i;
    size_t used = 0;
    buffer[0] = '\0';
    if (stream == NULL) {
        return;
    }
    for (i = 0; i < stream->count && used < buffer_size; ++i) {
        const Token *token = &stream->tokens[i];
        used += snprintf(buffer + used, buffer_size - used, "(%s, \"%s\", %d, %d, %s)\n",
            token->kind, token->lexeme, token->line, token->column, token->attr[0] == '\0' ? "null" : token->attr);
    }
}

static void append_scopes_text(const SymbolTable *table, char *buffer, size_t buffer_size) {
    int i;
    size_t used = 0;
    buffer[0] = '\0';
    if (table == NULL) {
        return;
    }
    for (i = 0; i < table->scope_count && used < buffer_size; ++i) {
        int j;
        const Scope *scope = &table->scopes[i];
        used += snprintf(buffer + used, buffer_size - used, "[scope %d] %s name=%s parent=%d entries=%d\n",
            scope->id, cp_scope_kind_name(scope->kind), scope->name, scope->parent_scope_id, scope->count);
        for (j = 0; j < scope->count && used < buffer_size; ++j) {
            const SymbolEntry *entry = &scope->entries[j];
            used += snprintf(buffer + used, buffer_size - used, "  - %s %s offset=%d scope=%d initialized=%d\n",
                entry->type_expr.text, entry->name, entry->offset, entry->scope_id, entry->initialized);
        }
    }
}

static void append_ir_text(const QuadrupleList *ir, char *buffer, size_t buffer_size) {
    int i;
    size_t used = 0;
    buffer[0] = '\0';
    if (ir == NULL) {
        return;
    }
    for (i = 0; i < ir->count && used < buffer_size; ++i) {
        const Quadruple *quad = &ir->items[i];
        used += snprintf(buffer + used, buffer_size - used, "(%d) (%s, %s, %s, %s)\n",
            quad->index, cp_ir_op_name(quad->op), quad->arg1, quad->arg2, quad->result);
    }
}

static void append_parse_trace_text(const ParseTrace *trace, char *buffer, size_t buffer_size) {
    int i;
    size_t used = 0;
    buffer[0] = '\0';
    if (trace == NULL) {
        return;
    }
    for (i = 0; i < trace->step_count && used < buffer_size; ++i) {
        used += snprintf(buffer + used, buffer_size - used, "%03d: %s\n", i + 1, trace->steps[i]);
    }
}

static void append_grammar_text(const Grammar *grammar, char *buffer, size_t buffer_size) {
    int i;
    size_t used = 0;
    buffer[0] = '\0';
    if (grammar == NULL) {
        return;
    }
    used += snprintf(buffer + used, buffer_size - used, "start_symbol: %s\n", grammar->start_symbol);
    for (i = 0; i < grammar->production_count && used < buffer_size; ++i) {
        int j;
        const Production *production = &grammar->productions[i];
        used += snprintf(buffer + used, buffer_size - used, "p%d: %s ->", production->id, production->lhs);
        for (j = 0; j < production->rhs_count && used < buffer_size; ++j) {
            used += snprintf(buffer + used, buffer_size - used, " %s", production->rhs[j]);
        }
        if (production->rhs_count == 0) {
            used += snprintf(buffer + used, buffer_size - used, " /* epsilon */");
        }
        if (production->action_text[0] != '\0') {
            used += snprintf(buffer + used, buffer_size - used, " { %s }", production->action_text);
        }
        used += snprintf(buffer + used, buffer_size - used, "\n");
    }
}

static void append_lr_table_text(const LRTable *table, char *buffer, size_t buffer_size) {
    int i;
    int j;
    size_t used = 0;
    buffer[0] = '\0';
    if (table == NULL) {
        return;
    }
    for (i = 0; i < table->state_count && used < buffer_size; ++i) {
        used += snprintf(buffer + used, buffer_size - used, "%s\n", table->states[i]);
        for (j = 0; j < table->symbols.count && used < buffer_size; ++j) {
            const LRAction *action = &table->action[i][j];
            if (action->kind == CP_ACT_SHIFT) {
                used += snprintf(buffer + used, buffer_size - used, "  %s: shift %d\n", table->symbols.names[j], action->target);
            } else if (action->kind == CP_ACT_REDUCE) {
                used += snprintf(buffer + used, buffer_size - used, "  %s: reduce %d\n", table->symbols.names[j], action->target);
            } else if (action->kind == CP_ACT_ACCEPT) {
                used += snprintf(buffer + used, buffer_size - used, "  %s: accept\n", table->symbols.names[j]);
            }
        }
    }
}

static void print_symbol_table(const SymbolTable *table) {
    int i;
    if (table == NULL) {
        return;
    }
    for (i = 0; i < table->scope_count; ++i) {
        int j;
        const Scope *scope = &table->scopes[i];
        printf("[scope %d] %s name=%s parent=%d entries=%d\n",
            scope->id,
            cp_scope_kind_name(scope->kind),
            scope->name,
            scope->parent_scope_id,
            scope->count);
        for (j = 0; j < scope->count; ++j) {
            const SymbolEntry *entry = &scope->entries[j];
            printf("  - %s %s offset=%d scope=%d initialized=%d\n",
                entry->type_expr.text,
                entry->name,
                entry->offset,
                entry->scope_id,
                entry->initialized);
        }
    }
}

static void print_ir(const QuadrupleList *ir) {
    int i;
    if (ir == NULL) {
        return;
    }
    for (i = 0; i < ir->count; ++i) {
        const Quadruple *quad = &ir->items[i];
        printf("(%d) (%s, %s, %s, %s)\n",
            quad->index,
            cp_ir_op_name(quad->op),
            quad->arg1,
            quad->arg2,
            quad->result);
    }
}

static int cmd_semantic(const char *lex_spec_path, const char *yacc_spec_path, const char *source_path) {
    static LexSpecResult lex_spec;
    static GrammarResult grammar;
    static TokenStreamResult tokens;
    static ASTNodeResult ast;
    static SymbolTableResult symbols;
    static BoolResult type_check;
    static QuadrupleListResult ir;
    static char source[4096];

    lex_spec = cp_parse_lex_spec(lex_spec_path);
    if (!lex_spec.base.ok) {
        printf("lex spec error: %s\n", lex_spec.base.errors[0].message);
        return 1;
    }
    grammar = cp_parse_yacc_spec(yacc_spec_path);
    if (!grammar.base.ok) {
        printf("yacc spec error: %s\n", grammar.base.errors[0].message);
        return 1;
    }
    if (!read_file(source_path, source, sizeof(source))) {
        printf("cannot read source file\n");
        return 1;
    }
    tokens = cp_tokenize_source(&lex_spec.data, source);
    if (!tokens.base.ok) {
        printf("tokenize error: %s\n", tokens.base.errors[0].message);
        return 1;
    }
    ast = cp_parse_tokens(&grammar.data, &tokens.data);
    if (!ast.base.ok) {
        printf("parse error: %s\n", ast.base.errors[0].message);
        return 1;
    }
    symbols = cp_run_semantic_actions(&ast.data);
    if (!symbols.base.ok) {
        printf("semantic action error: %s\n", symbols.base.errors[0].message);
        return 1;
    }
    type_check = cp_check_types(&ast.data);
    if (!type_check.base.ok) {
        printf("type error: %s\n", type_check.base.errors[0].message);
        return 1;
    }
    ir = cp_build_ir(&ast.data);
    if (!ir.base.ok) {
        printf("ir error: %s\n", ir.base.errors[0].message);
        return 1;
    }
    printf("AST root: %s (%s)\n", ast.data.symbol_name, cp_ast_node_type_name(ast.data.node_type));
    print_symbol_table(&symbols.data);
    print_ir(&ir.data);
    return 0;
}

static int cmd_parse(const char *lex_spec_path, const char *yacc_spec_path, const char *source_path) {
    LexSpecResult lex_spec;
    GrammarResult grammar;
    TokenStreamResult tokens;
    ASTNodeResult ast;
    char source[4096];
    lex_spec = cp_parse_lex_spec(lex_spec_path);
    grammar = cp_parse_yacc_spec(yacc_spec_path);
    if (!lex_spec.base.ok) {
        printf("lex spec error: %s\n", lex_spec.base.errors[0].message);
        return 1;
    }
    if (!grammar.base.ok) {
        printf("yacc spec error: %s\n", grammar.base.errors[0].message);
        return 1;
    }
    if (!read_file(source_path, source, sizeof(source))) {
        printf("cannot read source file\n");
        return 1;
    }
    tokens = cp_tokenize_source(&lex_spec.data, source);
    if (!tokens.base.ok) {
        printf("tokenize error: %s\n", tokens.base.errors[0].message);
        return 1;
    }
    ast = cp_parse_tokens(&grammar.data, &tokens.data);
    if (!ast.base.ok) {
        printf("parse error: %s\n", ast.base.errors[0].message);
        return 1;
    }
    print_ast(&ast.data, 0);
    return 0;
}

static int cmd_ir(const char *lex_spec_path, const char *yacc_spec_path, const char *source_path) {
    LexSpecResult lex_spec;
    GrammarResult grammar;
    TokenStreamResult tokens;
    ASTNodeResult ast;
    QuadrupleListResult ir;
    char source[4096];
    lex_spec = cp_parse_lex_spec(lex_spec_path);
    grammar = cp_parse_yacc_spec(yacc_spec_path);
    if (!lex_spec.base.ok) {
        printf("lex spec error: %s\n", lex_spec.base.errors[0].message);
        return 1;
    }
    if (!grammar.base.ok) {
        printf("yacc spec error: %s\n", grammar.base.errors[0].message);
        return 1;
    }
    if (!read_file(source_path, source, sizeof(source))) {
        printf("cannot read source file\n");
        return 1;
    }
    tokens = cp_tokenize_source(&lex_spec.data, source);
    if (!tokens.base.ok) {
        printf("tokenize error: %s\n", tokens.base.errors[0].message);
        return 1;
    }
    ast = cp_parse_tokens(&grammar.data, &tokens.data);
    if (!ast.base.ok) {
        printf("parse error: %s\n", ast.base.errors[0].message);
        return 1;
    }
    ir = cp_build_ir(&ast.data);
    if (!ir.base.ok) {
        printf("ir error: %s\n", ir.base.errors[0].message);
        return 1;
    }
    print_ir(&ir.data);
    return 0;
}

static int cmd_unified(const char *lex_spec_path, const char *yacc_spec_path, const char *source_path, const char *out_dir) {
    static LexSpecResult lex_spec;
    static GrammarResult grammar;
    static TokenStreamResult tokens;
    static NFAResult nfa;
    static DFAResult dfa;
    static DFAResult min_dfa;
    static ASTNodeResult ast;
    static SymbolTableResult symbols;
    static BoolResult type_check;
    static QuadrupleListResult ir;
    static char source[4096];
    static char text[32768];
    static char path[512];

    if (!ensure_output_tree(out_dir)) {
        printf("cannot create output directories under %s\n", out_dir);
        return 1;
    }
    lex_spec = cp_parse_lex_spec(lex_spec_path);
    grammar = cp_parse_yacc_spec(yacc_spec_path);
    if (!lex_spec.base.ok) {
        printf("lex spec error: %s\n", lex_spec.base.errors[0].message);
        return 1;
    }
    if (!grammar.base.ok) {
        printf("yacc spec error: %s\n", grammar.base.errors[0].message);
        return 1;
    }
    if (!read_file(source_path, source, sizeof(source))) {
        printf("cannot read source file\n");
        return 1;
    }
    nfa = cp_build_combined_nfa_from_lex_spec(&lex_spec.data);
    if (!nfa.base.ok) {
        printf("nfa error: %s\n", nfa.base.errors[0].message);
        return 1;
    }
    dfa = cp_determinize(&nfa.data);
    if (!dfa.base.ok) {
        printf("dfa error: %s\n", dfa.base.errors[0].message);
        return 1;
    }
    min_dfa = cp_minimize_dfa(&dfa.data);
    if (!min_dfa.base.ok) {
        printf("min dfa error: %s\n", min_dfa.base.errors[0].message);
        return 1;
    }
    tokens = cp_tokenize_source(&lex_spec.data, source);
    if (!tokens.base.ok) {
        printf("tokenize error: %s\n", tokens.base.errors[0].message);
        return 1;
    }
    ast = cp_parse_tokens(&grammar.data, &tokens.data);
    if (!ast.base.ok) {
        printf("parse error: %s\n", ast.base.errors[0].message);
        return 1;
    }
    symbols = cp_run_semantic_actions(&ast.data);
    if (!symbols.base.ok) {
        printf("semantic action error: %s\n", symbols.base.errors[0].message);
        return 1;
    }
    type_check = cp_check_types(&ast.data);
    if (!type_check.base.ok) {
        printf("type error: %s\n", type_check.base.errors[0].message);
        return 1;
    }
    ir = cp_build_ir(&ast.data);
    if (!ir.base.ok) {
        printf("ir error: %s\n", ir.base.errors[0].message);
        return 1;
    }

    append_tokens_text(&tokens.data, text, sizeof(text));
    snprintf(path, sizeof(path), "%s/token_stream/tokens.txt", out_dir);
    write_text_file(path, text);

    cp_dump_nfa_text(&nfa.data, text, (int) sizeof(text));
    snprintf(path, sizeof(path), "%s/automata/nfa.txt", out_dir);
    write_text_file(path, text);

    cp_dump_dfa_text(&dfa.data, text, (int) sizeof(text));
    snprintf(path, sizeof(path), "%s/automata/dfa.txt", out_dir);
    write_text_file(path, text);

    cp_dump_dfa_text(&min_dfa.data, text, (int) sizeof(text));
    snprintf(path, sizeof(path), "%s/automata/min_dfa.txt", out_dir);
    write_text_file(path, text);

    build_ast_text(&ast.data, text, sizeof(text));
    snprintf(path, sizeof(path), "%s/ast/ast.txt", out_dir);
    write_text_file(path, text);

    append_scopes_text(&symbols.data, text, sizeof(text));
    snprintf(path, sizeof(path), "%s/symbol_table/scopes.txt", out_dir);
    write_text_file(path, text);

    append_ir_text(&ir.data, text, sizeof(text));
    snprintf(path, sizeof(path), "%s/ir/quads.txt", out_dir);
    write_text_file(path, text);

    append_parse_trace_text(cp_last_parse_trace(), text, sizeof(text));
    snprintf(path, sizeof(path), "%s/log/parse_steps.txt", out_dir);
    write_text_file(path, text);

    append_grammar_text(&grammar.data, text, sizeof(text));
    snprintf(path, sizeof(path), "%s/log/grammar.txt", out_dir);
    write_text_file(path, text);

    printf("wrote outputs under %s\n", out_dir);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 9
        && strcmp(argv[1], "--lex") == 0
        && strcmp(argv[3], "--yacc") == 0
        && strcmp(argv[5], "--input") == 0
        && strcmp(argv[7], "--out") == 0) {
        return cmd_unified(argv[2], argv[4], argv[6], argv[8]);
    }
    if (argc == 3 && strcmp(argv[1], "--parse-lex") == 0) {
        LexSpecResult spec = cp_parse_lex_spec(argv[2]);
        if (!spec.base.ok) {
            printf("lex spec error: %s\n", spec.base.errors[0].message);
            return 1;
        }
        printf("definitions=%d rules=%d\n", spec.data.definition_count, spec.data.rule_count);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--build-dfa") == 0) {
        LexSpecResult spec = cp_parse_lex_spec(argv[2]);
        DFAResult dfa;
        char text[32768];
        if (!spec.base.ok) {
            printf("lex spec error: %s\n", spec.base.errors[0].message);
            return 1;
        }
        dfa = cp_build_dfa_from_lex_spec(&spec.data);
        if (!dfa.base.ok) {
            printf("dfa error: %s\n", dfa.base.errors[0].message);
            return 1;
        }
        cp_dump_dfa_text(&dfa.data, text, (int) sizeof(text));
        printf("%s", text);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--tokenize") == 0) {
        return cmd_lex(argv[2], argv[3]);
    }
    if (argc == 3 && strcmp(argv[1], "--parse-yacc") == 0) {
        return cmd_yacc(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "--build-table") == 0) {
        GrammarResult grammar = cp_parse_yacc_spec(argv[2]);
        LRTableResult table;
        char text[32768];
        if (!grammar.base.ok) {
            printf("yacc spec error: %s\n", grammar.base.errors[0].message);
            return 1;
        }
        table = cp_build_lr_table(&grammar.data);
        if (!table.base.ok) {
            printf("lr table error: %s\n", table.base.errors[0].message);
            return 1;
        }
        append_lr_table_text(&table.data, text, sizeof(text));
        printf("%s", text);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--parse") == 0) {
        return cmd_parse(argv[2], argv[3], argv[4]);
    }
    if (argc == 5 && strcmp(argv[1], "--semantic") == 0) {
        return cmd_semantic(argv[2], argv[3], argv[4]);
    }
    if (argc == 5 && strcmp(argv[1], "--ir") == 0) {
        return cmd_ir(argv[2], argv[3], argv[4]);
    }
    if (argc < 2) {
        print_usage();
        return 1;
    }
    if (strcmp(argv[1], "check-layout") == 0) {
        return cmd_check_layout();
    }
    if (strcmp(argv[1], "lex") == 0 && argc == 6 && strcmp(argv[2], "--spec") == 0 && strcmp(argv[4], "--source") == 0) {
        return cmd_lex(argv[3], argv[5]);
    }
    if (strcmp(argv[1], "yacc") == 0 && argc == 4 && strcmp(argv[2], "--spec") == 0) {
        return cmd_yacc(argv[3]);
    }
    if (strcmp(argv[1], "parse") == 0
        && argc == 8
        && strcmp(argv[2], "--lex-spec") == 0
        && strcmp(argv[4], "--yacc-spec") == 0
        && strcmp(argv[6], "--source") == 0) {
        return cmd_parse(argv[3], argv[5], argv[7]);
    }
    if (strcmp(argv[1], "semantic") == 0
        && argc == 8
        && strcmp(argv[2], "--lex-spec") == 0
        && strcmp(argv[4], "--yacc-spec") == 0
        && strcmp(argv[6], "--source") == 0) {
        return cmd_semantic(argv[3], argv[5], argv[7]);
    }
    if (strcmp(argv[1], "ir") == 0
        && argc == 8
        && strcmp(argv[2], "--lex-spec") == 0
        && strcmp(argv[4], "--yacc-spec") == 0
        && strcmp(argv[6], "--source") == 0) {
        return cmd_ir(argv[3], argv[5], argv[7]);
    }
    print_usage();
    return 1;
}
