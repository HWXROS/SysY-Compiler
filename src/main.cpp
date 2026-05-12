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

using namespace std;

extern FILE *yyin;
extern int yyparse(unique_ptr<BaseAST> &ast);
extern BaseAST* g_parse_result;

int main(int argc, const char *argv[]) {
  assert(argc == 5);
  auto mode = argv[1];
  auto input = argv[2];
  auto output = argv[4];

  FILE *out_file = fopen(output, "w");
  assert(out_file);

  std::ostringstream out_stream;

  if (string(mode) == "-koopa") {
    yyin = fopen(input, "r");
    assert(yyin);

    unique_ptr<BaseAST> ast;
    auto ret = yyparse(ast);
    assert(!ret);

    // Get the parse result from the global variable
    if (g_parse_result) {
      ast = unique_ptr<BaseAST>(g_parse_result);
      g_parse_result = nullptr; // Reset the global variable
    }

    if (!ast) {
      std::cerr << "Error: yyparse returned success but AST is null!" << std::endl;
      return 1;
    }
    auto comp_unit = static_cast<CompUnitAST*>(ast.get());
    auto program = comp_unit->GenIR();
    program->Dump(out_stream);
  } else if (string(mode) == "-riscv") {
    std::string input_str(input);
    std::string koopa_str;
    const char* koopa_input = input;
    
    // 检测输入类型：如果是 .c 文件，先生成 Koopa IR
    if (input_str.size() < 6 || input_str.substr(input_str.size() - 6) != ".koopa") {
      yyin = fopen(input, "r");
      assert(yyin);

      unique_ptr<BaseAST> ast;
      auto pret = yyparse(ast);
      assert(!pret);

      if (g_parse_result) {
        ast = unique_ptr<BaseAST>(g_parse_result);
        g_parse_result = nullptr;
      }

      if (!ast) {
        std::cerr << "Error: yyparse returned success but AST is null!" << std::endl;
        return 1;
      }
      auto comp_unit = static_cast<CompUnitAST*>(ast.get());
      auto program = comp_unit->GenIR();
      std::ostringstream koosa_ss;
      program->Dump(koosa_ss);
      koopa_str = koosa_ss.str();
      koopa_input = koopa_str.c_str();
    }
    
    koopa_program_t program;
    auto ret = koopa_parse_from_string(koopa_input, &program);
    assert(ret == 0);

    koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
    koopa_raw_program_t raw = koopa_build_raw_program(builder, program);
    
    RiscVGenerator generator;
    generator.Generate(raw, out_stream);
    
    koopa_delete_raw_program_builder(builder);
    koopa_delete_program(program);
  } else {
    yyin = fopen(input, "r");
    assert(yyin);

    unique_ptr<BaseAST> ast;
    auto ret = yyparse(ast);
    assert(!ret);

    // Get the parse result from the global variable
    if (g_parse_result) {
      ast = unique_ptr<BaseAST>(g_parse_result);
      g_parse_result = nullptr; // Reset the global variable
    }

    if (!ast) {
      std::cerr << "Error: yyparse returned success but AST is null!" << std::endl;
      return 1;
    }
    ast->Dump();
  }

  fprintf(out_file, "%s", out_stream.str().c_str());
  fclose(out_file);

  return 0;
}
