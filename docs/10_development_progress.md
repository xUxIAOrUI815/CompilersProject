# 开发进度与后续目标

更新时间：2026-05-17

## 当前状态

本项目已经从最小骨架推进到可跑通 `Lex -> Yacc -> LALR Table -> AST -> Semantic -> IR` 的集成链路。

已验证主流程：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/compiler_project parse --lex-spec samples/lex/minic.l --yacc-spec samples/yacc/minic.y --source samples/src/ok/demo.mc
./build/compiler_project ir --lex-spec samples/lex/minic.l --yacc-spec samples/yacc/minic.y --source samples/src/ok/demo.mc
```

当前测试结果：

```text
2/2 tests passed
```

## A：词法模块进度

已完成：

- `.l` 规则读取。
- 正则校验。
- NFA 构造、DFA 确定化、DFA 最小化。
- 最长匹配和规则优先级。
- TokenStream 输出，含 `kind / lexeme / line / column / attr`。
- 支持关键字、标识符、整数、算术运算符、赋值、比较运算符、括号、大括号、逗号、分号。
- 支持 `if / else / while / return / int / float / void`。

当前样例文件：

- `samples/lex/minic.l`

## B：Yacc 与语法模块进度

已完成：

- `.y` 文件中 `%token`、`%left / %right / %nonassoc`、产生式规则读取。
- 产生式编号、语义动作原文保留字段。
- FIRST/FOLLOW 集计算。
- LR(1) 项目集族构造，item 带 lookahead。
- 基于 `FIRST(beta lookahead)` 的 LR(1) closure/goto。
- LALR 同 core 状态合并，合并 lookahead。
- 基于 LALR lookahead 的 action/goto 表构造。
- 基于优先级和结合性的 shift/reduce 冲突消解。
- 表驱动 parser：维护状态栈和值栈，执行 `shift / reduce / accept`。
- 归约时构造 AST。
- 语法分析 trace 输出：状态栈、符号栈、lookahead、action。
- 语法错误诊断：输出当前 lookahead 和 expected token 列表。

当前支持的主要语法：

- 全局变量声明。
- 函数定义。
- 形参列表。
- 代码块。
- 变量声明与初始化。
- 赋值表达式。
- 加减乘除。
- 函数调用节点。
- `return`。
- `if / if-else`。
- `while`。
- `== / != / < / <= / > / >=` 比较表达式。

当前样例文件：

- `samples/yacc/minic.y`

说明：

- 当前分析表已经从 `SLR + 优先级消解` 升级为 `LR(1) canonical collection -> LALR merge -> precedence conflict handling`。
- `if-else` 悬挂问题当前使用常见的 shift 优先策略，使 `else` 绑定最近的 `if`。

## C：语义与 IR 模块进度

已完成：

- 多作用域符号表。
- 函数、参数、变量入表。
- 同作用域重定义检查。
- 基本类型表达式。
- 基本偏移量计算。
- 表达式类型综合。
- 赋值类型检查。
- 算术表达式类型检查。
- 比较表达式类型检查。
- `return` 类型检查。
- `if / while` 条件类型检查。
- 四元式生成。
- 临时变量生成。
- 标签生成。
- 条件跳转 IR。

当前 IR 支持：

- `ADD / SUB / MUL / DIV`
- `ASSIGN`
- `RET`
- `FUNC_BEGIN / FUNC_END`
- `PARAM / CALL`
- `LABEL`
- `GOTO`
- `IF_LT / IF_LE / IF_GT / IF_GE / IF_EQ / IF_NE`

`samples/src/ok/demo.mc` 当前可以生成类似：

```text
(100) (ADD, 1, 2, T1)
(101) (ASSIGN, T1, _, sum)
(102) (LABEL, _, _, L1)
(103) (IF_LT, sum, 5, L2)
(104) (GOTO, _, _, L3)
...
```

## 输出与联调

统一入口已支持：

```bash
./build/compiler_project --lex samples/lex/minic.l --yacc samples/yacc/minic.y --input samples/src/ok/demo.mc --out output
```

输出目录约定：

- `output/token_stream/tokens.txt`
- `output/automata/nfa.txt`
- `output/automata/dfa.txt`
- `output/automata/min_dfa.txt`
- `output/ast/ast.txt`
- `output/symbol_table/scopes.txt`
- `output/ir/quads.txt`
- `output/log/parse_steps.txt`
- `output/log/grammar.txt`

## 本轮完成内容

上一轮重点完成：

- 扩展词法 token：`IF / ELSE / WHILE / EQ / NE / LT / LE / GT / GE`。
- 扩展文法：`if`、`if-else`、`while`、block statement、比较表达式。
- 扩展 parser 归约 AST 映射：生成 `if / while / eq / ne / lt / le / gt / ge` 节点。
- 扩展类型检查：比较表达式返回 `bool`，条件表达式要求可判真值。
- 扩展 IR：为条件和循环生成 `LABEL / GOTO / IF_*`。
- 补充测试：控制流程序可以通过表驱动 parser、语义检查和 IR 生成。

本轮重点完成：

- 将 LR 构造器升级为 LR(1) item，item 结构包含 `production_id / dot / lookahead`。
- 实现 LR(1) closure/goto，并使用 `FIRST(beta lookahead)` 传播展望符。
- 实现 LALR 同 core 合并，合并 LR(1) lookahead 后填写 reduce action。
- 保留并复用优先级/结合性冲突消解逻辑，覆盖算术表达式和 dangling else 的 shift/reduce 场景。
- 将 parser trace 扩展为结构化步骤：状态栈、符号栈、lookahead、action。
- 语法错误信息扩展为 `unexpected token X, expected: ...`。
- 修复 LR 表变大后统一流水线中的栈溢出问题：大型 LR 构造结果和 parser 内 LR 表改为静态存储。
- 更新测试：验证控制流程序、trace 字段、expected token 诊断。
- 增加函数定义 IR：生成 `FUNC_BEGIN / FUNC_END`。
- 增加函数调用 IR：按实参顺序生成 `PARAM`，再生成 `CALL name argc result`。
- 扩展函数调用类型检查：从“只检查参数数量”升级为“参数数量 + 参数类型”。
- 更新 `samples/src/ok/demo.mc`：加入 `inc(int x)` 与 `inc(sum)` 调用，作为当前综合演示样例。

## 本轮补充进展

- 已增加短路布尔：词法支持 `&& / || / !`，文法新增逻辑表达式，AST 归约生成 `and / or / not` 节点。
- 类型检查中逻辑表达式返回 `bool`，并要求操作数可判真值。
- IR builder 在条件位置为 `&& / || / !` 生成短路跳转，而不是普通二元算术四元式。
- 类型检查前预扫描全局函数签名，支持函数在定义前被调用。
- `samples/src/err` 补充语法错误、未声明标识符、函数实参类型错误样例。
- 新增 `docs/12_visualization_and_report_material.md`，整理统一流水线可输出的可视化/报告素材和当前错误恢复策略说明。

## 剩余目标

优先级高：

- 增加 `while/if` 的更多嵌套测试和错误样例测试。
- 明确禁止或处理 `void` 变量声明，目前类型系统可识别但语言约束还不完整。

优先级中：

- 为 LR/LALR 表输出增加更完整的项目集内容，而不仅是 action/goto。
- 增加 `else` 悬挂问题的文法消歧说明到报告材料。
- 增加数组下标、指针、结构体等类型表达式。

优先级低：

- 执行 `.y` 中 `{ ... }` 语义动作，而不仅仅保留原文。
- 更完整的错误恢复。
- 自动机和分析表可视化。
- 更完整的报告素材与演示脚本。

## 建议的下一步

下一轮建议直接做短路布尔：

1. 扩展词法和文法：加入 `&& / || / !`。
2. 在 parser 归约中生成 `and / or / not` AST 节点。
3. 在类型检查中约束布尔表达式操作数。
4. 在 IR builder 中生成短路跳转，不把 `&& / ||` 简化成普通算术表达式。
5. 增加嵌套 `if/while`、函数调用参数错误、布尔短路的综合样例。

## 完整目标达成判断

按当前文档目标，达到“完整目标要求”至少需要完成：

1. A 模块：Lex 输入解析、正则到 NFA/DFA/MinDFA、TokenStream、自动机输出。当前基本完成。
2. B 模块：Yacc 输入解析、FIRST/FOLLOW、LR(1)、LALR、表驱动 parser、AST、规约/步骤日志。当前基本完成。
3. C 模块：多作用域符号表、类型表达式、声明/函数/参数入表、类型检查、四元式、统一 CLI、输出目录。当前主链路完成。
4. 剩余增强：短路布尔、更多错误样例、函数前向调用、错误恢复、可视化/报告素材。完成这些后更接近“完整版”和答辩展示标准。

因此，若以课程项目“完整链路可演示”为标准，当前已经接近达标；若以文档中所有拓展项都实现为标准，还需要完成短路布尔、函数前向声明、错误恢复、数组/指针/结构体等增强项。
