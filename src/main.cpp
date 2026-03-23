#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "ast.h"
#include "ir.h"

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

  std::ostringstream oss;
  std::streambuf* old_buf = std::cout.rdbuf();
  std::cout.rdbuf(oss.rdbuf());

  if (string(mode) == "-koopa") {
    program->Dump();
  } else if (string(mode) == "-riscv") {
    program->DumpRiscV();
  } else {
    ast->Dump();
  }

  std::cout.rdbuf(old_buf);
  fprintf(out_file, "%s", oss.str().c_str());
  fclose(out_file);

  return 0;
}
