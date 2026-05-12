%code requires {
  #include <memory>
  #include <string>
  #include <vector>
  #include <utility>
  #include "ast.h"
}

%glr-parser
%expect-rr 1

%{

#include <iostream>
#include "ast.h"

int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);

// Global variable to store parse result
BaseAST* g_parse_result = nullptr;

using namespace std;

%}

%parse-param { std::unique_ptr<BaseAST> &ast }

%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val;
  char char_val;
  std::vector<BaseAST*> *ast_list;
  std::vector<std::pair<int, BaseAST*>> *dim_list;
}

%token CONST INT VOID RETURN IF ELSE WHILE BREAK CONTINUE
%token EQ NE LE GE AND OR
%token <str_val> IDENT
%token <int_val> INT_CONST

%type <ast_val> CompUnit FuncDef FuncType Block Stmt MatchedStmt UnmatchedStmt Exp PrimaryExp UnaryExp MulExp AddExp RelExp EqExp LAndExp LOrExp Number
%type <ast_val> Decl ConstDecl VarDecl ConstDef VarDef ConstInitVal InitVal LVal ConstExp BType FuncFParam
%type <char_val> UnaryOp
%type <ast_list> BlockItem ConstDefList VarDefList FuncFParams FuncRParams ConstInitList InitList OptIndexList IndexList
%type <dim_list> DimList

%%

