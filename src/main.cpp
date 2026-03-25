#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "ast.h"
#include "ir.h"
#include "riscv.h"

extern "C" {
#include <koopa.h>
}

using namespace std;

extern FILE *yyin;
extern int yyparse(unique_ptr<BaseAST> &ast);

int main(int argc, const char *argv[]) {
  assert(argc == 5);
  auto mode = argv[1];
  auto input = argv[2];
  auto output = argv[4];

  yyin = fopen(input, "r");
  assert(yyin);

  unique_ptr<BaseAST> ast;
  auto ret = yyparse(ast);
  assert(!ret);

  auto comp_unit = static_cast<CompUnitAST*>(ast.get());
  auto program = comp_unit->GenIR();

  FILE *out_file = fopen(output, "w");
  assert(out_file);

  std::ostringstream out_stream;

  if (string(mode) == "-koopa") {
    program->Dump(out_stream);
  } else if (string(mode) == "-riscv") {
    std::string ir_str = program->ToString();
    
    koopa_program_t koopa_prog;
    koopa_error_code_t err = koopa_parse_from_string(ir_str.c_str(), &koopa_prog);
    assert(err == KOOPA_EC_SUCCESS);
    
    koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
    koopa_raw_program_t raw = koopa_build_raw_program(builder, koopa_prog);
    
    RiscVGenerator generator;
    generator.Generate(raw, out_stream);
    
    koopa_delete_raw_program_builder(builder);
    koopa_delete_program(koopa_prog);
  } else {
    ast->Dump();
  }

  fprintf(out_file, "%s", out_stream.str().c_str());
  fclose(out_file);

  return 0;
}
