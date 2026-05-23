#include "compiler_project/yacc/lr_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_project/common/error.h"

#define CP_AUGMENTED_PRODUCTION_ID 0
#define CP_EPSILON_SYMBOL "__EPSILON__"
#define CP_MAX_ITEMS_PER_STATE 1024
#define CP_MAX_AUGMENTED_PRODUCTIONS (CP_MAX_PRODUCTIONS + 1)

typedef struct {
    int production_id;
    int dot;
    char lookahead[32];
} LR1Item;

typedef struct {
    LR1Item items[CP_MAX_ITEMS_PER_STATE];
    int count;
} LR1ItemSet;

typedef struct {
    Production productions[CP_MAX_AUGMENTED_PRODUCTIONS];
    int count;
    char start_symbol[64];
} AugmentedGrammar;

static int is_terminal(const Grammar *grammar, const char *symbol) {
    int i;
    if (symbol == NULL) {
        return 0;
    }
    if (strcmp(symbol, "EOF") == 0) {
        return 1;
    }
    for (i = 0; grammar != NULL && i < grammar->token_count; ++i) {
        if (strcmp(grammar->tokens[i], symbol) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_non_terminal(const Grammar *grammar, const char *symbol) {
    int i;
    if (grammar == NULL || symbol == NULL) {
        return 0;
    }
    for (i = 0; i < grammar->non_terminal_count; ++i) {
        if (strcmp(grammar->non_terminals[i], symbol) == 0) {
            return 1;
        }
    }
    return 0;
}

static const GrammarPrecedence *find_precedence(const Grammar *grammar, const char *symbol) {
    int i;
    if (grammar == NULL || symbol == NULL) {
        return NULL;
    }
    for (i = 0; i < grammar->precedence_count; ++i) {
        if (strcmp(grammar->precedence[i].symbol, symbol) == 0) {
            return &grammar->precedence[i];
        }
    }
    return NULL;
}

static const GrammarPrecedence *find_production_precedence(const Grammar *grammar, const Production *production) {
    int i;
    if (grammar == NULL || production == NULL) {
        return NULL;
    }
    for (i = production->rhs_count - 1; i >= 0; --i) {
        const GrammarPrecedence *precedence = find_precedence(grammar, production->rhs[i]);
        if (precedence != NULL) {
            return precedence;
        }
    }
    return NULL;
}

static void init_lr_table(LRTable *table) {
    int i;
    int j;
    memset(table, 0, sizeof(*table));
    for (i = 0; i < CP_MAX_LR_STATES; ++i) {
        for (j = 0; j < CP_MAX_GRAMMAR_SYMBOLS; ++j) {
            table->go_to[i][j] = -1;
        }
    }
}

static void build_augmented_grammar(const Grammar *grammar, AugmentedGrammar *augmented) {
    int i;
    memset(augmented, 0, sizeof(*augmented));
    augmented->count = grammar == NULL ? 0 : grammar->production_count + 1;
    snprintf(augmented->start_symbol, sizeof(augmented->start_symbol), "%s'", grammar == NULL ? "program" : grammar->start_symbol);
    augmented->productions[0].id = CP_AUGMENTED_PRODUCTION_ID;
    snprintf(augmented->productions[0].lhs, sizeof(augmented->productions[0].lhs), "%s", augmented->start_symbol);
    augmented->productions[0].rhs_count = 1;
    snprintf(augmented->productions[0].rhs[0], sizeof(augmented->productions[0].rhs[0]), "%s", grammar == NULL ? "program" : grammar->start_symbol);
    for (i = 0; grammar != NULL && i < grammar->production_count; ++i) {
        augmented->productions[i + 1] = grammar->productions[i];
    }
}

static const Production *find_augmented_production(const AugmentedGrammar *augmented, int production_id) {
    int i;
    if (augmented == NULL) {
        return NULL;
    }
    for (i = 0; i < augmented->count; ++i) {
        if (augmented->productions[i].id == production_id) {
            return &augmented->productions[i];
        }
    }
    return NULL;
}

static const Production *find_grammar_production(const Grammar *grammar, int production_id) {
    int i;
    if (grammar == NULL) {
        return NULL;
    }
    for (i = 0; i < grammar->production_count; ++i) {
        if (grammar->productions[i].id == production_id) {
            return &grammar->productions[i];
        }
    }
    return NULL;
}

static int item_exists(const LR1ItemSet *set, int production_id, int dot, const char *lookahead) {
    int i;
    for (i = 0; i < set->count; ++i) {
        if (set->items[i].production_id == production_id
            && set->items[i].dot == dot
            && strcmp(set->items[i].lookahead, lookahead) == 0) {
            return 1;
        }
    }
    return 0;
}

static int add_item(LR1ItemSet *set, int production_id, int dot, const char *lookahead) {
    if (set == NULL || lookahead == NULL || set->count >= CP_MAX_ITEMS_PER_STATE || item_exists(set, production_id, dot, lookahead)) {
        return 0;
    }
    set->items[set->count].production_id = production_id;
    set->items[set->count].dot = dot;
    snprintf(set->items[set->count].lookahead, sizeof(set->items[set->count].lookahead), "%s", lookahead);
    set->count++;
    return 1;
}

static int first_set_contains_epsilon(const GrammarSymbolSet *set) {
    int i;
    if (set == NULL) {
        return 0;
    }
    for (i = 0; i < set->count; ++i) {
        if (strcmp(set->items[i], CP_EPSILON_SYMBOL) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_first_sequence(
    const Grammar *grammar,
    const GrammarAnalysis *analysis,
    const Production *production,
    int start_index,
    const char *lookahead,
    GrammarSymbolSet *out
) {
    int i;
    int nullable_prefix = 1;
    memset(out, 0, sizeof(*out));
    for (i = start_index; nullable_prefix && production != NULL && i < production->rhs_count; ++i) {
        const char *symbol = production->rhs[i];
        nullable_prefix = 0;
        if (is_terminal(grammar, symbol)) {
            snprintf(out->items[out->count++], sizeof(out->items[0]), "%s", symbol);
            return;
        }
        {
            int symbol_index = cp_grammar_find_symbol(&analysis->symbols, symbol);
            int item_index;
            if (symbol_index < 0) {
                return;
            }
            for (item_index = 0; item_index < analysis->first[symbol_index].count && out->count < CP_MAX_SYMBOL_SET_ITEMS; ++item_index) {
                const char *item = analysis->first[symbol_index].items[item_index];
                if (strcmp(item, CP_EPSILON_SYMBOL) == 0) {
                    nullable_prefix = 1;
                } else if (!cp_first_set_contains(out, item)) {
                    snprintf(out->items[out->count++], sizeof(out->items[0]), "%s", item);
                }
            }
            if (first_set_contains_epsilon(&analysis->first[symbol_index])) {
                nullable_prefix = 1;
            }
        }
    }
    if (nullable_prefix && out->count < CP_MAX_SYMBOL_SET_ITEMS && !cp_first_set_contains(out, lookahead)) {
        snprintf(out->items[out->count++], sizeof(out->items[0]), "%s", lookahead);
    }
}

static void closure_lr1(const Grammar *grammar, const GrammarAnalysis *analysis, const AugmentedGrammar *augmented, LR1ItemSet *set) {
    int changed = 1;
    while (changed) {
        int i;
        changed = 0;
        for (i = 0; i < set->count; ++i) {
            const Production *production = find_augmented_production(augmented, set->items[i].production_id);
            if (production != NULL && set->items[i].dot < production->rhs_count) {
                const char *symbol = production->rhs[set->items[i].dot];
                int prod_index;
                if (strcmp(symbol, augmented->start_symbol) != 0 && !is_non_terminal(grammar, symbol)) {
                    continue;
                }
                for (prod_index = 0; prod_index < augmented->count; ++prod_index) {
                    if (strcmp(augmented->productions[prod_index].lhs, symbol) == 0) {
                        GrammarSymbolSet lookaheads;
                        int la_index;
                        add_first_sequence(grammar, analysis, production, set->items[i].dot + 1, set->items[i].lookahead, &lookaheads);
                        for (la_index = 0; la_index < lookaheads.count; ++la_index) {
                            changed |= add_item(set, augmented->productions[prod_index].id, 0, lookaheads.items[la_index]);
                        }
                    }
                }
            }
        }
    }
}

static void goto_lr1(
    const Grammar *grammar,
    const GrammarAnalysis *analysis,
    const AugmentedGrammar *augmented,
    const LR1ItemSet *from,
    const char *symbol,
    LR1ItemSet *out
) {
    int i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < from->count; ++i) {
        const Production *production = find_augmented_production(augmented, from->items[i].production_id);
        if (production != NULL
            && from->items[i].dot < production->rhs_count
            && strcmp(production->rhs[from->items[i].dot], symbol) == 0) {
            add_item(out, from->items[i].production_id, from->items[i].dot + 1, from->items[i].lookahead);
        }
    }
    closure_lr1(grammar, analysis, augmented, out);
}

static int item_compare(const void *left, const void *right) {
    const LR1Item *lhs = (const LR1Item *) left;
    const LR1Item *rhs = (const LR1Item *) right;
    if (lhs->production_id != rhs->production_id) {
        return lhs->production_id - rhs->production_id;
    }
    if (lhs->dot != rhs->dot) {
        return lhs->dot - rhs->dot;
    }
    return strcmp(lhs->lookahead, rhs->lookahead);
}

static void normalize_item_set(LR1ItemSet *set) {
    qsort(set->items, (size_t) set->count, sizeof(set->items[0]), item_compare);
}

static int same_item_set(const LR1ItemSet *left, const LR1ItemSet *right) {
    if (left->count != right->count) {
        return 0;
    }
    return memcmp(left->items, right->items, (size_t) left->count * sizeof(left->items[0])) == 0;
}

static int find_state(const LR1ItemSet *states, int state_count, const LR1ItemSet *needle) {
    int i;
    for (i = 0; i < state_count; ++i) {
        if (same_item_set(&states[i], needle)) {
            return i;
        }
    }
    return -1;
}

static int core_item_exists(const LR1ItemSet *set, int production_id, int dot) {
    int i;
    for (i = 0; i < set->count; ++i) {
        if (set->items[i].production_id == production_id && set->items[i].dot == dot) {
            return 1;
        }
    }
    return 0;
}

static int core_count(const LR1ItemSet *set) {
    int i;
    int count = 0;
    for (i = 0; i < set->count; ++i) {
        int seen = 0;
        int j;
        for (j = 0; j < i; ++j) {
            if (set->items[j].production_id == set->items[i].production_id && set->items[j].dot == set->items[i].dot) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            count++;
        }
    }
    return count;
}

static int same_core(const LR1ItemSet *left, const LR1ItemSet *right) {
    int i;
    if (core_count(left) != core_count(right)) {
        return 0;
    }
    for (i = 0; i < left->count; ++i) {
        if (!core_item_exists(right, left->items[i].production_id, left->items[i].dot)) {
            return 0;
        }
    }
    return 1;
}

static int merge_item_set(LR1ItemSet *target, const LR1ItemSet *source) {
    int i;
    int changed = 0;
    for (i = 0; i < source->count; ++i) {
        changed |= add_item(target, source->items[i].production_id, source->items[i].dot, source->items[i].lookahead);
    }
    normalize_item_set(target);
    return changed;
}

static int find_lalr_state_by_core(const LR1ItemSet *states, int state_count, const LR1ItemSet *needle) {
    int i;
    for (i = 0; i < state_count; ++i) {
        if (same_core(&states[i], needle)) {
            return i;
        }
    }
    return -1;
}

static int set_action(LRTableResult *result, const Grammar *grammar, LRTable *table, int state, int symbol_index, LRActionKind kind, int target) {
    LRAction *slot = &table->action[state][symbol_index];
    if (slot->kind != CP_ACT_ERROR && (slot->kind != kind || slot->target != target)) {
        LRAction resolved = *slot;
        const char *symbol_name = table->symbols.names[symbol_index];
        const Production *reduce_production = NULL;
        const GrammarPrecedence *token_precedence = find_precedence(grammar, symbol_name);
        const GrammarPrecedence *production_precedence = NULL;
        int existing_is_shift_reduce = (slot->kind == CP_ACT_SHIFT && kind == CP_ACT_REDUCE)
            || (slot->kind == CP_ACT_REDUCE && kind == CP_ACT_SHIFT);

        if (existing_is_shift_reduce) {
            int reduce_target = slot->kind == CP_ACT_REDUCE ? slot->target : target;
            reduce_production = find_grammar_production(grammar, reduce_target);
        }

        if (existing_is_shift_reduce && reduce_production != NULL) {
            production_precedence = find_production_precedence(grammar, reduce_production);
            if (token_precedence != NULL && production_precedence != NULL) {
                if (token_precedence->level > production_precedence->level) {
                    resolved.kind = CP_ACT_SHIFT;
                    resolved.target = slot->kind == CP_ACT_SHIFT ? slot->target : target;
                } else if (token_precedence->level < production_precedence->level) {
                    resolved.kind = CP_ACT_REDUCE;
                    resolved.target = slot->kind == CP_ACT_REDUCE ? slot->target : target;
                } else if (strcmp(token_precedence->assoc, "left") == 0) {
                    resolved.kind = CP_ACT_REDUCE;
                    resolved.target = slot->kind == CP_ACT_REDUCE ? slot->target : target;
                } else if (strcmp(token_precedence->assoc, "right") == 0) {
                    resolved.kind = CP_ACT_SHIFT;
                    resolved.target = slot->kind == CP_ACT_SHIFT ? slot->target : target;
                } else {
                    resolved.kind = CP_ACT_ERROR;
                    resolved.target = 0;
                }
                *slot = resolved;
                return 1;
            }
            if (token_precedence == NULL && production_precedence == NULL) {
                resolved.kind = CP_ACT_SHIFT;
                resolved.target = slot->kind == CP_ACT_SHIFT ? slot->target : target;
                *slot = resolved;
                return 1;
            }
        }
        {
            CompilerError error;
            cp_make_error(&error, 4003, "LALR action conflict", -1, -1, "yacc");
            RESULT_FAILURE(&result->base, error);
            return 0;
        }
    }
    slot->kind = kind;
    slot->target = target;
    return 1;
}

LRTableResult cp_build_lr_table(const Grammar *grammar) {
    static LRTableResult result;
    static GrammarAnalysisResult analysis;
    static AugmentedGrammar augmented;
    static LR1ItemSet lr1_states[CP_MAX_LR_STATES];
    static LR1ItemSet lalr_states[CP_MAX_LR_STATES];
    static LR1ItemSet moved;
    static int lr1_transitions[CP_MAX_LR_STATES][CP_MAX_GRAMMAR_SYMBOLS];
    static int lr1_to_lalr[CP_MAX_LR_STATES];
    int lr1_state_count = 0;
    int lalr_state_count = 0;
    int processed = 0;
    int i;
    int j;

    RESULT_SUCCESS(&result.base);
    init_lr_table(&result.data);
    memset(lr1_states, 0, sizeof(lr1_states));
    memset(lalr_states, 0, sizeof(lalr_states));
    for (i = 0; i < CP_MAX_LR_STATES; ++i) {
        lr1_to_lalr[i] = -1;
        for (j = 0; j < CP_MAX_GRAMMAR_SYMBOLS; ++j) {
            lr1_transitions[i][j] = -1;
        }
    }

    if (grammar == NULL) {
        CompilerError error;
        cp_make_error(&error, 4002, "missing grammar for lr table build", -1, -1, "yacc");
        RESULT_FAILURE(&result.base, error);
        return result;
    }

    analysis = cp_analyze_grammar(grammar);
    if (!analysis.base.ok) {
        RESULT_FAILURE(&result.base, analysis.base.errors[0]);
        return result;
    }
    result.data.symbols = analysis.data.symbols;

    build_augmented_grammar(grammar, &augmented);
    add_item(&lr1_states[0], CP_AUGMENTED_PRODUCTION_ID, 0, "EOF");
    closure_lr1(grammar, &analysis.data, &augmented, &lr1_states[0]);
    normalize_item_set(&lr1_states[0]);
    lr1_state_count = 1;

    while (processed < lr1_state_count && result.base.ok) {
        int symbol_index;
        for (symbol_index = 0; symbol_index < result.data.symbols.count; ++symbol_index) {
            int target_state;
            const char *symbol = result.data.symbols.names[symbol_index];
            memset(&moved, 0, sizeof(moved));
            goto_lr1(grammar, &analysis.data, &augmented, &lr1_states[processed], symbol, &moved);
            if (moved.count == 0) {
                continue;
            }
            normalize_item_set(&moved);
            target_state = find_state(lr1_states, lr1_state_count, &moved);
            if (target_state < 0) {
                if (lr1_state_count >= CP_MAX_LR_STATES) {
                    CompilerError error;
                    cp_make_error(&error, 4004, "too many LR(1) states", -1, -1, "yacc");
                    RESULT_FAILURE(&result.base, error);
                    break;
                }
                lr1_states[lr1_state_count] = moved;
                target_state = lr1_state_count++;
            }
            lr1_transitions[processed][symbol_index] = target_state;
        }
        processed++;
    }

    for (i = 0; i < lr1_state_count && result.base.ok; ++i) {
        int lalr_index = find_lalr_state_by_core(lalr_states, lalr_state_count, &lr1_states[i]);
        if (lalr_index < 0) {
            if (lalr_state_count >= CP_MAX_LR_STATES) {
                CompilerError error;
                cp_make_error(&error, 4005, "too many LALR states", -1, -1, "yacc");
                RESULT_FAILURE(&result.base, error);
                break;
            }
            lalr_index = lalr_state_count++;
            lalr_states[lalr_index] = lr1_states[i];
        } else {
            merge_item_set(&lalr_states[lalr_index], &lr1_states[i]);
        }
        lr1_to_lalr[i] = lalr_index;
    }

    for (i = 0; i < lr1_state_count && result.base.ok; ++i) {
        int from = lr1_to_lalr[i];
        for (j = 0; j < result.data.symbols.count; ++j) {
            int target = lr1_transitions[i][j];
            int to;
            if (target < 0) {
                continue;
            }
            to = lr1_to_lalr[target];
            if (is_terminal(grammar, result.data.symbols.names[j])) {
                if (!set_action(&result, grammar, &result.data, from, j, CP_ACT_SHIFT, to)) {
                    break;
                }
            } else {
                result.data.go_to[from][j] = to;
            }
        }
    }

    for (i = 0; i < lalr_state_count && result.base.ok; ++i) {
        int item_index;
        snprintf(result.data.states[i], sizeof(result.data.states[i]), "I%d", i);
        for (item_index = 0; item_index < lalr_states[i].count; ++item_index) {
            const Production *production = find_augmented_production(&augmented, lalr_states[i].items[item_index].production_id);
            int symbol_index;
            if (production == NULL || lalr_states[i].items[item_index].dot != production->rhs_count) {
                continue;
            }
            symbol_index = cp_grammar_find_symbol(&result.data.symbols, lalr_states[i].items[item_index].lookahead);
            if (symbol_index < 0) {
                continue;
            }
            if (production->id == CP_AUGMENTED_PRODUCTION_ID) {
                if (!set_action(&result, grammar, &result.data, i, symbol_index, CP_ACT_ACCEPT, 0)) {
                    break;
                }
            } else if (!set_action(&result, grammar, &result.data, i, symbol_index, CP_ACT_REDUCE, production->id)) {
                break;
            }
        }
    }

    result.data.state_count = lalr_state_count;
    return result;
}