CompUnit
  : CompUnit Decl {
    auto comp_unit = static_cast<CompUnitAST*>($1);
    comp_unit->items.push_back(unique_ptr<BaseAST>($2));
    $$ = comp_unit;
    g_parse_result = comp_unit;
  }
  | CompUnit FuncDef {
    auto comp_unit = static_cast<CompUnitAST*>($1);
    comp_unit->items.push_back(unique_ptr<BaseAST>($2));
    $$ = comp_unit;
    g_parse_result = comp_unit;
  }
  | Decl {
    auto comp_unit = new CompUnitAST();
    comp_unit->items.push_back(unique_ptr<BaseAST>($1));
    $$ = comp_unit;
    g_parse_result = comp_unit;
  }
  | FuncDef {
    auto comp_unit = new CompUnitAST();
    comp_unit->items.push_back(unique_ptr<BaseAST>($1));
    $$ = comp_unit;
    g_parse_result = comp_unit;
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
  | FuncType IDENT '(' FuncFParams ')' Block {
    auto ast = new FuncDefAST();
    ast->func_type = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<string>($2);
    auto raw_list = static_cast<vector<BaseAST*>*>($4);
    for (auto ptr : *raw_list) {
      ast->params.push_back(unique_ptr<BaseAST>(ptr));
    }
    ast->block = unique_ptr<BaseAST>($6);
    $$ = ast;
  }
  ;

FuncFParams
  : FuncFParam {
    auto list = new vector<BaseAST*>();
    list->push_back($1);
    $$ = list;
  }
  | FuncFParams ',' FuncFParam {
    auto list = static_cast<vector<BaseAST*>*>($1);
    list->push_back($3);
    $$ = list;
  }
  ;

FuncFParam
  : BType IDENT {
    auto ast = new FuncFParamAST();
    ast->ident = *unique_ptr<string>($2);
    ast->is_array = false;
    $$ = ast;
  }
  | BType IDENT '[' ']' OptIndexList {
    auto ast = new FuncFParamAST();
    ast->ident = *unique_ptr<string>($2);
    ast->is_array = true;
    $$ = ast;
  }
  ;

FuncType
  : INT {
    $$ = new FuncTypeAST();
  }
  | VOID {
    auto ast = new FuncTypeAST();
    ast->is_void = true;
    $$ = ast;
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
  | IDENT DimList '=' ConstInitVal {
    auto ast = new ConstDefAST();
    ast->ident = *unique_ptr<string>($1);
    auto dim_list = static_cast<vector<pair<int, BaseAST*>>* >($2);
    for (auto &p : *dim_list) {
      ast->dims.push_back(p.first);
      ast->dim_exps.push_back(unique_ptr<BaseAST>(p.second));
    }
    delete dim_list;
    ast->init_val = unique_ptr<BaseAST>($4);
    $$ = ast;
  }
  ;

ConstInitVal
  : ConstExp {
    $$ = $1;
  }
  | '{' '}' {
    auto ast = new InitListAST();
    $$ = ast;
  }
  | '{' ConstInitList '}' {
    auto ast = new InitListAST();
    for (auto item : *$2) {
      ast->items.push_back(unique_ptr<BaseAST>(item));
    }
    delete $2;
    $$ = ast;
  }
  ;

ConstInitList
  : ConstInitVal {
    $$ = new vector<BaseAST*>();
    $$->push_back($1);
  }
  | ConstInitList ',' ConstInitVal {
    $1->push_back($3);
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
  | IDENT DimList {
    auto ast = new VarDefAST();
    ast->ident = *unique_ptr<string>($1);
    auto dim_list = static_cast<vector<pair<int, BaseAST*>>* >($2);
    for (auto &p : *dim_list) {
      ast->dims.push_back(p.first);
      ast->dim_exps.push_back(unique_ptr<BaseAST>(p.second));
    }
    delete dim_list;
    ast->has_init = false;
    $$ = ast;
  }
  | IDENT DimList '=' InitVal {
    auto ast = new VarDefAST();
    ast->ident = *unique_ptr<string>($1);
    auto dim_list = static_cast<vector<pair<int, BaseAST*>>* >($2);
    for (auto &p : *dim_list) {
      ast->dims.push_back(p.first);
      ast->dim_exps.push_back(unique_ptr<BaseAST>(p.second));
    }
    delete dim_list;
    ast->init_val = unique_ptr<BaseAST>($4);
    ast->has_init = true;
    $$ = ast;
  }
  ;

DimList
  : DimList '[' ConstExp ']' {
    auto list = static_cast<vector<pair<int, BaseAST*>>* >($1);
    list->push_back({0, $3});
    $$ = list;
  }
  | '[' ConstExp ']' {
    auto list = new vector<pair<int, BaseAST*>>();
    list->push_back({0, $2});
    $$ = list;
  }
  ;

InitVal
  : Exp {
    $$ = $1;
  }
  | '{' '}' {
    auto ast = new InitListAST();
    $$ = ast;
  }
  | '{' InitList '}' {
    auto ast = new InitListAST();
    for (auto item : *$2) {
      ast->items.push_back(unique_ptr<BaseAST>(item));
    }
    delete $2;
    $$ = ast;
  }
  ;

InitList
  : InitVal {
    $$ = new vector<BaseAST*>();
    $$->push_back($1);
  }
  | InitList ',' InitVal {
    $1->push_back($3);
    $$ = $1;
  }
  ;

BType
  : INT {
    $$ = new BTypeAST();
  }
  ;

Stmt
  : MatchedStmt
  | UnmatchedStmt
  ;

MatchedStmt
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
  | IF '(' Exp ')' MatchedStmt ELSE MatchedStmt {
    auto ast = new IfStmtAST();
    ast->cond = unique_ptr<BaseAST>($3);
    ast->then_stmt = unique_ptr<BaseAST>($5);
    ast->else_stmt = unique_ptr<BaseAST>($7);
    $$ = ast;
  }
  | WHILE '(' Exp ')' MatchedStmt {
    auto ast = new WhileStmtAST();
    ast->cond = unique_ptr<BaseAST>($3);
    ast->body = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  | BREAK ';' {
    auto ast = new BreakStmtAST();
    $$ = ast;
  }
  | CONTINUE ';' {
    auto ast = new ContinueStmtAST();
    $$ = ast;
  }
  ;

UnmatchedStmt
  : IF '(' Exp ')' Stmt {
    auto ast = new IfStmtAST();
    ast->cond = unique_ptr<BaseAST>($3);
    ast->then_stmt = unique_ptr<BaseAST>($5);
    ast->else_stmt = nullptr;
    $$ = ast;
  }
  | IF '(' Exp ')' MatchedStmt ELSE UnmatchedStmt {
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
  : IDENT OptIndexList {
    auto ast = new LValAST();
    ast->ident = *unique_ptr<string>($1);
    if ($2) {
      auto list = static_cast<vector<BaseAST*>*>(($2));
      for (auto ptr : *list) {
        ast->indexes.push_back(unique_ptr<BaseAST>(ptr));
      }
    }
    $$ = ast;
  }
  ;

OptIndexList
  : /* empty */ {
    $$ = nullptr;
  }
  | IndexList {
    $$ = $1;
  }
  ;

IndexList
  : '[' Exp ']' {
    auto list = new vector<BaseAST*>();
    list->push_back($2);
    $$ = list;
  }
  | IndexList '[' Exp ']' {
    auto list = static_cast<vector<BaseAST*>*>(($1));
    list->push_back($3);
    $$ = list;
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

FuncRParams
  : Exp {
    auto list = new vector<BaseAST*>();
    list->push_back($1);
    $$ = list;
  }
  | FuncRParams ',' Exp {
    auto list = static_cast<vector<BaseAST*>*>($1);
    list->push_back($3);
    $$ = list;
  }
  ;

PrimaryExp
  : '(' Exp ')' {
    $$ = $2;
  }
  | IDENT '(' FuncRParams ')' {
    auto ast = new CallExprAST();
    ast->ident = *unique_ptr<string>($1);
    auto raw_list = static_cast<vector<BaseAST*>*>($3);
    for (auto ptr : *raw_list) {
      ast->args.push_back(unique_ptr<BaseAST>(ptr));
    }
    $$ = ast;
  }
  | IDENT '(' ')' {
    auto ast = new CallExprAST();
    ast->ident = *unique_ptr<string>($1);
    $$ = ast;
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
