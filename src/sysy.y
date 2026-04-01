%code requires {
  #include <memory>
  #include <string>
  #include <vector>
  #include "ast.h"
}

%{

#include <iostream>
#include "ast.h"

int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);

using namespace std;

%}

%parse-param { std::unique_ptr<BaseAST> &ast }

%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val;
  char char_val;
  std::vector<BaseAST*> *ast_list;
}

%token CONST INT RETURN IF ELSE
%token EQ NE LE GE AND OR
%token <str_val> IDENT
%token <int_val> INT_CONST

%type <ast_val> FuncDef FuncType Block Stmt Exp PrimaryExp UnaryExp MulExp AddExp RelExp EqExp LAndExp LOrExp Number
%type <ast_val> Decl ConstDecl VarDecl ConstDef VarDef ConstInitVal InitVal LVal ConstExp BType
%type <char_val> UnaryOp
%type <ast_list> BlockItem ConstDefList VarDefList

%%

CompUnit
  : FuncDef {
    auto comp_unit = make_unique<CompUnitAST>();
    comp_unit->func_def = unique_ptr<BaseAST>($1);
    ast = std::move(comp_unit);
  }
  ;

FuncDef
  : FuncType IDENT '(' ')' Block {
    auto ast = new FuncDefAST();
    ast->func_type = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<string>($2);
    ast->block = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  ;

FuncType
  : INT {
    $$ = new FuncTypeAST();
  }
  ;

Block
  : '{' '}' {
    auto ast = new BlockAST();
    $$ = ast;
  }
  | '{' BlockItem '}' {
    auto ast = new BlockAST();
    for (auto item : *$2) {
      ast->items.push_back(unique_ptr<BaseAST>(item));
    }
    delete $2;
    $$ = ast;
  }
  ;

BlockItem
  : Decl {
    $$ = new vector<BaseAST*>();
    $$->push_back($1);
  }
  | Stmt {
    $$ = new vector<BaseAST*>();
    $$->push_back($1);
  }
  | BlockItem Decl {
    $1->push_back($2);
    $$ = $1;
  }
  | BlockItem Stmt {
    $1->push_back($2);
    $$ = $1;
  }
  ;

Decl
  : ConstDecl {
    $$ = $1;
  }
  | VarDecl {
    $$ = $1;
  }
  ;

ConstDecl
  : CONST BType ConstDefList ';' {
    auto ast = new ConstDeclAST();
    for (auto def : *$3) {
      ast->const_defs.push_back(unique_ptr<BaseAST>(def));
    }
    delete $3;
    $$ = ast;
  }
  ;

ConstDefList
  : ConstDef {
    $$ = new vector<BaseAST*>();
    $$->push_back($1);
  }
  | ConstDefList ',' ConstDef {
    $1->push_back($3);
    $$ = $1;
  }
  ;

ConstDef
  : IDENT '=' ConstInitVal {
    auto ast = new ConstDefAST();
    ast->ident = *unique_ptr<string>($1);
    ast->init_val = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

ConstInitVal
  : ConstExp {
    $$ = $1;
  }
  ;

VarDecl
  : BType VarDefList ';' {
    auto ast = new VarDeclAST();
    for (auto def : *$2) {
      ast->var_defs.push_back(unique_ptr<BaseAST>(def));
    }
    delete $2;
    $$ = ast;
  }
  ;

VarDefList
  : VarDef {
    $$ = new vector<BaseAST*>();
    $$->push_back($1);
  }
  | VarDefList ',' VarDef {
    $1->push_back($3);
    $$ = $1;
  }
  ;

VarDef
  : IDENT {
    auto ast = new VarDefAST();
    ast->ident = *unique_ptr<string>($1);
    ast->has_init = false;
    $$ = ast;
  }
  | IDENT '=' InitVal {
    auto ast = new VarDefAST();
    ast->ident = *unique_ptr<string>($1);
    ast->init_val = unique_ptr<BaseAST>($3);
    ast->has_init = true;
    $$ = ast;
  }
  ;

InitVal
  : Exp {
    $$ = $1;
  }
  ;

BType
  : INT {
    $$ = new BTypeAST();
  }
  ;

Stmt
  : RETURN ';' {
    auto ast = new StmtAST();
    ast->type = StmtType::RETURN;
    ast->exp = nullptr;
    $$ = ast;
  }
  | RETURN Exp ';' {
    auto ast = new StmtAST();
    ast->type = StmtType::RETURN;
    ast->exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  | LVal '=' Exp ';' {
    auto ast = new StmtAST();
    ast->type = StmtType::ASSIGN;
    ast->lval = unique_ptr<BaseAST>($1);
    ast->exp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | Exp ';' {
    auto ast = new StmtAST();
    ast->type = StmtType::EXPR;
    ast->exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | ';' {
    auto ast = new StmtAST();
    ast->type = StmtType::EMPTY;
    ast->exp = nullptr;
    $$ = ast;
  }
  | Block {
    $$ = $1;
  }
  | IF '(' Exp ')' Stmt {
    auto ast = new IfStmtAST();
    ast->cond = unique_ptr<BaseAST>($3);
    ast->then_stmt = unique_ptr<BaseAST>($5);
    ast->else_stmt = nullptr;
    $$ = ast;
  }
  | IF '(' Exp ')' Stmt ELSE Stmt {
    auto ast = new IfStmtAST();
    ast->cond = unique_ptr<BaseAST>($3);
    ast->then_stmt = unique_ptr<BaseAST>($5);
    ast->else_stmt = unique_ptr<BaseAST>($7);
    $$ = ast;
  }
  ;

Exp
  : LOrExp {
    $$ = $1;
  }
  ;

LVal
  : IDENT {
    auto ast = new LValAST();
    ast->ident = *unique_ptr<string>($1);
    $$ = ast;
  }
  ;

ConstExp
  : Exp {
    $$ = $1;
  }
  ;

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

PrimaryExp
  : '(' Exp ')' {
    $$ = $2;
  }
  | LVal {
    $$ = $1;
  }
  | Number {
    $$ = $1;
  }
  ;

Number
  : INT_CONST {
    $$ = new NumberAST($1);
  }
  ;

%%

void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
  cerr << "error: " << s << endl;
}
