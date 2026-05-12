#include "riscv.h"
#include <cassert>
#include <map>
#include <set>
#include <algorithm>
#include <queue>
#include <climits>

// RISC-V 寄存器分配器
class RegAllocator {
public:
  // 可用的临时寄存器 (caller-saved)
  const std::vector<std::string> temp_regs = {"t0", "t1", "t2", "t3", "t4", "t5", "t6"};
  // 可用的保存寄存器 (callee-saved)
  const std::vector<std::string> saved_regs = {"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11"};
  
  // 当前使用的寄存器
  std::map<int, std::string> reg_map;  // value_id -> reg_name
  std::set<std::string> used_regs;     // 已使用的寄存器名
  
  // 栈帧分配
  std::map<int, int> stack_map;        // value_id -> stack_offset
  int stack_size = 0;
  
  // 活跃区间
  struct LiveInterval {
    int start;  // 定义位置
    int end;    // 最后使用位置
    int value_id;
    bool spilled;
    
    LiveInterval(int s, int e, int v) : start(s), end(e), value_id(v), spilled(false) {}
    
    bool Overlaps(const LiveInterval& other) const {
      return start < other.end && end > other.start;
    }
  };
  
  std::vector<LiveInterval> live_intervals;
  
  // 分配寄存器
  std::string AllocReg(int value_id, int pos) {
    // 检查是否已分配
    auto it = reg_map.find(value_id);
    if (it != reg_map.end()) {
      return it->second;
    }
    
    // 尝试分配空闲寄存器
    for (const auto& reg : temp_regs) {
      if (used_regs.find(reg) == used_regs.end()) {
        reg_map[value_id] = reg;
        used_regs.insert(reg);
        return reg;
      }
    }
    
    // 没有空闲寄存器，需要溢出
    return Spill(value_id, pos);
  }
  
  // 释放寄存器
  void FreeReg(int value_id) {
    auto it = reg_map.find(value_id);
    if (it != reg_map.end()) {
      used_regs.erase(it->second);
      reg_map.erase(it);
    }
  }
  
  // 溢出到栈
  std::string Spill(int value_id, int pos) {
    // 找到最早结束的活跃区间
    std::string victim_reg;
    int earliest_end = INT_MAX;
    int victim_id = -1;
    
    for (const auto& pair : reg_map) {
      int vid = pair.first;
      const std::string& reg = pair.second;
      
      // 找到这个寄存器对应的活跃区间
      for (auto& li : live_intervals) {
        if (li.value_id == vid && li.end < earliest_end && pos >= li.start) {
          earliest_end = li.end;
          victim_reg = reg;
          victim_id = vid;
        }
      }
    }
    
    if (!victim_reg.empty()) {
      // 保存受害者到栈
      if (stack_map.find(victim_id) == stack_map.end()) {
        stack_map[victim_id] = stack_size;
        stack_size -= 4;
      }
      
      // 释放受害者寄存器
      used_regs.erase(victim_reg);
      reg_map.erase(victim_id);
      
      // 分配给新值
      reg_map[value_id] = victim_reg;
      used_regs.insert(victim_reg);
      return victim_reg;
    }
    
    // 所有寄存器都在使用，使用栈
    if (stack_map.find(value_id) == stack_map.end()) {
      stack_map[value_id] = stack_size;
      stack_size -= 4;
    }
    return "stack";
  }
  
  int GetStackOffset(int value_id) {
    return stack_map[value_id];
  }
  
  int GetStackSize() const {
    return -stack_size;
  }
};

// 全局变量
static std::map<const koopa_raw_value_data*, int> value_stack;
static std::set<const koopa_raw_value_data*> referenced_values;
static int stack_size = 0;
static const koopa_raw_value_data* last_result = nullptr;
static std::map<uint32_t, int> param_stack_map;
static std::string current_func_name;
static std::map<const koopa_raw_value_data*, std::string> global_alloc_names;
static std::map<const koopa_raw_value_data*, int> alloc_max_idx;

// Safe lw/sw with large offset handling
static void LW(std::ostream &os, const std::string &reg, int offset) {
  if (offset >= -2048 && offset <= 2047) {
    os << "  lw " << reg << ", " << offset << "(sp)\n";
  } else {
    os << "  li t3, " << offset << "\n";
    os << "  add t3, sp, t3\n";
    os << "  lw " << reg << ", 0(t3)\n";
  }
}

static void SW(std::ostream &os, const std::string &reg, int offset) {
  if (offset >= -2048 && offset <= 2047) {
    os << "  sw " << reg << ", " << offset << "(sp)\n";
  } else {
    os << "  li t3, " << offset << "\n";
    os << "  add t3, sp, t3\n";
    os << "  sw " << reg << ", 0(t3)\n";
  }
}

// For our own IR format
static std::map<int, int> value_ref_stack;
static std::set<int> referenced_value_refs;
static int our_stack_size = 0;
static int last_value_id = -1;

