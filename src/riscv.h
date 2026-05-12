#pragma once

#include <iostream>
#include <fstream>
#include "ir.h"

extern "C" {
#include <koopa.h>
}

class RiscVGenerator {
 public:
  void Generate(const koopa_raw_program_t &program, std::ostream &os);
  void Generate(const Program &program, std::ostream &os);
  
 private:
  std::ostream *os_;
  
  void Visit(const koopa_raw_slice_t &slice);
  void Visit(const koopa_raw_function_t &func);
  void Visit(const koopa_raw_basic_block_t &bb);
  void Visit(const koopa_raw_value_t &value);
  void Visit(const koopa_raw_return_t &ret);
  void Visit(const koopa_raw_integer_t &integer);
  void Visit(const koopa_raw_binary_t &binary, const koopa_raw_value_t &value);
  void Visit(const koopa_raw_store_t &store);
  void Visit(const koopa_raw_load_t &load, const koopa_raw_value_t &value);
  void VisitAlloc(const koopa_raw_value_t &value);
  void Visit(const koopa_raw_branch_t &branch);
  void Visit(const koopa_raw_jump_t &jump);
  void VisitGetPtr(const koopa_raw_get_ptr_t &get_ptr, const koopa_raw_value_t &value);
  void VisitGetElemPtr(const koopa_raw_get_elem_ptr_t &get_elem_ptr, const koopa_raw_value_t &value);
  void Visit(const koopa_raw_call_t &call, const koopa_raw_value_t &value);
  
  // Methods for our own IR format with register allocation
  void VisitWithRegAlloc(const Function &func);
  void VisitWithRegAlloc(const BinaryOpInst &inst, class RegAllocator &allocator);
  void VisitWithRegAlloc(const LoadInst &inst, class RegAllocator &allocator);
  void VisitWithRegAlloc(const StoreInst &inst, class RegAllocator &allocator);
  void VisitWithRegAlloc(const GetElemPtrInst &inst, class RegAllocator &allocator);
  void VisitWithRegAlloc(const BranchInst &inst, class RegAllocator &allocator);
  void VisitWithRegAlloc(const RetInst &inst, class RegAllocator &allocator);
  void VisitWithRegAlloc(const CallInst &inst, class RegAllocator &allocator);
  
  // Legacy methods (kept for compatibility)
  void Visit(const Function &func);
  void Visit(const BasicBlock &bb);
  void Visit(const AllocInst &inst);
  void Visit(const StoreInst &inst);
  void Visit(const LoadInst &inst);
  void Visit(const BinaryOpInst &inst);
  void Visit(const UnaryOpInst &inst);
  void Visit(const BranchInst &inst);
  void Visit(const JumpInst &inst);
  void Visit(const RetInst &inst);
  void Visit(const CallInst &inst);
  void Visit(const IntConst &value);
  void Visit(const ValueRef &value);
};
