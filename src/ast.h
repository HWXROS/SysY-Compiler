#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include "ir.h"

enum class StmtType {
  RETURN,
  ASSIGN
};

class SymbolTable {
 public:
  std::map<std::string, int> const_values;
  std::map<std::string, int> var_addrs;
  
  bool IsConst(const std::string &name) const {
    return const_values.find(name) != const_values.end();
  }
  
  bool IsVar(const std::string &name) const {
    return var_addrs.find(name) != var_addrs.end();
  }
  
  bool Exists(const std::string &name) const {
    return IsConst(name) || IsVar(name);
  }
  
  int GetConstValue(const std::string &name) const {
    auto it = const_values.find(name);
    if (it != const_values.end()) {
      return it->second;
    }
    return 0;
  }
  
  int GetVarAddr(const std::string &name) const {
    auto it = var_addrs.find(name);
    if (it != var_addrs.end()) {
      return it->second;
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
  std::unique_ptr<BasicBlock> GenIR(SymbolTable &symtab) const;
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override {
    return nullptr;
  }
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