static void CollectReferences(const koopa_raw_program_t &program) {
  for (size_t i = 0; i < program.funcs.len; ++i) {
    auto func = reinterpret_cast<koopa_raw_function_t>(program.funcs.buffer[i]);
    if (!func || func->bbs.len == 0) continue;
    
    for (size_t j = 0; j < func->bbs.len; ++j) {
      auto bb = reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[j]);
      for (size_t k = 0; k < bb->insts.len; ++k) {
        auto inst = reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[k]);
        if (inst->kind.tag == KOOPA_RVT_BINARY) {
          auto &binary = inst->kind.data.binary;
          if (binary.lhs->kind.tag != KOOPA_RVT_INTEGER) {
            referenced_values.insert(binary.lhs);
          }
          if (binary.rhs->kind.tag != KOOPA_RVT_INTEGER) {
            referenced_values.insert(binary.rhs);
          }
        } else if (inst->kind.tag == KOOPA_RVT_RETURN) {
          if (inst->kind.data.ret.value && 
              inst->kind.data.ret.value->kind.tag != KOOPA_RVT_INTEGER) {
            referenced_values.insert(inst->kind.data.ret.value);
          }
        } else if (inst->kind.tag == KOOPA_RVT_STORE) {
          auto &store = inst->kind.data.store;
          if (store.value->kind.tag != KOOPA_RVT_INTEGER) {
            referenced_values.insert(store.value);
          }
        } else if (inst->kind.tag == KOOPA_RVT_LOAD) {
          referenced_values.insert(inst);
        } else if (inst->kind.tag == KOOPA_RVT_BRANCH) {
          auto &branch = inst->kind.data.branch;
          if (branch.cond->kind.tag != KOOPA_RVT_INTEGER) {
            referenced_values.insert(branch.cond);
          }
        }
      }
    }
  }
}

void RiscVGenerator::Generate(const koopa_raw_program_t &program, std::ostream &os) {
  os_ = &os;
  value_stack.clear();
  referenced_values.clear();
  stack_size = 0;
  last_result = nullptr;
  global_alloc_names.clear();
  
  // First pass: collect global alloc names and generate .data section
  *os_ << "  .data\n";
  for (size_t i = 0; i < program.values.len; ++i) {
    auto val = reinterpret_cast<koopa_raw_value_t>(program.values.buffer[i]);
    if (val->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
      std::string name = val->name + 1;  // skip '@'
      global_alloc_names[val] = name;
      
      auto init = val->kind.data.global_alloc.init;
      int init_val = 0;
      if (init && init->kind.tag == KOOPA_RVT_INTEGER) {
        init_val = init->kind.data.integer.value;
      }
      
      *os_ << "  .globl " << name << "\n";
      *os_ << "  .align 2\n";
      *os_ << name << ":\n";
      if (init_val == 0) {
        *os_ << "  .zero 4\n";
      } else {
        *os_ << "  .word " << init_val << "\n";
      }
    }
  }
  *os_ << "  .text\n";
  
  CollectReferences(program);
  
  Visit(program.funcs);
}

void RiscVGenerator::Visit(const koopa_raw_slice_t &slice) {
  for (size_t i = 0; i < slice.len; ++i) {
    auto ptr = slice.buffer[i];
    switch (slice.kind) {
      case KOOPA_RSIK_FUNCTION:
        Visit(reinterpret_cast<koopa_raw_function_t>(ptr));
        break;
      case KOOPA_RSIK_BASIC_BLOCK:
        Visit(reinterpret_cast<koopa_raw_basic_block_t>(ptr));
        break;
      case KOOPA_RSIK_VALUE:
        Visit(reinterpret_cast<koopa_raw_value_t>(ptr));
        break;
      default:
        assert(false);
    }
  }
}

void RiscVGenerator::Visit(const koopa_raw_function_t &func) {
  if (func->bbs.len == 0) {
    return;
  }
  
  value_stack.clear();
  stack_size = 0;
  last_result = nullptr;
  param_stack_map.clear();
  
  current_func_name = func->name + 1;
  
  alloc_max_idx.clear();
  
  // Pre-compute alloc sizes from getptr indices
  for (size_t j = 0; j < func->bbs.len; ++j) {
    auto bb = reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[j]);
    for (size_t k = 0; k < bb->insts.len; ++k) {
      auto inst = reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[k]);
      if (inst->kind.tag == KOOPA_RVT_ALLOC) {
        alloc_max_idx[inst] = 1;  // at least 1 element
      } else if (inst->kind.tag == KOOPA_RVT_GET_PTR || inst->kind.tag == KOOPA_RVT_GET_ELEM_PTR) {
        auto src = (inst->kind.tag == KOOPA_RVT_GET_PTR) ? inst->kind.data.get_ptr.src : inst->kind.data.get_elem_ptr.src;
        auto it = alloc_max_idx.find(src);
        if (it != alloc_max_idx.end()) {
          auto idx_val = (inst->kind.tag == KOOPA_RVT_GET_PTR) ? inst->kind.data.get_ptr.index : inst->kind.data.get_elem_ptr.index;
          if (idx_val->kind.tag == KOOPA_RVT_INTEGER) {
            int idx = idx_val->kind.data.integer.value;
            if (idx + 1 > it->second) it->second = idx + 1;
          } else {
            if (it->second < 4096) it->second = 4096;
          }
        }
      }
    }
  }
  
  *os_ << "  .text\n";
  *os_ << "  .globl " << func->name + 1 << "\n";
  *os_ << func->name + 1 << ":\n";
  
  // Save function parameters to stack
  const std::string param_regs[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
  for (uint32_t i = 0; i < func->params.len; ++i) {
    int param_stack = stack_size;
    stack_size -= 4;
    if (i < 8) {
      SW(*os_, param_regs[i], param_stack);
    }
    param_stack_map[i] = param_stack;
  }
  
  Visit(func->bbs);
}

void RiscVGenerator::Visit(const koopa_raw_basic_block_t &bb) {
  const char* name = bb->name;
  if (name[0] == '%') {
    name += 1;
  }
  *os_ << current_func_name << "_" << name << ":\n";
  Visit(bb->insts);
}

