#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include "ir.h"

class Function;
class BasicBlock;
extern thread_local Function* g_current_func;
extern thread_local BasicBlock* g_current_bb;
extern thread_local std::vector<BasicBlock*> g_end_block_stack;

struct LoopContext {
  BasicBlock* cond_block;
  BasicBlock* end_block;
};
extern thread_local std::vector<LoopContext> g_loop_stack;

enum class StmtType {
  RETURN,
  ASSIGN,
  EXPR,      // 表达式语句
  EMPTY,     // 空语句
  BLOCK,     // 块语句（BlockAST 也用于 Stmt）
  BREAK,     // break 语句
  CONTINUE   // continue 语句
};

class SymbolTable {
 public:
  std::map<std::string, int> const_values;
  std::map<std::string, int> var_addrs;
  SymbolTable *parent;  // 父作用域
  
  SymbolTable(SymbolTable *p = nullptr) : parent(p) {}
  
  // 在当前作用域检查是否存在（不查找父作用域）
  bool ExistsLocal(const std::string &name) const {
    return const_values.count(name) || var_addrs.count(name);
  }
  
  // 在当前作用域或父作用域检查是否存在
  bool Exists(const std::string &name) const {
    if (const_values.count(name) || var_addrs.count(name)) {
      return true;
    }
    if (parent) {
      return parent->Exists(name);
    }
    return false;
  }
  
  bool IsConst(const std::string &name) const {
    if (const_values.count(name)) {
      return true;
    }
    if (parent) {
      return parent->IsConst(name);
    }
    return false;
  }
  
  bool IsVar(const std::string &name) const {
    if (var_addrs.count(name)) {
      return true;
    }
    if (parent) {
      return parent->IsVar(name);
    }
    return false;
  }
  
  int GetConstValue(const std::string &name) const {
    auto it = const_values.find(name);
    if (it != const_values.end()) {
      return it->second;
    }
    if (parent) {
      return parent->GetConstValue(name);
    }
    return 0;
  }
  
  int GetVarAddr(const std::string &name) const {
    auto it = var_addrs.find(name);
    if (it != var_addrs.end()) {
      return it->second;
    }
    if (parent) {
      return parent->GetVarAddr(name);
    }
    return 0;
  }
};

class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump() const = 0;
  virtual std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const = 0;
};

class CompUnitAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_def;
  void Dump() const override {
    std::cout << "CompUnitAST { ";
    func_def->Dump();
    std::cout << " }";
  }
  std::unique_ptr<Program> GenIR() const;
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override {
    return nullptr;
  }
};

class FuncDefAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_type;
  std::string ident;
  std::unique_ptr<BaseAST> block;
  void Dump() const override {
    std::cout << "FuncDefAST { ";
    func_type->Dump();
    std::cout << ", " << ident << ", ";
    block->Dump();
    std::cout << " }";
  }
  std::unique_ptr<Function> GenIR() const;
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override {
    return nullptr;
  }
};

class FuncTypeAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "FuncTypeAST { int }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override {
    return nullptr;
  }
};

class BTypeAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "BTypeAST { int }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override {
    return nullptr;
  }
};

class BlockAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;
  void Dump() const override {
    std::cout << "BlockAST { ";
    for (const auto &item : items) {
      item->Dump();
      std::cout << ", ";
    }
    std::cout << " }";
  }
  std::unique_ptr<BasicBlock> GenIR(IRBuilder &builder, SymbolTable &symtab) const;
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class ConstDeclAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> const_defs;
  void Dump() const override {
    std::cout << "ConstDeclAST { ";
    for (const auto &def : const_defs) {
      def->Dump();
      std::cout << ", ";
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class ConstDefAST : public BaseAST {
 public:
  std::string ident;
  std::unique_ptr<BaseAST> init_val;
  void Dump() const override {
    std::cout << "ConstDefAST { " << ident << ", ";
    init_val->Dump();
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class VarDeclAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> var_defs;
  void Dump() const override {
    std::cout << "VarDeclAST { ";
    for (const auto &def : var_defs) {
      def->Dump();
      std::cout << ", ";
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class VarDefAST : public BaseAST {
 public:
  std::string ident;
  std::unique_ptr<BaseAST> init_val;
  bool has_init;
  void Dump() const override {
    std::cout << "VarDefAST { " << ident;
    if (has_init) {
      std::cout << ", ";
      init_val->Dump();
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class StmtAST : public BaseAST {
 public:
  StmtType type;
  std::unique_ptr<BaseAST> exp;
  std::unique_ptr<BaseAST> lval;
  void Dump() const override {
    std::cout << "StmtAST { ";
    if (type == StmtType::RETURN) {
      std::cout << "return, ";
      exp->Dump();
    } else {
      lval->Dump();
      std::cout << " = ";
      exp->Dump();
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class IfStmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> cond;
  std::unique_ptr<BaseAST> then_stmt;
  std::unique_ptr<BaseAST> else_stmt;
  void Dump() const override {
    std::cout << "IfStmtAST { ";
    cond->Dump();
    std::cout << ", ";
    then_stmt->Dump();
    if (else_stmt) {
      std::cout << ", else, ";
      else_stmt->Dump();
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class WhileStmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> cond;
  std::unique_ptr<BaseAST> body;
  void Dump() const override {
    std::cout << "WhileStmtAST { ";
    cond->Dump();
    std::cout << ", ";
    body->Dump();
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class BreakStmtAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "BreakStmtAST";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class ContinueStmtAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "ContinueStmtAST";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class LValAST : public BaseAST {
 public:
  std::string ident;
  void Dump() const override {
    std::cout << "LValAST { " << ident << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class NumberAST : public BaseAST {
 public:
  int value;
  NumberAST(int v) : value(v) {}
  void Dump() const override {
    std::cout << value;
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class UnaryExprAST : public BaseAST {
 public:
  char op;
  std::unique_ptr<BaseAST> exp;
  void Dump() const override {
    std::cout << "UnaryExprAST { " << op << ", ";
    exp->Dump();
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};

class BinaryExprAST : public BaseAST {
 public:
  char op;
  std::unique_ptr<BaseAST> left, right;
  void Dump() const override {
    std::cout << "BinaryExprAST { " << op << ", ";
    left->Dump();
    std::cout << ", ";
    right->Dump();
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};
