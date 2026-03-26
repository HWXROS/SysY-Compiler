#include "riscv.h"
#include <cassert>
#include <map>
#include <set>

static std::map<const koopa_raw_value_data*, int> value_stack;
static std::set<const koopa_raw_value_data*> referenced_values;
static int stack_size = 0;
static const koopa_raw_value_data* last_result = nullptr;

static void CollectReferences(const koopa_raw_program_t &program) {
  for (size_t i = 0; i < program.funcs.len; ++i) {
    auto func = reinterpret_cast<koopa_raw_function_t>(program.funcs.buffer[i]);
    if (func->bbs.len == 0) continue;
    
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
  
  CollectReferences(program);
  
  Visit(program.values);
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
  
  *os_ << "  .text\n";
  *os_ << "  .globl " << func->name + 1 << "\n";
  *os_ << func->name + 1 << ":\n";
  
  Visit(func->bbs);
}

void RiscVGenerator::Visit(const koopa_raw_basic_block_t &bb) {
  Visit(bb->insts);
}

void RiscVGenerator::Visit(const koopa_raw_value_t &value) {
  if (value == last_result) {
    return;
  }
  
  auto it = value_stack.find(value);
  if (it != value_stack.end()) {
    *os_ << "  lw a0, " << it->second << "(sp)\n";
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
    case KOOPA_RVT_ALLOC:
      VisitAlloc(value);
      break;
    case KOOPA_RVT_STORE:
      Visit(kind.data.store);
      break;
    case KOOPA_RVT_LOAD:
      Visit(kind.data.load, value);
      break;
    default:
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
  int addr = stack_size;
  stack_size -= 4;
  value_stack[value] = addr;
  last_result = nullptr;
}

void RiscVGenerator::Visit(const koopa_raw_store_t &store) {
  Visit(store.value);
  
  auto addr_value = store.dest;
  auto it = value_stack.find(addr_value);
  if (it != value_stack.end()) {
    *os_ << "  sw a0, " << it->second << "(sp)\n";
  }
  last_result = nullptr;
}

void RiscVGenerator::Visit(const koopa_raw_load_t &load, const koopa_raw_value_t &value) {
  auto addr_value = load.src;
  auto it = value_stack.find(addr_value);
  if (it != value_stack.end()) {
    *os_ << "  lw a0, " << it->second << "(sp)\n";
  }
  
  last_result = value;
  
  if (referenced_values.find(value) != referenced_values.end()) {
    int result_stack = stack_size;
    stack_size -= 4;
    *os_ << "  sw a0, " << result_stack << "(sp)\n";
    value_stack[value] = result_stack;
  }
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
    *os_ << "  sw a0, " << lhs_stack_pos << "(sp)\n";
    last_result = nullptr;
  }
  
  if (rhs_is_int) {
    *os_ << "  li a0, " << binary.rhs->kind.data.integer.value << "\n";
    last_result = nullptr;
  } else {
    Visit(binary.rhs);
  }
  
  if (!lhs_is_int) {
    *os_ << "  lw t1, " << lhs_stack_pos << "(sp)\n";
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
    *os_ << "  sw a0, " << result_stack << "(sp)\n";
    value_stack[value] = result_stack;
  }
}
