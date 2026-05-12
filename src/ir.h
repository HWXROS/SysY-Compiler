#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

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
  std::string name;
  bool is_named;
 public:
  ValueRef(int i) : id(i), is_named(false) {}
  ValueRef(const std::string &n) : id(-1), name(n), is_named(true) {}
  int GetId() const { return id; }
  const std::string& GetName() const { return name; }
  bool IsNamed() const { return is_named; }
  void Dump(std::ostream &os) const override {
    if (is_named) {
      os << name;
    } else {
      os << "%" << id;
    }
  }
};

class Instruction {
 public:
  virtual ~Instruction() = default;
  virtual void Dump(std::ostream &os) const = 0;
};

class AllocInst : public Instruction {
  int result_id;
  std::string type_str;
 public:
  AllocInst(int id, const std::string &type = "i32") : result_id(id), type_str(type) {}
  int GetResultId() const { return result_id; }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = alloc " << type_str << "\n";
  }
};

class GlobalAllocInst {
  std::string name;
  std::string type_str;
  std::unique_ptr<KoopaValue> init_val;
 public:
  GlobalAllocInst(const std::string &n, const std::string &t, std::unique_ptr<KoopaValue> init = nullptr)
      : name(n), type_str(t), init_val(std::move(init)) {}
  const std::string& GetName() const { return name; }
  void Dump(std::ostream &os) const {
    os << "global @" << name << " = alloc " << type_str;
    if (init_val) {
      os << ", ";
      init_val->Dump(os);
    }
    os << "\n";
  }
};

class StoreInst : public Instruction {
  std::unique_ptr<KoopaValue> value;
  int addr_id;
  std::string addr_name;
  bool addr_is_named;
 public:
  StoreInst(std::unique_ptr<KoopaValue> v, int a)
      : value(std::move(v)), addr_id(a), addr_is_named(false) {}
  StoreInst(std::unique_ptr<KoopaValue> v, const std::string &name)
      : value(std::move(v)), addr_id(-1), addr_name(name), addr_is_named(true) {}
  const KoopaValue* GetValue() const { return value.get(); }
  int GetAddrId() const { return addr_id; }
  void Dump(std::ostream &os) const override {
    os << "  store ";
    value->Dump(os);
    os << ", ";
    if (addr_is_named) {
      os << addr_name;
    } else {
      os << "%" << addr_id;
    }
    os << "\n";
  }
};

class LoadInst : public Instruction {
  int result_id;
  int addr_id;
  std::string addr_name;
  bool addr_is_named;
 public:
  LoadInst(int id, int a)
      : result_id(id), addr_id(a), addr_is_named(false) {}
  LoadInst(int id, const std::string &name)
      : result_id(id), addr_id(-1), addr_name(name), addr_is_named(true) {}
  int GetResultId() const { return result_id; }
  int GetAddrId() const { return addr_id; }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = load ";
    if (addr_is_named) {
      os << addr_name;
    } else {
      os << "%" << addr_id;
    }
    os << "\n";
  }
};

class GetElemPtrInst : public Instruction {
  int result_id;
  int base_addr_id;
  std::unique_ptr<KoopaValue> offset;
 public:
  GetElemPtrInst(int id, int base, std::unique_ptr<KoopaValue> off)
      : result_id(id), base_addr_id(base), offset(std::move(off)) {}
  int GetResultId() const { return result_id; }
  int GetBaseAddrId() const { return base_addr_id; }
  const KoopaValue* GetOffset() const { return offset.get(); }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = getelemptr %" << base_addr_id << ", ";
    offset->Dump(os);
    os << "\n";
  }
};

