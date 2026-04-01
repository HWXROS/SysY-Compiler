#include "ast.h"

thread_local Function* g_current_func = nullptr;
thread_local BasicBlock* g_current_bb = nullptr;
thread_local std::vector<BasicBlock*> g_end_block_stack;

std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  auto func = static_cast<FuncDefAST*>(func_def.get());
  program->AddFunc(func->GenIR());
  return program;
}

std::unique_ptr<Function> FuncDefAST::GenIR() const {
  auto func = std::make_unique<Function>(ident, "i32");
  g_current_func = func.get();
  auto block = static_cast<BlockAST*>(this->block.get());
  SymbolTable symtab;
  IRBuilder builder;
  auto entry_bb = std::make_unique<BasicBlock>("entry");
  g_current_bb = entry_bb.get();
  func->AddBlock(std::move(entry_bb));
  block->GenIR(g_current_bb, builder, symtab);
  g_current_func = nullptr;
  g_current_bb = nullptr;
  return func;
}

std::unique_ptr<BasicBlock> BlockAST::GenIR(IRBuilder &builder, SymbolTable &symtab) const {
  auto bb = std::make_unique<BasicBlock>("entry");
  SymbolTable new_symtab(&symtab);
  for (const auto &item : items) {
    item->GenIR(bb.get(), builder, new_symtab);
  }
  return bb;
}

std::unique_ptr<KoopaValue> BlockAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  SymbolTable new_symtab(&symtab);
  for (const auto &item : items) {
    if (g_current_bb && g_current_bb->HasTerminator()) {
      break;
    }
    if (g_current_bb) {
      item->GenIR(g_current_bb, builder, new_symtab);
    } else {
      item->GenIR(bb, builder, new_symtab);
    }
  }
  
  if (g_current_bb && !g_current_bb->HasTerminator()) {
    if (!g_end_block_stack.empty()) {
      BasicBlock* outer_end = g_end_block_stack.back();
      if (g_current_bb != outer_end) {
        g_current_bb->AddInst(std::make_unique<JumpInst>(outer_end->GetName()));
      }
    }
  }
  
  return nullptr;
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
    if (exp) {
      auto exp_val = exp->GenIR(bb, builder, symtab);
      bb->AddInst(std::make_unique<RetInst>(std::move(exp_val)));
    } else {
      bb->AddInst(std::make_unique<RetInst>(nullptr));
    }
  } else if (type == StmtType::ASSIGN) {
    auto lval_ptr = static_cast<LValAST*>(lval.get());
    auto exp_val = exp->GenIR(bb, builder, symtab);
    int addr_id = symtab.GetVarAddr(lval_ptr->ident);
    bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
  } else if (type == StmtType::EXPR) {
    // 表达式语句：计算表达式但丢弃结果
    if (exp) {
      exp->GenIR(bb, builder, symtab);
    }
  } else if (type == StmtType::EMPTY) {
    // 空语句：什么都不做
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

std::unique_ptr<KoopaValue> IfStmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  BasicBlock* current_bb = g_current_bb ? g_current_bb : bb;
  
  auto cond_val = cond->GenIR(current_bb, builder, symtab);
  
  int id = builder.NewId();
  std::string then_label = "b" + std::to_string(id * 3 + 1);
  std::string else_label = "b" + std::to_string(id * 3 + 2);
  std::string end_label = "b" + std::to_string(id * 3 + 3);
  
  BasicBlock* then_bb = g_current_func->CreateBlock(then_label);
  
  BasicBlock* end_bb = nullptr;
  BasicBlock* else_bb = nullptr;
  
  bool pushed_to_stack = false;
  
  if (else_stmt) {
    else_bb = g_current_func->CreateBlock(else_label);
    end_bb = g_current_func->CreateBlock(end_label);
    g_end_block_stack.push_back(end_bb);
    pushed_to_stack = true;
  } else {
    if (g_end_block_stack.empty()) {
      end_bb = g_current_func->CreateBlock(end_label);
      g_end_block_stack.push_back(end_bb);
      pushed_to_stack = true;
    } else {
      end_bb = g_end_block_stack.back();
    }
  }
  
  g_current_bb = then_bb;
  then_stmt->GenIR(then_bb, builder, symtab);
  
  BasicBlock* then_last_bb = g_current_bb;
  bool then_has_ret = then_bb->HasTerminator();
  if (!then_has_ret && then_last_bb && then_last_bb != then_bb) {
    then_has_ret = then_last_bb->HasTerminator();
  }
  
  if (else_stmt) {
    g_current_bb = else_bb;
    else_stmt->GenIR(else_bb, builder, symtab);
    
    BasicBlock* else_last_bb = g_current_bb;
    bool else_has_ret = else_bb->HasTerminator();
    if (!else_has_ret && else_last_bb && else_last_bb != else_bb) {
      else_has_ret = else_last_bb->HasTerminator();
    }
    
    current_bb->AddInst(std::make_unique<BranchInst>(std::move(cond_val), then_label, else_label));
    
    if (!then_has_ret) {
      if (then_last_bb && then_last_bb != end_bb && !then_last_bb->HasTerminator()) {
        then_last_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      } else if (then_bb != end_bb && !then_bb->HasTerminator()) {
        then_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      }
    }
    if (!else_has_ret) {
      if (else_last_bb && else_last_bb != end_bb && !else_last_bb->HasTerminator()) {
        else_last_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      } else if (else_bb != end_bb && !else_bb->HasTerminator()) {
        else_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      }
    }
    
    g_current_bb = end_bb;
  } else {
    current_bb->AddInst(std::make_unique<BranchInst>(std::move(cond_val), then_label, end_bb->GetName()));
    
    if (!then_has_ret) {
      if (then_last_bb && then_last_bb != end_bb && !then_last_bb->HasTerminator()) {
        then_last_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      } else if (then_bb != end_bb && !then_bb->HasTerminator()) {
        then_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      }
    }
    
    g_current_bb = end_bb;
  }
  
  if (pushed_to_stack) {
    g_end_block_stack.pop_back();
  }
  
  return nullptr;
}
