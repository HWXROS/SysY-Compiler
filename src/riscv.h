#pragma once

#include <iostream>
#include <fstream>

extern "C" {
#include <koopa.h>
}

class RiscVGenerator {
 public:
  void Generate(const koopa_raw_program_t &program, std::ostream &os);
  
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
};
