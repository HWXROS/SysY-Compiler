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
  std::map<std::string, bool> func_is_void;
  std::map<std::string, bool> is_array;
  std::map<std::string, bool> is_global;
  std::map<std::string, std::vector<int>> array_dims;
  std::map<std::string, std::vector<bool>> func_param_is_array;
  std::map<std::string, std::string> rename_map;
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

  bool IsFuncVoid(const std::string &name) const {
    auto it = func_is_void.find(name);
    if (it != func_is_void.end()) {
      return it->second;
    }
    if (parent) {
      return parent->IsFuncVoid(name);
    }
    return false;
  }

  bool IsArray(const std::string &name) const {
    auto it = is_array.find(name);
    if (it != is_array.end()) {
      return it->second;
    }
    if (parent) {
      return parent->IsArray(name);
    }
    return false;
  }

  bool IsGlobal(const std::string &name) const {
    auto it = is_global.find(name);
    if (it != is_global.end()) {
      return it->second;
    }
    if (parent) {
      return parent->IsGlobal(name);
    }
    return false;
  }

  bool IsFuncParamArray(const std::string &func_name, int param_idx) const {
    auto it = func_param_is_array.find(func_name);
    if (it != func_param_is_array.end() && param_idx < (int)it->second.size()) {
      return it->second[param_idx];
    }
    if (parent) {
      return parent->IsFuncParamArray(func_name, param_idx);
    }
    return false;
  }

  void SetFuncParamArray(const std::string &func_name, const std::vector<bool> &is_array_params) {
    func_param_is_array[func_name] = is_array_params;
  }

  std::vector<int> GetArrayDims(const std::string &name) const {
    auto it = array_dims.find(name);
    if (it != array_dims.end()) {
      return it->second;
    }
    if (parent) {
      return parent->GetArrayDims(name);
    }
    return {};
  }

  int GetArraySize(const std::string &name) const {
    auto dims = GetArrayDims(name);
    int size = 1;
    for (int d : dims) {
      size *= d;
    }
    return size;
  }
};

class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump() const = 0;
  virtual std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const = 0;
};

class CompUnitAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;
  void Dump() const override {
    std::cout << "CompUnitAST { ";
    for (const auto &item : items) {
      item->Dump();
      std::cout << ", ";
    }
    std::cout << " }";
  }
  std::unique_ptr<Program> GenIR() const;
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override {
    return nullptr;
  }
};

class FuncDefAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_type;
  std::string ident;
  std::vector<std::unique_ptr<BaseAST>> params;
  std::unique_ptr<BaseAST> block;
  void Dump() const override {
    std::cout << "FuncDefAST { ";
    func_type->Dump();
    std::cout << ", " << ident << ", ";
    block->Dump();
    std::cout << " }";
  }
  std::unique_ptr<Function> GenIR(SymbolTable &global_symtab) const;
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override {
    return nullptr;
  }
};

class FuncTypeAST : public BaseAST {
 public:
  bool is_void = false;
  void Dump() const override {
    std::cout << "FuncTypeAST { " << (is_void ? "void" : "int") << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override {
    return nullptr;
  }
};

class FuncFParamAST : public BaseAST {
 public:
  std::string ident;
  bool is_array = false;
  void Dump() const override {
    std::cout << "FuncFParamAST { " << ident << (is_array ? "[]" : "") << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class BTypeAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "BTypeAST { int }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override {
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class ConstDefAST : public BaseAST {
 public:
  std::string ident;
  std::vector<int> dims;
  std::vector<std::unique_ptr<BaseAST>> dim_exps;
  std::unique_ptr<BaseAST> init_val;
  void Dump() const override {
    std::cout << "ConstDefAST { " << ident;
    for (int d : dims) {
      std::cout << "[" << d << "]";
    }
    std::cout << ", ";
    init_val->Dump();
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class VarDefAST : public BaseAST {
 public:
  std::string ident;
  std::vector<int> dims;
  std::vector<std::unique_ptr<BaseAST>> dim_exps;
  std::unique_ptr<BaseAST> init_val;
  bool has_init;
  void Dump() const override {
    std::cout << "VarDefAST { " << ident;
    for (int d : dims) {
      std::cout << "[" << d << "]";
    }
    if (has_init) {
      std::cout << ", ";
      init_val->Dump();
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class BreakStmtAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "BreakStmtAST";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class ContinueStmtAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "ContinueStmtAST";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class LValAST : public BaseAST {
 public:
  std::string ident;
  std::vector<std::unique_ptr<BaseAST>> indexes;
  void Dump() const override {
    std::cout << "LValAST { " << ident;
    for (const auto &idx : indexes) {
      std::cout << "[";
      idx->Dump();
      std::cout << "]";
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
  std::unique_ptr<KoopaValue> GenIRPtr(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const;
};

class NumberAST : public BaseAST {
 public:
  int value;
  NumberAST(int v) : value(v) {}
  void Dump() const override {
    std::cout << value;
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class CallExprAST : public BaseAST {
 public:
  std::string ident;
  std::vector<std::unique_ptr<BaseAST>> args;
  void Dump() const override {
    std::cout << "CallExprAST { " << ident << ", args: " << args.size() << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};

class InitListAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;
  void Dump() const override {
    std::cout << "InitListAST { ";
    for (const auto &item : items) {
      item->Dump();
      std::cout << ", ";
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program = nullptr) const override;
};
