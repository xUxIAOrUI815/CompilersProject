#include <assert.h>
#include <string.h>

#include "compiler_project/common/ast.h"
#include "compiler_project/common/ir.h"
#include "compiler_project/lex/automata.h"
#include "compiler_project/lex/lex_parser.h"
#include "compiler_project/lex/lexer_runtime.h"
#include "compiler_project/lex/regex_parser.h"
#include "compiler_project/semantic/ir_builder.h"
#include "compiler_project/semantic/semantic_action.h"
#include "compiler_project/semantic/symbol_table.h"
#include "compiler_project/semantic/type_checker.h"
#include "compiler_project/yacc/parser_runtime.h"
#include "compiler_project/yacc/yacc_parser.h"

static void init_decl(ASTNode *decl, ASTNode *type, ASTNode *ident, const char *type_name, const char *name) {
    cp_ast_init(decl, AST_DECLARATION, "declaration", "", 1, 1);
    cp_ast_init(type, AST_TYPE_NAME, "type", type_name, 1, 1);
    cp_ast_init(ident, AST_IDENTIFIER, "identifier", name, 1, 1);
    cp_ast_add_child(decl, type);
    cp_ast_add_child(decl, ident);
}

static void test_parse_lex_spec(void) {
    LexSpecResult result = cp_parse_lex_spec("samples/lex/minic.l");
    assert(result.base.ok);
    assert(result.data.rule_count >= 3);
    assert(result.data.rules[0].skip);
    assert(result.data.rules[0].priority == 0);
}

static void test_tokenize_source(void) {
    LexSpecResult spec = cp_parse_lex_spec("samples/lex/minic.l");
    TokenStreamResult result = cp_tokenize_source(&spec.data, "bool ok = !done && ready || 0;");
    assert(result.base.ok);
    assert(strcmp(result.data.tokens[0].kind, "BOOL") == 0);
    assert(strcmp(result.data.tokens[1].kind, "ID") == 0);
    assert(strcmp(result.data.tokens[1].lexeme, "ok") == 0);
    assert(strcmp(result.data.tokens[3].kind, "NOT") == 0);
    assert(strcmp(result.data.tokens[5].kind, "AND") == 0);
    assert(strcmp(result.data.tokens[7].kind, "OR") == 0);
    assert(strcmp(result.data.tokens[10].kind, "EOF") == 0);
}

static void test_keyword_longest_match(void) {
    LexSpecResult spec = cp_parse_lex_spec("samples/lex/minic.l");
    TokenStreamResult result = cp_tokenize_source(&spec.data, "int intx returnx return");
    assert(result.base.ok);
    assert(strcmp(result.data.tokens[0].kind, "INT") == 0);
    assert(strcmp(result.data.tokens[1].kind, "ID") == 0);
    assert(strcmp(result.data.tokens[1].lexeme, "intx") == 0);
    assert(strcmp(result.data.tokens[2].kind, "ID") == 0);
    assert(strcmp(result.data.tokens[2].lexeme, "returnx") == 0);
    assert(strcmp(result.data.tokens[3].kind, "RETURN") == 0);
}

static void test_token_positions_and_skip(void) {
    LexSpecResult spec = cp_parse_lex_spec("samples/lex/minic.l");
    TokenStreamResult result = cp_tokenize_source(&spec.data, "int\n  sum = 12;");
    assert(result.base.ok);
    assert(result.data.tokens[0].line == 1);
    assert(result.data.tokens[0].column == 1);
    assert(result.data.tokens[1].line == 2);
    assert(result.data.tokens[1].column == 3);
}

static void test_tokenize_error(void) {
    LexSpecResult spec = cp_parse_lex_spec("samples/lex/minic.l");
    TokenStreamResult result = cp_tokenize_source(&spec.data, "int @;");
    assert(!result.base.ok);
    assert(result.base.errors[0].code == 5001);
    assert(result.base.errors[0].line == 1);
    assert(result.base.errors[0].column == 5);
}

static void test_regex_validation_and_dfa(void) {
    LexSpecResult spec = cp_parse_lex_spec("samples/lex/minic.l");
    RegexResult bad = cp_parse_regex("[A-");
    DFAResult dfa = cp_build_dfa_from_lex_spec(&spec.data);
    assert(!bad.base.ok);
    assert(dfa.base.ok);
    assert(dfa.data.state_count > 0);
}

