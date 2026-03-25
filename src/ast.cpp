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
  IRBuilder builder;
  auto stmt = static_cast<StmtAST*>(this->stmt.get());
  stmt->GenIR(bb.get(), builder);
  return bb;
}

std::unique_ptr<KoopaValue> StmtAST::GenIR(BasicBlock *bb, IRBuilder &builder) const {
  auto exp_val = exp->GenIR(bb, builder);
  bb->AddInst(std::make_unique<RetInst>(std::move(exp_val)));
  return nullptr;
}

std::unique_ptr<KoopaValue> NumberAST::GenIR(BasicBlock *bb, IRBuilder &builder) const {
  return std::make_unique<IntConst>(value);
}

std::unique_ptr<KoopaValue> UnaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder) const {
  auto operand = exp->GenIR(bb, builder);
  
  if (op == '+') {
    return operand;
  }
  
  int id = builder.NewId();
  bb->AddInst(std::make_unique<UnaryOpInst>(id, op, std::move(operand)));
  return std::make_unique<ValueRef>(id);
}

std::unique_ptr<KoopaValue> BinaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder) const {
  auto lhs = left->GenIR(bb, builder);
  auto rhs = right->GenIR(bb, builder);
  
  if (op == '&' || op == '|') {
    int id1 = builder.NewId();
    bb->AddInst(std::make_unique<BinaryOpInst>(id1, 'N', std::move(lhs), std::make_unique<IntConst>(0)));
    
    int id2 = builder.NewId();
    bb->AddInst(std::make_unique<BinaryOpInst>(id2, 'N', std::move(rhs), std::make_unique<IntConst>(0)));
    
    int id3 = builder.NewId();
    bb->AddInst(std::make_unique<BinaryOpInst>(id3, op, std::make_unique<ValueRef>(id1), std::make_unique<ValueRef>(id2)));
    
    return std::make_unique<ValueRef>(id3);
  }
  
  int id = builder.NewId();
  bb->AddInst(std::make_unique<BinaryOpInst>(id, op, std::move(lhs), std::move(rhs)));
  return std::make_unique<ValueRef>(id);
}
