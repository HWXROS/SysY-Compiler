#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>

class IRBuilder {
 public:
  int next_id = 0;
  
  int NewId() { return next_id++; }
};

class KoopaValue {
 public:
  virtual ~KoopaValue() = default;
  virtual void Dump(std::ostream &os) const = 0;
  virtual bool IsConst() const { return false; }
  virtual int GetConstValue() const { return 0; }
};

class IntConst : public KoopaValue {
  int value;
 public:
  IntConst(int v) : value(v) {}
  int GetValue() const { return value; }
  bool IsConst() const override { return true; }
  int GetConstValue() const override { return value; }
  void Dump(std::ostream &os) const override {
    os << value;
  }
};

class ValueRef : public KoopaValue {
  int id;
 public:
  ValueRef(int i) : id(i) {}
  int GetId() const { return id; }
  void Dump(std::ostream &os) const override {
    os << "%" << id;
  }
};

class Instruction {
 public:
  virtual ~Instruction() = default;
  virtual void Dump(std::ostream &os) const = 0;
};

class AllocInst : public Instruction {
  int result_id;
 public:
  AllocInst(int id) : result_id(id) {}
  int GetResultId() const { return result_id; }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = alloc i32\n";
  }
};

class StoreInst : public Instruction {
  std::unique_ptr<KoopaValue> value;
  int addr_id;
 public:
  StoreInst(std::unique_ptr<KoopaValue> v, int a)
      : value(std::move(v)), addr_id(a) {}
  void Dump(std::ostream &os) const override {
    os << "  store ";
    value->Dump(os);
    os << ", %" << addr_id << "\n";
  }
};

class LoadInst : public Instruction {
  int result_id;
  int addr_id;
 public:
  LoadInst(int id, int a)
      : result_id(id), addr_id(a) {}
  int GetResultId() const { return result_id; }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = load %" << addr_id << "\n";
  }
};

class UnaryOpInst : public Instruction {
  int result_id;
  char op;
  std::unique_ptr<KoopaValue> operand;
 public:
  UnaryOpInst(int id, char o, std::unique_ptr<KoopaValue> opnd) 
      : result_id(id), op(o), operand(std::move(opnd)) {}
  int GetResultId() const { return result_id; }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = ";
    switch (op) {
      case '+':
        operand->Dump(os);
        break;
      case '-':
        os << "sub 0, ";
        operand->Dump(os);
        break;
      case '!':
        os << "eq 0, ";
        operand->Dump(os);
        break;
    }
    os << "\n";
  }
};

class BinaryOpInst : public Instruction {
  int result_id;
  char op;
  std::unique_ptr<KoopaValue> lhs;
  std::unique_ptr<KoopaValue> rhs;
 public:
  BinaryOpInst(int id, char o, std::unique_ptr<KoopaValue> l, std::unique_ptr<KoopaValue> r)
      : result_id(id), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
  int GetResultId() const { return result_id; }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = ";
    switch (op) {
      case '+':
        os << "add ";
        break;
      case '-':
        os << "sub ";
        break;
      case '*':
        os << "mul ";
        break;
      case '/':
        os << "div ";
        break;
      case '%':
        os << "mod ";
        break;
      case '<':
        os << "lt ";
        break;
      case '>':
        os << "gt ";
        break;
      case 'L':
        os << "le ";
        break;
      case 'G':
        os << "ge ";
        break;
      case 'E':
        os << "eq ";
        break;
      case 'N':
        os << "ne ";
        break;
      case '&':
        os << "and ";
        break;
      case '|':
        os << "or ";
        break;
    }
    lhs->Dump(os);
    os << ", ";
    rhs->Dump(os);
    os << "\n";
  }
};

class RetInst : public Instruction {
  std::unique_ptr<KoopaValue> value;
 public:
  RetInst(std::unique_ptr<KoopaValue> v) : value(std::move(v)) {}
  void Dump(std::ostream &os) const override {
    os << "  ret ";
    if (value) {
      value->Dump(os);
    } else {
      os << "0";
    }
    os << "\n";
  }
};

class BranchInst : public Instruction {
  std::unique_ptr<KoopaValue> cond;
  std::string true_label;
  std::string false_label;
 public:
  BranchInst(std::unique_ptr<KoopaValue> c, const std::string &t, const std::string &f)
      : cond(std::move(c)), true_label(t), false_label(f) {}
  void Dump(std::ostream &os) const override {
    os << "  br ";
    cond->Dump(os);
    os << ", %" << true_label << ", %" << false_label << "\n";
  }
};

class JumpInst : public Instruction {
  std::string target_label;
 public:
  JumpInst(const std::string &t) : target_label(t) {}
  const std::string& GetTarget() const { return target_label; }
  void Dump(std::ostream &os) const override {
    os << "  jump %" << target_label << "\n";
  }
};

class BasicBlock {
  std::string name;
  std::string next_block;
  std::vector<std::unique_ptr<Instruction>> insts;
  bool is_protected = false;
 public:
  BasicBlock(const std::string &n) : name(n) {}
  const std::string& GetName() const { return name; }
  void SetNextBlock(const std::string &n) { next_block = n; }
  const std::string& GetNextBlock() const { return next_block; }
  void SetProtected(bool v) { is_protected = v; }
  bool IsProtected() const { return is_protected; }
  void AddInst(std::unique_ptr<Instruction> inst) {
    insts.push_back(std::move(inst));
  }
  bool HasTerminator() const {
    if (insts.empty()) return false;
    auto last = insts.back().get();
    return dynamic_cast<RetInst*>(last) || 
           dynamic_cast<JumpInst*>(last) || 
           dynamic_cast<BranchInst*>(last);
  }
  bool IsEmpty() const { return insts.empty(); }
  Instruction* GetLastInst() const { 
    if (insts.empty()) return nullptr;
    return insts.back().get(); 
  }
  void RemoveLastInst() { 
    if (!insts.empty()) insts.pop_back(); 
  }
  void Dump(std::ostream &os) const {
    os << "%" << name << ":\n";
    for (const auto &inst : insts) {
      inst->Dump(os);
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
  BasicBlock* CreateBlock(const std::string &name) {
    blocks.push_back(std::make_unique<BasicBlock>(name));
    return blocks.back().get();
  }
  size_t GetBlockCount() const { return blocks.size(); }
  BasicBlock* GetBlock(size_t index) { return blocks[index].get(); }
  void Dump(std::ostream &os) const {
    os << "fun @" << name << "(): " << ret_type << " {\n";
    for (size_t i = 0; i < blocks.size(); ++i) {
      blocks[i]->Dump(os);
    }
    if (!blocks.empty() && !blocks.back()->HasTerminator()) {
      os << "  ret 0\n";
    }
    os << "}\n";
  }
};

class Program {
  std::vector<std::unique_ptr<Function>> funcs;
 public:
  void AddFunc(std::unique_ptr<Function> func) {
    funcs.push_back(std::move(func));
  }
  void Dump(std::ostream &os) const {
    for (const auto &func : funcs) {
      func->Dump(os);
    }
  }
  std::string ToString() const {
    std::ostringstream oss;
    Dump(oss);
    return oss.str();
  }
};