void RiscVGenerator::Visit(const koopa_raw_value_t &value) {
  if (value == last_result) {
    return;
  }
  
  auto it = value_stack.find(value);
  if (it != value_stack.end()) {
    LW(*os_, "a0", it->second);
    last_result = nullptr;
    return;
  }
  
  const auto &kind = value->kind;
  switch (kind.tag) {
    case KOOPA_RVT_RETURN:
      Visit(kind.data.ret);
      break;
    case KOOPA_RVT_INTEGER:
      Visit(kind.data.integer);
      break;
    case KOOPA_RVT_BINARY:
      Visit(kind.data.binary, value);
      break;
    case KOOPA_RVT_FUNC_ARG_REF:
      {
        uint32_t idx = kind.data.func_arg_ref.index;
        auto it = param_stack_map.find(idx);
        if (it != param_stack_map.end()) {
          LW(*os_, "a0", it->second);
        }
        last_result = value;
      }
      break;
    case KOOPA_RVT_ALLOC:
      VisitAlloc(value);
      break;
    case KOOPA_RVT_STORE:
      Visit(kind.data.store);
      break;
    case KOOPA_RVT_LOAD:
      Visit(kind.data.load, value);
      break;
    case KOOPA_RVT_BRANCH:
      Visit(kind.data.branch);
      break;
    case KOOPA_RVT_JUMP:
      Visit(kind.data.jump);
      break;
    case KOOPA_RVT_GET_PTR:
      VisitGetPtr(kind.data.get_ptr, value);
      break;
    case KOOPA_RVT_GET_ELEM_PTR:
      VisitGetElemPtr(kind.data.get_elem_ptr, value);
      break;
    case KOOPA_RVT_CALL:
      Visit(kind.data.call, value);
      break;
    case KOOPA_RVT_GLOBAL_ALLOC:
      break;
    default:
      std::cerr << "Unknown instruction kind tag: " << kind.tag << std::endl;
      assert(false);
  }
}

void RiscVGenerator::Visit(const koopa_raw_return_t &ret) {
  if (ret.value) {
    Visit(ret.value);
  }
  *os_ << "  ret\n";
}

void RiscVGenerator::Visit(const koopa_raw_integer_t &integer) {
  *os_ << "  li a0, " << integer.value << "\n";
  last_result = nullptr;
}

void RiscVGenerator::VisitAlloc(const koopa_raw_value_t &value) {
  auto it = alloc_max_idx.find(value);
  int num_elements = (it != alloc_max_idx.end()) ? it->second : 1;
  // All allocs go at negative offsets from sp
  // stack_size starts at 0 and goes negative for both allocs and temps
  int addr = stack_size;
  stack_size -= num_elements * 4;
  value_stack[value] = addr;
  last_result = nullptr;
}

void RiscVGenerator::Visit(const koopa_raw_store_t &store) {
  auto addr_value = store.dest;
  auto addr_it = value_stack.find(addr_value);
  bool is_alloc_dest = (addr_value->kind.tag == KOOPA_RVT_ALLOC);
  
  if (addr_it != value_stack.end() && is_alloc_dest) {
    Visit(store.value);
    SW(*os_, "a0", addr_it->second);
  } else if (addr_it != value_stack.end()) {
    LW(*os_, "t0", addr_it->second);
    Visit(store.value);
    *os_ << "  sw a0, 0(t0)\n";
  } else if (addr_value == last_result) {
    *os_ << "  mv t0, a0\n";
    Visit(store.value);
    *os_ << "  sw a0, 0(t0)\n";
  } else {
    Visit(addr_value);
    *os_ << "  mv t0, a0\n";
    Visit(store.value);
    *os_ << "  sw a0, 0(t0)\n";
  }
  last_result = nullptr;
}

void RiscVGenerator::Visit(const koopa_raw_load_t &load, const koopa_raw_value_t &value) {
  auto addr_value = load.src;
  auto addr_it = value_stack.find(addr_value);
  bool is_alloc_src = (addr_value->kind.tag == KOOPA_RVT_ALLOC);
  
  if (addr_it != value_stack.end() && is_alloc_src) {
    LW(*os_, "a0", addr_it->second);
  } else if (addr_it != value_stack.end()) {
    LW(*os_, "t0", addr_it->second);
    *os_ << "  lw a0, 0(t0)\n";
  } else if (addr_value == last_result) {
    *os_ << "  lw a0, 0(a0)\n";
  } else {
    Visit(addr_value);
    *os_ << "  lw a0, 0(a0)\n";
  }
  
  last_result = value;
  
  if (referenced_values.find(value) != referenced_values.end()) {
    int result_stack = stack_size;
    stack_size -= 4;
    SW(*os_, "a0", result_stack);
    value_stack[value] = result_stack;
  }
}

void RiscVGenerator::Visit(const koopa_raw_branch_t &branch) {
  Visit(branch.cond);
  
  const char* true_bb_name = branch.true_bb->name;
  if (true_bb_name[0] == '%') {
    true_bb_name += 1;
  }
  
  const char* false_bb_name = branch.false_bb->name;
  if (false_bb_name[0] == '%') {
    false_bb_name += 1;
  }
  
  *os_ << "  bnez a0, " << current_func_name << "_" << true_bb_name << "\n";
  *os_ << "  j " << current_func_name << "_" << false_bb_name << "\n";
  last_result = nullptr;
}

void RiscVGenerator::Visit(const koopa_raw_jump_t &jump) {
  const char* target_name = jump.target->name;
  if (target_name[0] == '%') {
    target_name += 1;
  }
  
  *os_ << "  j " << current_func_name << "_" << target_name << "\n";
  last_result = nullptr;
}