static void test_parse_yacc_spec(void) {
    GrammarResult result = cp_parse_yacc_spec("samples/yacc/minic.y");
    assert(result.base.ok);
    assert(strcmp(result.data.start_symbol, "program") == 0);
}

static void test_symbol_table(void) {
    SymbolTable table;
    SymbolEntry entry;
    cp_symbol_table_init(&table);
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.name, "sum");
    entry.category = SYMBOL_VARIABLE;
    cp_type_init_primitive(&entry.type_expr, TYPE_INT_T);
    assert(cp_symbol_table_insert(&table, &entry));
    assert(cp_symbol_table_enter_scope(&table, SCOPE_BLOCK, "inner") >= 0);
    assert(cp_symbol_table_lookup(&table, "sum") != NULL);
    cp_symbol_table_exit_scope(&table);
    assert(cp_symbol_table_lookup(&table, "sum") != NULL);
}

static void test_semantic_pipeline(void) {
    ASTNode program;
    ASTNode decl;
    ASTNode type;
    ASTNode ident;
    ASTNode literal;
    SymbolTableResult symbols;
    BoolResult type_ok;
    QuadrupleListResult ir;

    cp_ast_init(&program, AST_PROGRAM, "program", "", 1, 1);
    init_decl(&decl, &type, &ident, "int", "sum");
    cp_ast_init(&literal, AST_LITERAL, "literal", "12", 1, 10);
    cp_ast_add_child(&decl, &literal);
    cp_ast_add_child(&program, &decl);

    symbols = cp_run_semantic_actions(&program);
    assert(symbols.base.ok);
    assert(symbols.data.scopes[0].count == 1);
    assert(strcmp(symbols.data.scopes[0].entries[0].name, "sum") == 0);

    type_ok = cp_check_types(&program);
    assert(type_ok.base.ok);
    assert(type_ok.data == 1);

    ir = cp_build_ir(&program);
    assert(ir.base.ok);
    assert(ir.data.count == 1);
    assert(ir.data.items[0].op == IR_ASSIGN);
    assert(strcmp(ir.data.items[0].result, "sum") == 0);
}

static void test_full_pipeline_from_specs(void) {
    LexSpecResult lex = cp_parse_lex_spec("samples/lex/minic.l");
    GrammarResult grammar = cp_parse_yacc_spec("samples/yacc/minic.y");
    TokenStreamResult tokens = cp_tokenize_source(&lex.data, "int main() { int sum = 1 + 2; while (sum < 5 && !done(sum)) { sum = inc(sum); } if (sum >= 5 || sum == 0) { sum = sum - 1; } else { sum = 0; } return sum; } int inc(int x) { return x + 1; } int done(int x) { return x > 9; }");
    ASTNodeResult ast = cp_parse_tokens(&grammar.data, &tokens.data);
    SymbolTableResult symbols = cp_run_semantic_actions(&ast.data);
    BoolResult type_ok = cp_check_types(&ast.data);
    QuadrupleListResult ir = cp_build_ir(&ast.data);
    int saw_param = 0;
    int saw_call = 0;
    int index;
    assert(lex.base.ok);
    assert(grammar.base.ok);
    assert(tokens.base.ok);
    assert(ast.base.ok);
    assert(symbols.base.ok);
    assert(type_ok.base.ok);
    assert(ir.base.ok);
    assert(symbols.data.scopes[0].count == 3);
    assert(strcmp(symbols.data.scopes[0].entries[0].name, "main") == 0);
    assert(strcmp(symbols.data.scopes[0].entries[1].name, "inc") == 0);
    assert(strcmp(symbols.data.scopes[0].entries[2].name, "done") == 0);
    assert(ir.data.count >= 18);
    assert(ir.data.items[0].op == IR_FUNC_BEGIN);
    for (index = 0; index < ir.data.count; ++index) {
        saw_param |= ir.data.items[index].op == IR_PARAM;
        saw_call |= ir.data.items[index].op == IR_CALL;
    }
    assert(saw_param);
    assert(saw_call);
}