class GetPtrInst : public Instruction {
  int result_id;
  int base_addr_id;
  std::string base_name;
  bool base_is_named;
  std::unique_ptr<KoopaValue> index;
 public:
  GetPtrInst(int id, int base, std::unique_ptr<KoopaValue> idx)
      : result_id(id), base_addr_id(base), base_is_named(false), index(std::move(idx)) {}
  GetPtrInst(int id, int base, std::unique_ptr<KoopaValue> idx, const std::string &name)
      : result_id(id), base_addr_id(base), base_name(name), base_is_named(true), index(std::move(idx)) {}
  int GetResultId() const { return result_id; }
  int GetBaseAddrId() const { return base_addr_id; }
  const KoopaValue* GetIndex() const { return index.get(); }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = getptr ";
    if (base_is_named) {
      os << base_name;
    } else {
      os << "%" << base_addr_id;
    }
    os << ", ";
    index->Dump(os);
    os << "\n";
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

enum class BinaryOp {
  ADD, SUB, MUL, DIV, MOD,
  LT, GT, LE, GE, EQ, NE,
  AND, OR
};

class BinaryOpInst : public Instruction {
  int result_id;
  BinaryOp op;
  std::unique_ptr<KoopaValue> lhs;
  std::unique_ptr<KoopaValue> rhs;
 public:
  BinaryOpInst(int id, BinaryOp o, std::unique_ptr<KoopaValue> l, std::unique_ptr<KoopaValue> r)
      : result_id(id), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
  int GetResultId() const { return result_id; }
  BinaryOp GetOp() const { return op; }
  const KoopaValue* GetLHS() const { return lhs.get(); }
  const KoopaValue* GetRHS() const { return rhs.get(); }
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = ";
    switch (op) {
      case BinaryOp::ADD:
        os << "add ";
        break;
      case BinaryOp::SUB:
        os << "sub ";
        break;
      case BinaryOp::MUL:
        os << "mul ";
        break;
      case BinaryOp::DIV:
        os << "div ";
        break;
      case BinaryOp::MOD:
        os << "mod ";
        break;
      case BinaryOp::LT:
        os << "lt ";
        break;
      case BinaryOp::GT:
        os << "gt ";
        break;
      case BinaryOp::LE:
        os << "le ";
        break;
      case BinaryOp::GE:
        os << "ge ";
        break;
      case BinaryOp::EQ:
        os << "eq ";
        break;
      case BinaryOp::NE:
        os << "ne ";
        break;
      case BinaryOp::AND:
        os << "and ";
        break;
      case BinaryOp::OR:
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
  bool is_void;
 public:
  RetInst(std::unique_ptr<KoopaValue> v, bool iv = false) : value(std::move(v)), is_void(iv) {}
  const KoopaValue* GetValue() const { return value.get(); }
  bool IsVoid() const { return is_void; }
  void Dump(std::ostream &os) const override {
    os << "  ret";
    if (!is_void && value) {
      os << " ";
      value->Dump(os);
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
  const KoopaValue* GetCond() const { return cond.get(); }
  const std::string& GetTrueLabel() const { return true_label; }
  const std::string& GetFalseLabel() const { return false_label; }
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

class CallInst : public Instruction {
  std::string func_name;
  std::vector<std::unique_ptr<KoopaValue>> args;
  bool is_void;
  int id;
 public:
  CallInst(const std::string &name, std::vector<std::unique_ptr<KoopaValue>> a, bool iv = false, int i = -1)
      : func_name(name), args(std::move(a)), is_void(iv), id(i) {}
  const std::string& GetFuncName() const { return func_name; }
  const std::vector<std::unique_ptr<KoopaValue>>& GetArgs() const { return args; }
  bool IsVoid() const { return is_void; }
  int GetId() const { return id; }
  void Dump(std::ostream &os) const override {
    if (!is_void) {
      if (id != -1) {
        os << "  %" << id << " = call @" << func_name << "(";
      } else {
        os << "  %call_inst = call @" << func_name << "(";
      }
    } else {
      os << "  call @" << func_name << "(";
    }
    for (size_t i = 0; i < args.size(); ++i) {
      args[i]->Dump(os);
      if (i < args.size() - 1) os << ", ";
    }
    os << ")\n";
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
  const std::vector<std::unique_ptr<Instruction>>& GetInsts() const {
    return insts;
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
  bool is_void;
  std::vector<std::pair<std::string, std::string>> params;
  std::vector<std::unique_ptr<BasicBlock>> blocks;
 public:
  Function(const std::string &n, const std::string &rt, bool iv = false)
      : name(n), ret_type(rt), is_void(iv) {}
  void AddParam(const std::string &param_name, const std::string &param_type = "i32") {
    params.push_back({param_name, param_type});
  }
  void AddBlock(std::unique_ptr<BasicBlock> block) {
    blocks.push_back(std::move(block));
  }
  BasicBlock* CreateBlock(const std::string &name) {
    blocks.push_back(std::make_unique<BasicBlock>(name));
    return blocks.back().get();
  }
  void MoveBlockToEnd(BasicBlock* block) {
    auto it = std::find_if(blocks.begin(), blocks.end(),
      [block](const std::unique_ptr<BasicBlock>& b) { return b.get() == block; });
    if (it != blocks.end()) {
      auto ptr = std::move(*it);
      blocks.erase(it);
      blocks.push_back(std::move(ptr));
    }
  }
  size_t GetBlockCount() const { return blocks.size(); }
  BasicBlock* GetBlock(size_t index) { return blocks[index].get(); }
  bool IsVoid() const { return is_void; }
  const std::string& GetName() const { return name; }
  const std::vector<std::unique_ptr<BasicBlock>>& GetBlocks() const {
    return blocks;
  }
  size_t GetParamCount() const { return params.size(); }
  std::string GetParamName(size_t index) const { return params[index].first; }
  void Dump(std::ostream &os) const {
    os << "fun @" << name << "(";
    for (size_t i = 0; i < params.size(); ++i) {
      if (i > 0) os << ", ";
      os << "@" << params[i].first << ": " << params[i].second;
    }
    os << ")";
    if (is_void) {
      os << " {\n";
    } else {
      os << ": " << ret_type << " {\n";
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
      blocks[i]->Dump(os);
    }
    if (!blocks.empty() && !blocks.back()->HasTerminator()) {
      if (is_void) {
        os << "  ret\n";
      } else {
        os << "  ret 0\n";
      }
    }
    os << "}\n";
  }
  void DumpDecl(std::ostream &os) const {
    os << "decl @" << name << "(): " << ret_type << "\n";
  }
};

class Program {
  std::vector<std::unique_ptr<Function>> funcs;
  std::vector<std::unique_ptr<Function>> decls;
  std::vector<std::unique_ptr<GlobalAllocInst>> globals;
 public:
  void AddFunc(std::unique_ptr<Function> func) {
    funcs.push_back(std::move(func));
  }
  void AddDecl(const std::string &name, const std::string &ret_type, bool is_void = true) {
    auto decl = std::make_unique<Function>(name, ret_type, is_void);
    decls.push_back(std::move(decl));
  }
  void AddGlobal(const std::string &name, const std::string &type, std::unique_ptr<KoopaValue> init = nullptr) {
    auto global = std::make_unique<GlobalAllocInst>(name, type, std::move(init));
    globals.push_back(std::move(global));
  }
  void Dump(std::ostream &os) const {
    for (const auto &global : globals) {
      global->Dump(os);
    }
    if (!globals.empty() && !decls.empty()) os << "\n";
    for (const auto &decl : decls) {
      decl->DumpDecl(os);
    }
    if ((!funcs.empty() || !globals.empty()) && !decls.empty()) os << "\n";
    for (const auto &func : funcs) {
      func->Dump(os);
    }
  }
  std::string ToString() const {
    std::ostringstream oss;
    Dump(oss);
    return oss.str();
  }
  const std::vector<std::unique_ptr<Function>>& GetFuncs() const {
    return funcs;
  }
};