void RiscVGenerator::Visit(const koopa_raw_binary_t &binary, const koopa_raw_value_t &value) {
  bool lhs_is_int = (binary.lhs->kind.tag == KOOPA_RVT_INTEGER);
  bool rhs_is_int = (binary.rhs->kind.tag == KOOPA_RVT_INTEGER);
  bool lhs_in_stack = (!lhs_is_int) && (value_stack.find(binary.lhs) != value_stack.end());
  
  int lhs_stack_pos = 0;
  
  if (lhs_is_int) {
    *os_ << "  li t1, " << binary.lhs->kind.data.integer.value << "\n";
  } else if (lhs_in_stack) {
    lhs_stack_pos = value_stack[binary.lhs];
  } else {
    Visit(binary.lhs);
    lhs_stack_pos = stack_size;
    stack_size -= 4;
    SW(*os_, "a0", lhs_stack_pos);
    last_result = nullptr;
  }
  
  if (rhs_is_int) {
    *os_ << "  li a0, " << binary.rhs->kind.data.integer.value << "\n";
    last_result = nullptr;
  } else {
    Visit(binary.rhs);
  }
  
  if (!lhs_is_int) {
    LW(*os_, "t1", lhs_stack_pos);
  }
  
  switch (binary.op) {
    case KOOPA_RBO_ADD:
      *os_ << "  add a0, t1, a0\n";
      break;
    case KOOPA_RBO_SUB:
      *os_ << "  sub a0, t1, a0\n";
      break;
    case KOOPA_RBO_MUL:
      *os_ << "  mul a0, t1, a0\n";
      break;
    case KOOPA_RBO_DIV:
      *os_ << "  div a0, t1, a0\n";
      break;
    case KOOPA_RBO_MOD:
      *os_ << "  rem a0, t1, a0\n";
      break;
    case KOOPA_RBO_EQ:
      *os_ << "  sub a0, t1, a0\n";
      *os_ << "  seqz a0, a0\n";
      break;
    case KOOPA_RBO_NOT_EQ:
      *os_ << "  sub a0, t1, a0\n";
      *os_ << "  snez a0, a0\n";
      break;
    case KOOPA_RBO_GT:
      *os_ << "  sgt a0, t1, a0\n";
      break;
    case KOOPA_RBO_LT:
      *os_ << "  slt a0, t1, a0\n";
      break;
    case KOOPA_RBO_GE:
      *os_ << "  slt a0, t1, a0\n";
      *os_ << "  seqz a0, a0\n";
      break;
    case KOOPA_RBO_LE:
      *os_ << "  sgt a0, t1, a0\n";
      *os_ << "  seqz a0, a0\n";
      break;
    case KOOPA_RBO_AND:
      *os_ << "  and a0, t1, a0\n";
      break;
    case KOOPA_RBO_OR:
      *os_ << "  or a0, t1, a0\n";
      break;
    case KOOPA_RBO_XOR:
      *os_ << "  xor a0, t1, a0\n";
      break;
    case KOOPA_RBO_SHL:
      *os_ << "  sll a0, t1, a0\n";
      break;
    case KOOPA_RBO_SHR:
      *os_ << "  srl a0, t1, a0\n";
      break;
    case KOOPA_RBO_SAR:
      *os_ << "  sra a0, t1, a0\n";
      break;
    default:
      assert(false);
  }
  
  last_result = value;
  
  if (referenced_values.find(value) != referenced_values.end()) {
    int result_stack = stack_size;
    stack_size -= 4;
    SW(*os_, "a0", result_stack);
    value_stack[value] = result_stack;
  }
}

// Methods for our own IR format with register allocation

void RiscVGenerator::Generate(const Program &program, std::ostream &os) {
  os_ = &os;
  
  for (const auto &func : program.GetFuncs()) {
    VisitWithRegAlloc(*func);
  }
}

