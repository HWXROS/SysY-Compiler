%code requires {
  #include <memory>
  #include <string>
  #include "ast.h"
}

%{

#include <iostream>
#include "ast.h"

// 声明 lexer 函数和错误处理函数
int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);

using namespace std;

%}

// 定义 parser 函数和错误处理函数的附加参数
// 我们需要返回一个字符串作为 AST, 所以我们把附加参数定义成字符串的智能指针
// 解析完成后, 我们要手动修改这个参数, 把它设置成解析得到的字符串
%parse-param { std::unique_ptr<BaseAST> &ast }

// yylval 的定义, 我们把它定义成了一个联合体 (union)
// 因为 token 的值有的是字符串指针, 有的是整数
// 之前我们在 lexer 中用到的 str_val 和 int_val 就是在这里被定义的
// 至于为什么要用字符串指针而不直接用 string 或者 unique_ptr<string>?
// 请自行 STFW 在 union 里写一个带析构函数的类会出现什么情况
%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val;
  char char_val;
}

%token INT RETURN
%token EQ NE LE GE AND OR
%token <str_val> IDENT
%token <int_val> INT_CONST

%type <ast_val> FuncDef FuncType Block Stmt Exp PrimaryExp UnaryExp MulExp AddExp RelExp EqExp LAndExp LOrExp Number
%type <char_val> UnaryOp

%%

// CompUnit    ::= FuncDef;
CompUnit
  : FuncDef {
    auto comp_unit = make_unique<CompUnitAST>();
    comp_unit->func_def = unique_ptr<BaseAST>($1);
    ast = std::move(comp_unit);
  }
  ;

// FuncDef     ::= FuncType IDENT "(" ")" Block;
FuncDef
  : FuncType IDENT '(' ')' Block {
    auto ast = new FuncDefAST();
    ast->func_type = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<string>($2);
    ast->block = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  ;

// FuncType    ::= "int";
FuncType
  : INT {
    $$ = new FuncTypeAST();
  }
  ;

// Block       ::= "{" Stmt "}";
Block
  : '{' Stmt '}' {
    auto ast = new BlockAST();
    ast->stmt = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  ;
   
// Stmt        ::= "return" Exp ";";
Stmt
  : RETURN Exp ';' {
    auto ast = new StmtAST();
    ast->exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  ;
// Exp         ::= LOrExp;
Exp
  : LOrExp {
    $$ = $1;
  }
  ;

// LOrExp      ::= LAndExp | LOrExp "||" LAndExp;
LOrExp
  : LAndExp {
    $$ = $1;
  }
  | LOrExp OR LAndExp {
    auto ast = new BinaryExprAST();
    ast->op = '|';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// LAndExp     ::= EqExp | LAndExp "&&" EqExp;
LAndExp
  : EqExp {
    $$ = $1;
  }
  | LAndExp AND EqExp {
    auto ast = new BinaryExprAST();
    ast->op = '&';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// EqExp       ::= RelExp | EqExp ("==" | "!=") RelExp;
EqExp
  : RelExp {
    $$ = $1;
  }
  | EqExp EQ RelExp {
    auto ast = new BinaryExprAST();
    ast->op = 'E';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | EqExp NE RelExp {
    auto ast = new BinaryExprAST();
    ast->op = 'N';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// RelExp      ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp;
RelExp
  : AddExp {
    $$ = $1;
  }
  | RelExp '<' AddExp {
    auto ast = new BinaryExprAST();
    ast->op = '<';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | RelExp '>' AddExp {
    auto ast = new BinaryExprAST();
    ast->op = '>';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | RelExp LE AddExp {
    auto ast = new BinaryExprAST();
    ast->op = 'L';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | RelExp GE AddExp {
    auto ast = new BinaryExprAST();
    ast->op = 'G';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// AddExp      ::= MulExp | AddExp ("+" | "-") MulExp;
AddExp
  : MulExp {
    $$ = $1;
  }
  | AddExp '+' MulExp {
    auto ast = new BinaryExprAST();
    ast->op = '+';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | AddExp '-' MulExp {
    auto ast = new BinaryExprAST();
    ast->op = '-';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// MulExp      ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp;
MulExp
  : UnaryExp {
    $$ = $1;
  }
  | MulExp '*' UnaryExp {
    auto ast = new BinaryExprAST();
    ast->op = '*';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | MulExp '/' UnaryExp {
    auto ast = new BinaryExprAST();
    ast->op = '/';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | MulExp '%' UnaryExp {
    auto ast = new BinaryExprAST();
    ast->op = '%';
    ast->left = unique_ptr<BaseAST>($1);
    ast->right = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

// UnaryOp     ::= "+" | "-" | "!";
UnaryOp
  : '+' {
    $$ = '+';
  }
  | '-' {
    $$ = '-';
  }
  | '!' {
    $$ = '!';
  }
  ;

// UnaryExp    ::= PrimaryExp | UnaryOp UnaryExp;
UnaryExp
  : PrimaryExp {
    $$ = $1;
  }
  | UnaryOp UnaryExp {
    auto ast = new UnaryExprAST();
    ast->op = $1;
    ast->exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  ;

// PrimaryExp  ::= "(" Exp ")" | Number;
PrimaryExp
  : '(' Exp ')' {
    $$ = $2;
  }
  | Number {
    $$ = $1;
  }
  ;

// Number      ::= INT_CONST;
Number
  : INT_CONST {
    $$ = new NumberAST($1);
  }
  ;



%%

void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
  cerr << "error: " << s << endl;
}
