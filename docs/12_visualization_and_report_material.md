# 可视化与报告素材

## 当前可生成材料

统一流水线命令：

```bash
./build/compiler_project --lex samples/lex/minic.l --yacc samples/yacc/minic.y --input samples/src/ok/demo.mc --out output
```

输出目录包含：

- `output/token_stream/tokens.txt`：Token 序列。
- `output/automata/nfa.txt`：合并 NFA 文本。
- `output/automata/dfa.txt`：DFA 文本。
- `output/automata/min_dfa.txt`：最小化 DFA 文本。
- `output/ast/ast.txt`：AST 层次文本。
- `output/symbol_table/scopes.txt`：作用域与符号表。
- `output/ir/quads.txt`：四元式。
- `output/log/parse_steps.txt`：LR 移进/规约步骤。
- `output/log/grammar.txt`：产生式列表。

## 本轮新增演示点

- 短路布尔：`&& / || / !` 已进入词法、文法、AST、类型检查和 IR。
- 函数前向调用：类型检查前预扫描全局函数签名，因此 `main` 可以调用后面定义的 `inc`。
- 错误样例：`samples/src/err` 下补充语法错误、未声明标识符、函数实参类型错误。

## 短路布尔 IR 说明

条件位置不会把 `&& / ||` 当普通二元算术计算，而是生成跳转：

- `a && b`：先判断 `a`，为假直接跳到 false 标签，为真再判断 `b`。
- `a || b`：先判断 `a`，为真直接跳到 true 标签，为假再判断 `b`。
- `!a`：交换 true / false 标签。

这可以在报告中作为“语法制导翻译和控制流四元式”的重点展示。

## 错误恢复现状

当前 parser 已输出 `unexpected token X, expected: ...`，属于诊断增强；panic-mode 同步恢复仍保留为后续工作。报告中建议明确写为：本项目采用立即失败策略保证 AST 和语义阶段输入可靠，并在错误消息中列出当前状态可接受 token，后续可按 `SEMI/RBRACE/EOF` 做同步点恢复。