void RiscVGenerator::VisitWithRegAlloc(const Function &func) {
  // 收集所有需要分配寄存器的值
  std::set<int> values_used;
  std::map<int, std::vector<int>> def_positions;  // value_id -> 定义位置列表
  std::map<int, std::vector<int>> use_positions;  // value_id -> 使用位置列表
  int inst_count = 0;
  
  for (const auto &bb : func.GetBlocks()) {
    for (const auto &inst : bb->GetInsts()) {
      if (auto bin_op = dynamic_cast<const BinaryOpInst*>(inst.get())) {
        int id = bin_op->GetResultId();
        def_positions[id].push_back(inst_count);
        
        if (auto lhs = dynamic_cast<const ValueRef*>(bin_op->GetLHS())) {
          use_positions[lhs->GetId()].push_back(inst_count);
          values_used.insert(lhs->GetId());
        }
        if (auto rhs = dynamic_cast<const ValueRef*>(bin_op->GetRHS())) {
          use_positions[rhs->GetId()].push_back(inst_count);
          values_used.insert(rhs->GetId());
        }
      } else if (auto ret = dynamic_cast<const RetInst*>(inst.get())) {
        if (auto val = dynamic_cast<const ValueRef*>(ret->GetValue())) {
          use_positions[val->GetId()].push_back(inst_count);
          values_used.insert(val->GetId());
        }
      } else if (auto store = dynamic_cast<const StoreInst*>(inst.get())) {
        // Store 的 value 不是 ValueRef，而是 IntConst
      } else if (auto load = dynamic_cast<const LoadInst*>(inst.get())) {
        int id = load->GetResultId();
        def_positions[id].push_back(inst_count);
        values_used.insert(id);
        // load 的地址可能是 GetElemPtr 的结果
        values_used.insert(load->GetAddrId());
      } else if (auto gep = dynamic_cast<const GetElemPtrInst*>(inst.get())) {
        int id = gep->GetResultId();
        def_positions[id].push_back(inst_count);
        values_used.insert(id);
        // gep 的基址和偏移
        values_used.insert(gep->GetBaseAddrId());
        if (auto offset_ref = dynamic_cast<const ValueRef*>(gep->GetOffset())) {
          values_used.insert(offset_ref->GetId());
        }
      } else if (auto branch = dynamic_cast<const BranchInst*>(inst.get())) {
        if (auto cond = dynamic_cast<const ValueRef*>(branch->GetCond())) {
          use_positions[cond->GetId()].push_back(inst_count);
          values_used.insert(cond->GetId());
        }
      } else if (auto call = dynamic_cast<const CallInst*>(inst.get())) {
        int id = call->GetId();
        if (id != -1) {
          def_positions[id].push_back(inst_count);
          values_used.insert(id);
        }
        for (const auto &arg : call->GetArgs()) {
          if (auto val = dynamic_cast<const ValueRef*>(arg.get())) {
            use_positions[val->GetId()].push_back(inst_count);
            values_used.insert(val->GetId());
          }
        }
      }
      inst_count++;
    }
  }
  
  // 构建活跃区间
  RegAllocator allocator;
  for (int vid : values_used) {
    int start = INT_MAX;
    int end = -1;
    
    // 定义位置
    if (def_positions.count(vid)) {
      for (int pos : def_positions[vid]) {
        start = std::min(start, pos);
      }
    }
    
    // 使用位置
    if (use_positions.count(vid)) {
      for (int pos : use_positions[vid]) {
        end = std::max(end, pos);
      }
    }
    
    if (start <= end) {
      allocator.live_intervals.emplace_back(start, end + 1, vid);
    }
  }
  
  // 按起始位置排序
  std::sort(allocator.live_intervals.begin(), allocator.live_intervals.end(),
            [](const RegAllocator::LiveInterval& a, const RegAllocator::LiveInterval& b) {
              return a.start < b.start;
            });
  
  // 线性扫描分配寄存器
  std::vector<RegAllocator::LiveInterval> active;
  inst_count = 0;
  
  for (const auto &bb : func.GetBlocks()) {
    for (const auto &inst : bb->GetInsts()) {
      // 移除已结束的活跃区间
      active.erase(std::remove_if(active.begin(), active.end(),
        [inst_count](const RegAllocator::LiveInterval& li) {
          return li.end <= inst_count;
        }), active.end());
      
      // 为新定义的值分配寄存器
      if (auto bin_op = dynamic_cast<const BinaryOpInst*>(inst.get())) {
        int id = bin_op->GetResultId();
        allocator.AllocReg(id, inst_count);
        
        // 添加到活跃区间
        for (auto& li : allocator.live_intervals) {
          if (li.value_id == id) {
            active.push_back(li);
            break;
          }
        }
      } else if (auto load = dynamic_cast<const LoadInst*>(inst.get())) {
        int id = load->GetResultId();
        allocator.AllocReg(id, inst_count);
        
        for (auto& li : allocator.live_intervals) {
          if (li.value_id == id) {
            active.push_back(li);
            break;
          }
        }
      } else if (auto gep = dynamic_cast<const GetElemPtrInst*>(inst.get())) {
        int id = gep->GetResultId();
        allocator.AllocReg(id, inst_count);
        
        for (auto& li : allocator.live_intervals) {
          if (li.value_id == id) {
            active.push_back(li);
            break;
          }
        }
      } else if (auto call = dynamic_cast<const CallInst*>(inst.get())) {
        int id = call->GetId();
        if (id != -1) {
          allocator.AllocReg(id, inst_count);
          
          for (auto& li : allocator.live_intervals) {
            if (li.value_id == id) {
              active.push_back(li);
              break;
            }
          }
        }
      }
      
      inst_count++;
    }
  }
  
  // 生成函数代码
  *os_ << "  .text\n";
  *os_ << "  .globl " << func.GetName() << "\n";
  *os_ << func.GetName() << ":\n";
  
  // 分配栈帧
  int total_stack_size = allocator.GetStackSize();
  if (total_stack_size > 0) {
    *os_ << "  addi sp, sp, -" << total_stack_size << "\n";
  }
  
  // 生成基本块代码
  inst_count = 0;
  std::map<int, std::string> current_regs;
  
  for (const auto &bb : func.GetBlocks()) {
    *os_ << bb->GetName() << ":\n";
    
    for (const auto &inst : bb->GetInsts()) {
      // 处理每条指令
      if (auto bin_op = dynamic_cast<const BinaryOpInst*>(inst.get())) {
        VisitWithRegAlloc(*bin_op, allocator);
      } else if (auto load = dynamic_cast<const LoadInst*>(inst.get())) {
        VisitWithRegAlloc(*load, allocator);
      } else if (auto gep = dynamic_cast<const GetElemPtrInst*>(inst.get())) {
        VisitWithRegAlloc(*gep, allocator);
      } else if (auto store = dynamic_cast<const StoreInst*>(inst.get())) {
        VisitWithRegAlloc(*store, allocator);
      } else if (auto branch = dynamic_cast<const BranchInst*>(inst.get())) {
        VisitWithRegAlloc(*branch, allocator);
      } else if (auto jump = dynamic_cast<const JumpInst*>(inst.get())) {
        *os_ << "  j " << jump->GetTarget() << "\n";
      } else if (auto ret = dynamic_cast<const RetInst*>(inst.get())) {
        VisitWithRegAlloc(*ret, allocator);
      } else if (auto call = dynamic_cast<const CallInst*>(inst.get())) {
        VisitWithRegAlloc(*call, allocator);
      }
      
      inst_count++;
    }
  }
}

