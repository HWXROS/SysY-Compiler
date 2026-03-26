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
  SymbolTable symtab;
  func->AddBlock(block->GenIR(symtab));
  return func;
}

std::unique_ptr<BasicBlock> BlockAST::GenIR(SymbolTable &symtab) const {
  auto bb = std::make_unique<BasicBlock>("entry");
  IRBuilder builder;
  for (const auto &item : items) {
    item->GenIR(bb.get(), builder, symtab);
  }
  return bb;
}

std::unique_ptr<KoopaValue> ConstDeclAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  for (const auto &def : const_defs) {
    def->GenIR(bb, builder, symtab);
  }
  return nullptr;
}

std::unique_ptr<KoopaValue> ConstDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  auto val = init_val->GenIR(bb, builder, symtab);
  int const_val = 0;
  if (val->IsConst()) {
    const_val = val->GetConstValue();
  } else {
    const_val = 0;
  }
  symtab.const_values[ident] = const_val;
  return nullptr;
}

std::unique_ptr<KoopaValue> VarDeclAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  for (const auto &def : var_defs) {
    def->GenIR(bb, builder, symtab);
  }
  return nullptr;
}

std::unique_ptr<KoopaValue> VarDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  int addr_id = builder.NewId();
  bb->AddInst(std::make_unique<AllocInst>(addr_id));
  symtab.var_addrs[ident] = addr_id;
  
  if (has_init) {
    auto val = init_val->GenIR(bb, builder, symtab);
    bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id));
  }
  return nullptr;
}

std::unique_ptr<KoopaValue> StmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (type == StmtType::RETURN) {
    auto exp_val = exp->GenIR(bb, builder, symtab);
    bb->AddInst(std::make_unique<RetInst>(std::move(exp_val)));
  } else if (type == StmtType::ASSIGN) {
    auto lval_ptr = static_cast<LValAST*>(lval.get());
    auto exp_val = exp->GenIR(bb, builder, symtab);
    int addr_id = symtab.GetVarAddr(lval_ptr->ident);
    bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
  }
  return nullptr;
}

std::unique_ptr<KoopaValue> LValAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (symtab.IsConst(ident)) {
    return std::make_unique<IntConst>(symtab.GetConstValue(ident));
  } else if (symtab.IsVar(ident)) {
    int addr_id = symtab.GetVarAddr(ident);
    int id = builder.NewId();
    bb->AddInst(std::make_unique<LoadInst>(id, addr_id));
    return std::make_unique<ValueRef>(id);
  }
  return std::make_unique<IntConst>(0);
}

std::unique_ptr<KoopaValue> NumberAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  return std::make_unique<IntConst>(value);
}

std::unique_ptr<KoopaValue> UnaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  auto operand = exp->GenIR(bb, builder, symtab);
  
  if (op == '+') {
    return operand;
  }
  
  if (operand->IsConst()) {
    int val = operand->GetConstValue();
    if (op == '-') {
      return std::make_unique<IntConst>(-val);
    } else if (op == '!') {
      return std::make_unique<IntConst>(val == 0 ? 1 : 0);
    }
  }
  
  int id = builder.NewId();
  bb->AddInst(std::make_unique<UnaryOpInst>(id, op, std::move(operand)));
  return std::make_unique<ValueRef>(id);
}

std::unique_ptr<KoopaValue> BinaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  auto lhs = left->GenIR(bb, builder, symtab);
  auto rhs = right->GenIR(bb, builder, symtab);
  
  if (lhs->IsConst() && rhs->IsConst()) {
    int l = lhs->GetConstValue();
    int r = rhs->GetConstValue();
    int result = 0;
    switch (op) {
      case '+': result = l + r; break;
      case '-': result = l - r; break;
      case '*': result = l * r; break;
      case '/': result = l / r; break;
      case '%': result = l % r; break;
      case '<': result = l < r ? 1 : 0; break;
      case '>': result = l > r ? 1 : 0; break;
      case 'L': result = l <= r ? 1 : 0; break;
      case 'G': result = l >= r ? 1 : 0; break;
      case 'E': result = l == r ? 1 : 0; break;
      case 'N': result = l != r ? 1 : 0; break;
      case '&': result = l && r ? 1 : 0; break;
      case '|': result = l || r ? 1 : 0; break;
    }
    return std::make_unique<IntConst>(result);
  }
  
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
