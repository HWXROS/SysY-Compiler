#pragma once

#include <iostream>
#include <memory>
#include <string>
#include "ir.h"

class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump() const = 0;
  virtual std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const = 0;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override {
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override {
    return nullptr;
  }
};

class FuncTypeAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "FuncTypeAST { int }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override {
    return nullptr;
  }
};

class BlockAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> stmt;
  void Dump() const override {
    std::cout << "BlockAST { ";
    stmt->Dump();
    std::cout << " }";
  }
  std::unique_ptr<BasicBlock> GenIR() const;
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override {
    return nullptr;
  }
};

class StmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> exp;
  void Dump() const override {
    std::cout << "StmtAST { ";
    exp->Dump();
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override;
};

class NumberAST : public BaseAST {
 public:
  int value;
  NumberAST(int v) : value(v) {}
  void Dump() const override {
    std::cout << value;
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override;
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
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder) const override;
};