static void test_type_error_assignment(void) {
    ASTNode program;
    ASTNode decl;
    ASTNode type;
    ASTNode ident;
    ASTNode stmt;
    ASTNode assign;
    ASTNode lhs;
    ASTNode rhs;
    BoolResult type_ok;

    cp_ast_init(&program, AST_PROGRAM, "program", "", 1, 1);
    init_decl(&decl, &type, &ident, "int", "sum");
    cp_ast_add_child(&program, &decl);

    cp_ast_init(&stmt, AST_STATEMENT, "expr", "", 2, 1);
    cp_ast_init(&assign, AST_EXPRESSION, "assign", "=", 2, 1);
    cp_ast_init(&lhs, AST_IDENTIFIER, "identifier", "sum", 2, 1);
    cp_ast_init(&rhs, AST_LITERAL, "literal", "3.14", 2, 7);
    cp_ast_add_child(&assign, &lhs);
    cp_ast_add_child(&assign, &rhs);
    cp_ast_add_child(&stmt, &assign);
    cp_ast_add_child(&program, &stmt);

    type_ok = cp_check_types(&program);
    assert(!type_ok.base.ok);
    assert(type_ok.base.errors[0].code == 8001);
}

static void test_function_call_argument_type_error(void) {
    LexSpecResult lex = cp_parse_lex_spec("samples/lex/minic.l");
    GrammarResult grammar = cp_parse_yacc_spec("samples/yacc/minic.y");
    TokenStreamResult tokens = cp_tokenize_source(&lex.data, "int inc(int x) { return x; } float v; int main() { return inc(v); }");
    ASTNodeResult ast = cp_parse_tokens(&grammar.data, &tokens.data);
    BoolResult type_ok;
    assert(lex.base.ok);
    assert(grammar.base.ok);
    assert(tokens.base.ok);
    assert(ast.base.ok);
    type_ok = cp_check_types(&ast.data);
    assert(!type_ok.base.ok);
    assert(type_ok.base.errors[0].code == 8009);
}

static void test_forward_function_call(void) {
    LexSpecResult lex = cp_parse_lex_spec("samples/lex/minic.l");
    GrammarResult grammar = cp_parse_yacc_spec("samples/yacc/minic.y");
    TokenStreamResult tokens = cp_tokenize_source(&lex.data, "int main() { return inc(1); } int inc(int x) { return x + 1; }");
    ASTNodeResult ast = cp_parse_tokens(&grammar.data, &tokens.data);
    BoolResult type_ok;
    assert(lex.base.ok);
    assert(grammar.base.ok);
    assert(tokens.base.ok);
    assert(ast.base.ok);
    type_ok = cp_check_types(&ast.data);
    assert(type_ok.base.ok);
}

static void test_short_circuit_ir(void) {
    LexSpecResult lex = cp_parse_lex_spec("samples/lex/minic.l");
    GrammarResult grammar = cp_parse_yacc_spec("samples/yacc/minic.y");
    TokenStreamResult tokens = cp_tokenize_source(&lex.data, "int main() { int a = 0; int b = 1; if (a != 0 && b != 0 || !a) { return b; } return a; }");
    ASTNodeResult ast = cp_parse_tokens(&grammar.data, &tokens.data);
    BoolResult type_ok;
    QuadrupleListResult ir;
    int index;
    int saw_conditional = 0;
    int saw_goto = 0;
    assert(lex.base.ok);
    assert(grammar.base.ok);
    assert(tokens.base.ok);
    assert(ast.base.ok);
    type_ok = cp_check_types(&ast.data);
    assert(type_ok.base.ok);
    ir = cp_build_ir(&ast.data);
    assert(ir.base.ok);
    for (index = 0; index < ir.data.count; ++index) {
        saw_conditional |= ir.data.items[index].op == IR_IF_NE;
        saw_goto |= ir.data.items[index].op == IR_GOTO;
    }
    assert(saw_conditional);
    assert(saw_goto);
}

int main(void) {
    test_parse_lex_spec();
    test_tokenize_source();
    test_keyword_longest_match();
    test_token_positions_and_skip();
    test_tokenize_error();
    test_regex_validation_and_dfa();
    test_parse_yacc_spec();
    test_symbol_table();
    test_semantic_pipeline();
    test_full_pipeline_from_specs();
    test_type_error_assignment();
    test_function_call_argument_type_error();
    test_forward_function_call();
    test_short_circuit_ir();
    return 0;
}
