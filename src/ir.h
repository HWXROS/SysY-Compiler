#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>

class KoopaValue {
 public:
  virtual ~KoopaValue() = default;
  virtual void Dump() const = 0;
  virtual void DumpRiscV() const = 0;
};

class IntConst : public KoopaValue {
  int value;
 public:
  IntConst(int v) : value(v) {}
  int GetValue() const { return value; }
  void Dump() const override {
    std::cout << value;
  }
  void DumpRiscV() const override {
    std::cout << "    li a0, " << value << "\n";
  }
};

class RetInst : public KoopaValue {
  std::unique_ptr<KoopaValue> value;
 public:
  RetInst(std::unique_ptr<KoopaValue> v) : value(std::move(v)) {}
  void Dump() const override {
    std::cout << "  ret ";
    value->Dump();
    std::cout << "\n";
  }
  void DumpRiscV() const override {
    value->DumpRiscV();
    std::cout << "    ret\n";
  }
};

class BasicBlock {
  std::string name;
  std::vector<std::unique_ptr<KoopaValue>> insts;
 public:
  BasicBlock(const std::string &n) : name(n) {}
  void AddInst(std::unique_ptr<KoopaValue> inst) {
    insts.push_back(std::move(inst));
  }
  void Dump() const {
    std::cout << "%" << name << ":\n";
    for (const auto &inst : insts) {
      inst->Dump();
    }
  }
  void DumpRiscV() const {
    for (const auto &inst : insts) {
      inst->DumpRiscV();
    }
  }
};

class Function {
  std::string name;
  std::string ret_type;
  std::vector<std::unique_ptr<BasicBlock>> blocks;
 public:
  Function(const std::string &n, const std::string &rt)
      : name(n), ret_type(rt) {}
  void AddBlock(std::unique_ptr<BasicBlock> block) {
    blocks.push_back(std::move(block));
  }
  void Dump() const {
    std::cout << "fun @" << name << "(): " << ret_type << " {\n";
    for (const auto &block : blocks) {
      block->Dump();
    }
    std::cout << "}\n";
  }
  void DumpRiscV() const {
    std::cout << "  .text\n";
    std::cout << "  .globl " << name << "\n";
    std::cout << name << ":\n";
    for (const auto &block : blocks) {
      block->DumpRiscV();
    }
  }
};

class Program {
  std::vector<std::unique_ptr<Function>> funcs;
 public:
  void AddFunc(std::unique_ptr<Function> func) {
    funcs.push_back(std::move(func));
  }
  void Dump() const {
    for (const auto &func : funcs) {
      func->Dump();
    }
  }
  void DumpRiscV() const {
    for (const auto &func : funcs) {
      func->DumpRiscV();
    }
  }
};