void RiscVGenerator::VisitWithRegAlloc(const BinaryOpInst &inst, RegAllocator &allocator) {
  std::string lhs_reg, rhs_reg, result_reg;
  
  // 获取操作数
  if (auto lhs = dynamic_cast<const IntConst*>(inst.GetLHS())) {
    *os_ << "  li t1, " << lhs->GetValue() << "\n";
    lhs_reg = "t1";
  } else if (auto lhs = dynamic_cast<const ValueRef*>(inst.GetLHS())) {
    std::string reg = allocator.reg_map[lhs->GetId()];
    if (!reg.empty() && reg != "stack") {
      lhs_reg = reg;
    } else {
      int offset = allocator.GetStackOffset(lhs->GetId());
      *os_ << "  lw t1, " << offset << "(sp)\n";
      lhs_reg = "t1";
    }
  }
  
  if (auto rhs = dynamic_cast<const IntConst*>(inst.GetRHS())) {
    *os_ << "  li t2, " << rhs->GetValue() << "\n";
    rhs_reg = "t2";
  } else if (auto rhs = dynamic_cast<const ValueRef*>(inst.GetRHS())) {
    std::string reg = allocator.reg_map[rhs->GetId()];
    if (!reg.empty() && reg != "stack") {
      rhs_reg = reg;
      // 如果和 lhs 用同一个寄存器，需要先保存
      if (rhs_reg == lhs_reg) {
        *os_ << "  lw t2, " << allocator.GetStackOffset(rhs->GetId()) << "(sp)\n";
        rhs_reg = "t2";
      }
    } else {
      int offset = allocator.GetStackOffset(rhs->GetId());
      *os_ << "  lw t2, " << offset << "(sp)\n";
      rhs_reg = "t2";
    }
  }
  
  // 获取结果寄存器
  result_reg = allocator.reg_map[inst.GetResultId()];
  if (result_reg.empty() || result_reg == "stack") {
    result_reg = "a0";
  }
  
  // 生成二进制操作
  switch (inst.GetOp()) {
    case BinaryOp::ADD:
      *os_ << "  add " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::SUB:
      *os_ << "  sub " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::MUL:
      *os_ << "  mul " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::DIV:
      *os_ << "  div " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::MOD:
      *os_ << "  rem " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::EQ:
      *os_ << "  sub " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      *os_ << "  seqz " << result_reg << ", " << result_reg << "\n";
      break;
    case BinaryOp::NE:
      *os_ << "  sub " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      *os_ << "  snez " << result_reg << ", " << result_reg << "\n";
      break;
    case BinaryOp::LT:
      *os_ << "  slt " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::GT:
      *os_ << "  sgt " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::LE:
      *os_ << "  slt t0, " << lhs_reg << ", " << rhs_reg << "\n";
      *os_ << "  seqz " << result_reg << ", t0\n";
      break;
    case BinaryOp::GE:
      *os_ << "  sgt t0, " << lhs_reg << ", " << rhs_reg << "\n";
      *os_ << "  seqz " << result_reg << ", t0\n";
      break;
    case BinaryOp::AND:
      *os_ << "  and " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    case BinaryOp::OR:
      *os_ << "  or " << result_reg << ", " << lhs_reg << ", " << rhs_reg << "\n";
      break;
    default:
      assert(false);
  }
  
  // 如果结果需要溢出到栈
  if (allocator.stack_map.count(inst.GetResultId())) {
    int offset = allocator.GetStackOffset(inst.GetResultId());
    *os_ << "  sw " << result_reg << ", " << offset << "(sp)\n";
  }
}

void RiscVGenerator::VisitWithRegAlloc(const LoadInst &inst, RegAllocator &allocator) {
  std::string result_reg = allocator.reg_map[inst.GetResultId()];
  if (result_reg.empty() || result_reg == "stack") {
    result_reg = "a0";
  }
  
  // 获取地址
  std::string addr_reg;
  if (allocator.reg_map.count(inst.GetAddrId())) {
    addr_reg = allocator.reg_map[inst.GetAddrId()];
    if (addr_reg == "stack") {
      *os_ << "  lw t0, " << allocator.GetStackOffset(inst.GetAddrId()) << "(sp)\n";
      addr_reg = "t0";
    }
  } else {
    // 直接使用栈偏移作为地址
    addr_reg = std::to_string(allocator.GetStackOffset(inst.GetAddrId())) + "(sp)";
  }
  
  if (addr_reg.find("(sp)") != std::string::npos) {
    *os_ << "  lw " << result_reg << ", " << addr_reg << "\n";
  } else {
    *os_ << "  lw " << result_reg << ", 0(" << addr_reg << ")\n";
  }
  
  // 如果结果需要溢出到栈
  if (allocator.stack_map.count(inst.GetResultId())) {
    int offset = allocator.GetStackOffset(inst.GetResultId());
    *os_ << "  sw " << result_reg << ", " << offset << "(sp)\n";
  }
}

