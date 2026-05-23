%token INT FLOAT VOID BOOL IF ELSE WHILE RETURN ID NUM ASSIGN OR AND NOT EQ NE LT LE GT GE PLUS MINUS MUL DIV LPAREN RPAREN LBRACE RBRACE COMMA SEMI
%right ASSIGN
%left OR
%left AND
%left EQ NE LT LE GT GE
%left PLUS MINUS
%left MUL DIV
%right NOT
%%
program: ext_list ;
ext_list: ext ext_list ;
ext_list: ext ;
ext: decl ;
ext: func_def ;
decl: type_spec ID ASSIGN expr SEMI ;
decl: type_spec ID SEMI ;
func_def: type_spec ID LPAREN param_list_opt RPAREN block ;
param_list_opt: param_list ;
param_list_opt: ;
param_list: param COMMA param_list ;
param_list: param ;
param: type_spec ID ;
type_spec: INT ;
type_spec: FLOAT ;
type_spec: VOID ;
block: LBRACE stmt_list RBRACE ;
stmt_list: stmt stmt_list ;
stmt_list: stmt ;
stmt: decl ;
stmt: RETURN expr SEMI ;
stmt: expr SEMI ;
expr: ID ASSIGN expr ;
expr: expr PLUS term ;
expr: expr MINUS term ;
expr: term ;
term: term MUL factor ;
term: term DIV factor ;
term: factor ;
factor: ID ;
factor: NUM ;
factor: ID LPAREN arg_list_opt RPAREN ;
factor: LPAREN expr RPAREN ;
arg_list_opt: arg_list ;
arg_list_opt: ;
arg_list: expr COMMA arg_list ;
arg_list: expr ;
stmt: block ;
stmt: IF LPAREN expr RPAREN stmt ;
stmt: IF LPAREN expr RPAREN stmt ELSE stmt ;
stmt: WHILE LPAREN expr RPAREN stmt ;
expr: expr EQ expr ;
expr: expr NE expr ;
expr: expr LT expr ;
expr: expr LE expr ;
expr: expr GT expr ;
expr: expr GE expr ;
expr: expr AND expr ;
expr: expr OR expr ;
expr: NOT expr ;
type_spec: BOOL ;
