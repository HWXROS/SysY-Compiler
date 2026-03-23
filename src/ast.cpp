#include "ast.h"

std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  auto func = static_cast<FuncDefAST*>(func_def.get());
  program->AddFunc(func->GenIR());
  return program;
}

std::unique_ptr<Function> FuncDefAST::GenIR() const {
  auto func = std::make_unique<Function>(ident, "i32");
  auto block = static_cast<BlockAST*>(this->block.get());
  func->AddBlock(block->GenIR());
  return func;
}

std::unique_ptr<BasicBlock> BlockAST::GenIR() const {
  auto bb = std::make_unique<BasicBlock>("entry");
  auto stmt = static_cast<StmtAST*>(this->stmt.get());
  bb->AddInst(stmt->GenIR());
  return bb;
}

std::unique_ptr<KoopaValue> StmtAST::GenIR() const {
  auto num = static_cast<NumberAST*>(number.get());
  return std::make_unique<RetInst>(num->GenIR());
}

std::unique_ptr<KoopaValue> NumberAST::GenIR() const {
  return std::make_unique<IntConst>(value);
}