void RiscVGenerator::VisitWithRegAlloc(const StoreInst &inst, RegAllocator &allocator) {
  std::string value_reg;
  
  if (auto val = dynamic_cast<const IntConst*>(inst.GetValue())) {
    *os_ << "  li t0, " << val->GetValue() << "\n";
    value_reg = "t0";
  } else if (auto val = dynamic_cast<const ValueRef*>(inst.GetValue())) {
    std::string reg = allocator.reg_map[val->GetId()];
    if (!reg.empty() && reg != "stack") {
      value_reg = reg;
    } else {
      int offset = allocator.GetStackOffset(val->GetId());
      *os_ << "  lw t0, " << offset << "(sp)\n";
      value_reg = "t0";
    }
  }
  
  // 获取地址
  std::string addr_reg;
  if (allocator.reg_map.count(inst.GetAddrId())) {
    addr_reg = allocator.reg_map[inst.GetAddrId()];
    if (addr_reg == "stack") {
      *os_ << "  lw t1, " << allocator.GetStackOffset(inst.GetAddrId()) << "(sp)\n";
      addr_reg = "t1";
    }
  } else {
    // 直接使用栈偏移作为地址
    addr_reg = std::to_string(allocator.GetStackOffset(inst.GetAddrId())) + "(sp)";
  }
  
  if (addr_reg.find("(sp)") != std::string::npos) {
    *os_ << "  sw " << value_reg << ", " << addr_reg << "\n";
  } else {
    *os_ << "  sw " << value_reg << ", 0(" << addr_reg << ")\n";
  }
}

void RiscVGenerator::VisitWithRegAlloc(const GetElemPtrInst &inst, RegAllocator &allocator) {
  std::string result_reg = allocator.reg_map[inst.GetResultId()];
  if (result_reg.empty() || result_reg == "stack") {
    result_reg = "t0";
  }
  
  // 获取基址
  std::string base_reg;
  if (allocator.reg_map.count(inst.GetBaseAddrId())) {
    base_reg = allocator.reg_map[inst.GetBaseAddrId()];
    if (base_reg == "stack") {
      *os_ << "  lw t1, " << allocator.GetStackOffset(inst.GetBaseAddrId()) << "(sp)\n";
      base_reg = "t1";
    }
  } else {
    base_reg = std::to_string(allocator.GetStackOffset(inst.GetBaseAddrId())) + "(sp)";
  }
  
  // 获取偏移
  if (auto offset_const = dynamic_cast<const IntConst*>(inst.GetOffset())) {
    if (base_reg.find("(sp)") != std::string::npos) {
      int offset_val = offset_const->GetValue();
      *os_ << "  addi " << result_reg << ", sp, " << allocator.GetStackOffset(inst.GetBaseAddrId()) + offset_val << "\n";
    } else {
      *os_ << "  li t2, " << offset_const->GetValue() << "\n";
      *os_ << "  add " << result_reg << ", " << base_reg << ", t2\n";
    }
  } else if (auto offset_ref = dynamic_cast<const ValueRef*>(inst.GetOffset())) {
    std::string offset_reg = allocator.reg_map[offset_ref->GetId()];
    if (offset_reg.empty() || offset_reg == "stack") {
      *os_ << "  lw t2, " << allocator.GetStackOffset(offset_ref->GetId()) << "(sp)\n";
      offset_reg = "t2";
    }
    if (base_reg.find("(sp)") != std::string::npos) {
      *os_ << "  addi t1, sp, " << allocator.GetStackOffset(inst.GetBaseAddrId()) << "\n";
      *os_ << "  add " << result_reg << ", t1, " << offset_reg << "\n";
    } else {
      *os_ << "  add " << result_reg << ", " << base_reg << ", " << offset_reg << "\n";
    }
  }
  
  // 如果结果需要溢出到栈
  if (allocator.stack_map.count(inst.GetResultId())) {
    int offset = allocator.GetStackOffset(inst.GetResultId());
    *os_ << "  sw " << result_reg << ", " << offset << "(sp)\n";
  }
}

void RiscVGenerator::VisitWithRegAlloc(const BranchInst &inst, RegAllocator &allocator) {
  std::string cond_reg;
  
  if (auto cond = dynamic_cast<const IntConst*>(inst.GetCond())) {
    *os_ << "  li a0, " << cond->GetValue() << "\n";
    cond_reg = "a0";
  } else if (auto cond = dynamic_cast<const ValueRef*>(inst.GetCond())) {
    std::string reg = allocator.reg_map[cond->GetId()];
    if (!reg.empty() && reg != "stack") {
      cond_reg = reg;
    } else {
      int offset = allocator.GetStackOffset(cond->GetId());
      *os_ << "  lw a0, " << offset << "(sp)\n";
      cond_reg = "a0";
    }
  }
  
  *os_ << "  bnez " << cond_reg << ", " << inst.GetTrueLabel() << "\n";
  *os_ << "  j " << inst.GetFalseLabel() << "\n";
}

void RiscVGenerator::VisitWithRegAlloc(const RetInst &inst, RegAllocator &allocator) {
  if (inst.GetValue()) {
    if (auto val = dynamic_cast<const IntConst*>(inst.GetValue())) {
      *os_ << "  li a0, " << val->GetValue() << "\n";
    } else if (auto val = dynamic_cast<const ValueRef*>(inst.GetValue())) {
      std::string reg = allocator.reg_map[val->GetId()];
      if (!reg.empty() && reg != "stack") {
        if (reg != "a0") {
          *os_ << "  mv a0, " << reg << "\n";
        }
      } else {
        int offset = allocator.GetStackOffset(val->GetId());
        *os_ << "  lw a0, " << offset << "(sp)\n";
      }
    }
  }
  
  // 恢复栈帧
  int total_stack_size = allocator.GetStackSize();
  if (total_stack_size > 0) {
    *os_ << "  addi sp, sp, " << total_stack_size << "\n";
  }
  
  *os_ << "  ret\n";
}

