#pragma once

#include <iostream>
#include <memory>
#include <string>
#include "ir.h"

class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump() const = 0;
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
};

class FuncTypeAST : public BaseAST {
 public:
  void Dump() const override {
    std::cout << "FuncTypeAST { int }";
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
};

class StmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> number;
  void Dump() const override {
    std::cout << "StmtAST { ";
    number->Dump();
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR() const;
};

class NumberAST : public BaseAST {
 public:
  int value;
  NumberAST(int v) : value(v) {}
  void Dump() const override {
    std::cout << value;
  }
  std::unique_ptr<KoopaValue> GenIR() const;
};