void RiscVGenerator::VisitWithRegAlloc(const CallInst &inst, RegAllocator &allocator) {
  // 函数调用前保存 caller-saved 寄存器
  for (size_t i = 0; i < inst.GetArgs().size(); ++i) {
    const auto &arg = inst.GetArgs()[i];
    if (auto val = dynamic_cast<const IntConst*>(arg.get())) {
      *os_ << "  li a" << i << ", " << val->GetValue() << "\n";
    } else if (auto val = dynamic_cast<const ValueRef*>(arg.get())) {
      std::string reg = allocator.reg_map[val->GetId()];
      if (!reg.empty() && reg != "stack") {
        *os_ << "  mv a" << i << ", " << reg << "\n";
      } else {
        int offset = allocator.GetStackOffset(val->GetId());
        *os_ << "  lw a" << i << ", " << offset << "(sp)\n";
      }
    }
  }
  
  *os_ << "  call " << inst.GetFuncName() << "\n";
  
  // 如果有返回值且需要保存
  if (!inst.IsVoid()) {
    std::string result_reg = allocator.reg_map[inst.GetId()];
    if (!result_reg.empty() && result_reg != "stack" && result_reg != "a0") {
      *os_ << "  mv " << result_reg << ", a0\n";
    }
    
    if (allocator.stack_map.count(inst.GetId())) {
      int offset = allocator.GetStackOffset(inst.GetId());
      *os_ << "  sw a0, " << offset << "(sp)\n";
    }
  }
}

// Legacy methods (kept for compatibility)
void RiscVGenerator::Visit(const Function &func) {
  VisitWithRegAlloc(func);
}

void RiscVGenerator::Visit(const BasicBlock &bb) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const AllocInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const StoreInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const LoadInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const BinaryOpInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const UnaryOpInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const BranchInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const JumpInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const RetInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const CallInst &inst) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const ValueRef &value) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::Visit(const IntConst &value) {
  // This is kept for compatibility but not used
}

void RiscVGenerator::VisitGetPtr(const koopa_raw_get_ptr_t &get_ptr, const koopa_raw_value_t &value) {
  auto src_it = value_stack.find(get_ptr.src);
  auto global_it = global_alloc_names.find(get_ptr.src);
  bool src_is_alloc = (get_ptr.src->kind.tag == KOOPA_RVT_ALLOC);
  
  if (src_it != value_stack.end() && src_is_alloc) {
    int offset = src_it->second;
    if (offset >= -2048 && offset <= 2047) {
      *os_ << "  addi t1, sp, " << offset << "\n";
    } else {
      *os_ << "  li t1, " << offset << "\n";
      *os_ << "  add t1, sp, t1\n";
    }
  } else if (src_it != value_stack.end()) {
    LW(*os_, "t1", src_it->second);
  } else if (global_it != global_alloc_names.end()) {
    *os_ << "  la t1, " << global_it->second << "\n";
  } else {
    Visit(get_ptr.src);
    *os_ << "  mv t1, a0\n";
  }
  
  Visit(get_ptr.index);
  
  *os_ << "  li t2, 4\n";
  *os_ << "  mul a0, a0, t2\n";
  *os_ << "  add a0, t1, a0\n";
  
  last_result = value;
  
  if (referenced_values.find(value) != referenced_values.end()) {
    int result_stack = stack_size;
    stack_size -= 4;
    SW(*os_, "a0", result_stack);
    value_stack[value] = result_stack;
  }
}

void RiscVGenerator::VisitGetElemPtr(const koopa_raw_get_elem_ptr_t &get_elem_ptr, const koopa_raw_value_t &value) {
  auto src_it = value_stack.find(get_elem_ptr.src);
  auto global_it = global_alloc_names.find(get_elem_ptr.src);
  bool src_is_alloc = (get_elem_ptr.src->kind.tag == KOOPA_RVT_ALLOC);
  
  if (src_it != value_stack.end() && src_is_alloc) {
    int offset = src_it->second;
    if (offset >= -2048 && offset <= 2047) {
      *os_ << "  addi t1, sp, " << offset << "\n";
    } else {
      *os_ << "  li t1, " << offset << "\n";
      *os_ << "  add t1, sp, t1\n";
    }
  } else if (src_it != value_stack.end()) {
    LW(*os_, "t1", src_it->second);
  } else if (global_it != global_alloc_names.end()) {
    *os_ << "  la t1, " << global_it->second << "\n";
  } else {
    Visit(get_elem_ptr.src);
    *os_ << "  mv t1, a0\n";
  }
  
  Visit(get_elem_ptr.index);
  
  *os_ << "  li t2, 4\n";
  *os_ << "  mul a0, a0, t2\n";
  *os_ << "  add a0, t1, a0\n";
  
  last_result = value;
  
  if (referenced_values.find(value) != referenced_values.end()) {
    int result_stack = stack_size;
    stack_size -= 4;
    SW(*os_, "a0", result_stack);
    value_stack[value] = result_stack;
  }
}

void RiscVGenerator::Visit(const koopa_raw_call_t &call, const koopa_raw_value_t &value) {
  for (size_t i = 0; i < call.args.len; ++i) {
    auto arg = static_cast<koopa_raw_value_t>(call.args.buffer[i]);
    Visit(arg);
    
    if (i == 0 && arg != last_result) {
      // Already in a0
    } else if (i < 8) {
      *os_ << "  mv a" << i << ", a0\n";
    }
  }
  
  *os_ << "  call " << call.callee->name + 1 << "\n";
  
  last_result = value;
  
  if (referenced_values.find(value) != referenced_values.end()) {
    int result_stack = stack_size;
    stack_size -= 4;
    SW(*os_, "a0", result_stack);
    value_stack[value] = result_stack;
  }
}
