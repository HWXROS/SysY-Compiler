#include "ast.h"
#include <set>

thread_local Function* g_current_func = nullptr;
thread_local BasicBlock* g_current_bb = nullptr;
thread_local std::vector<BasicBlock*> g_end_block_stack;
thread_local std::vector<LoopContext> g_loop_stack;
thread_local int g_loop_depth = 0;
thread_local std::set<BasicBlock*> g_protected_blocks;

std::unique_ptr<Program> CompUnitAST::GenIR() const {
  std::cerr << "CompUnitAST::GenIR() called" << std::endl;
  std::cerr << "items.size() = " << items.size() << std::endl;
  auto program = std::make_unique<Program>();
  
  program->AddDecl("getint", "i32", false);
  program->AddDecl("putint", "i32", true);
  program->AddDecl("putch", "i32", true);
  program->AddDecl("getarray", "i32", false);
  program->AddDecl("putarray", "i32", true);
  program->AddDecl("starttime", "i32", true);
  program->AddDecl("stoptime", "i32", true);
  
  SymbolTable global_symtab;
  global_symtab.func_is_void["putint"] = true;
  global_symtab.func_is_void["putch"] = true;
  global_symtab.func_is_void["getint"] = false;
  global_symtab.func_is_void["getarray"] = false;
  global_symtab.func_is_void["putarray"] = true;
  for (size_t i = 0; i < items.size(); ++i) {
    std::cerr << "Processing item " << i << std::endl;
    const auto& item = items[i];
    if (item == nullptr) {
      std::cerr << "  item is nullptr!" << std::endl;
      continue;
    }
    if (auto* func_def = dynamic_cast<FuncDefAST*>(item.get())) {
      std::cerr << "  FuncDef: " << func_def->ident << std::endl;
      program->AddFunc(func_def->GenIR(global_symtab));
    } else if (auto* var_decl = dynamic_cast<VarDeclAST*>(item.get())) {
      std::cerr << "  VarDecl" << std::endl;
      IRBuilder builder;
      var_decl->GenIR(nullptr, builder, global_symtab, program.get());
    } else if (auto* const_decl = dynamic_cast<ConstDeclAST*>(item.get())) {
      std::cerr << "  ConstDecl" << std::endl;
      IRBuilder builder;
      const_decl->GenIR(nullptr, builder, global_symtab);
    } else {
      std::cerr << "  Unknown type" << std::endl;
    }
  }
  return program;
}

std::unique_ptr<Function> FuncDefAST::GenIR(SymbolTable &global_symtab) const {
  auto func_type_ast = static_cast<FuncTypeAST*>(func_type.get());
  bool is_void = func_type_ast->is_void;
  global_symtab.func_is_void[ident] = is_void;
  
  std::vector<bool> param_is_array_list;
  for (size_t i = 0; i < params.size(); ++i) {
    auto param_ast = static_cast<FuncFParamAST*>(params[i].get());
    param_is_array_list.push_back(param_ast->is_array);
  }
  global_symtab.SetFuncParamArray(ident, param_is_array_list);
  
  auto func = std::make_unique<Function>(ident, "i32", is_void);
  g_current_func = func.get();
  auto block_ast = static_cast<BlockAST*>(this->block.get());
  SymbolTable symtab(&global_symtab);
  IRBuilder builder;
  auto entry_bb = std::make_unique<BasicBlock>("entry");
  BasicBlock* entry_bb_ptr = entry_bb.get();
  g_current_bb = entry_bb_ptr;
  func->AddBlock(std::move(entry_bb));
  for (size_t i = 0; i < params.size(); ++i) {
    auto param_ast = static_cast<FuncFParamAST*>(params[i].get());
    std::string param_type = param_ast->is_array ? "*i32" : "i32";
    std::string param_name = param_ast->ident;
    // 避免与全局变量同名冲突
    if (global_symtab.IsVar(param_name) || global_symtab.IsConst(param_name) || global_symtab.IsGlobal(param_name)) {
      param_name = param_name + "_p" + std::to_string(i);
    }
    func->AddParam(param_name, param_type);
    
    if (param_ast->is_array) {
      symtab.is_array[param_ast->ident] = true;
      symtab.array_dims[param_ast->ident] = {};
      symtab.var_addrs[param_ast->ident] = -1;
      symtab.is_global[param_ast->ident] = true;
      // 存储重命名后的引用名
      symtab.rename_map[param_ast->ident] = param_name;
    } else {
      int addr_id = builder.NewId();
      entry_bb_ptr->AddInst(std::make_unique<AllocInst>(addr_id));
      entry_bb_ptr->AddInst(std::make_unique<StoreInst>(
        std::make_unique<ValueRef>("@" + param_name),
        addr_id
      ));
      symtab.var_addrs[param_ast->ident] = addr_id;
      symtab.is_global[param_ast->ident] = false;
    }
  }
  block_ast->GenIR(entry_bb_ptr, builder, symtab);
  
  if (!is_void && !func->GetBlock(func->GetBlockCount() - 1)->HasTerminator()) {
    func->GetBlock(func->GetBlockCount() - 1)->AddInst(std::make_unique<RetInst>(std::make_unique<IntConst>(0)));
  }
  
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

std::unique_ptr<KoopaValue> BlockAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
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
  
  if (g_current_bb && !g_current_bb->HasTerminator() && g_loop_depth == 0) {
    if (!g_end_block_stack.empty()) {
      BasicBlock* outer_end = g_end_block_stack.back();
      if (g_current_bb != outer_end) {
        g_current_bb->AddInst(std::make_unique<JumpInst>(outer_end->GetName()));
      }
    }
  }
  
  return nullptr;
}

std::unique_ptr<KoopaValue> ConstDeclAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  for (const auto &def : const_defs) {
    def->GenIR(bb, builder, symtab);
  }
  return nullptr;
}

std::unique_ptr<KoopaValue> ConstDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
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

std::unique_ptr<KoopaValue> VarDeclAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  for (const auto &def : var_defs) {
    def->GenIR(bb, builder, symtab, program);
  }
  return nullptr;
}

std::unique_ptr<KoopaValue> VarDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  std::string alloc_type = "i32";
  
  if (!dims.empty()) {
    symtab.is_array[ident] = true;
    std::vector<int> computed_dims;
    int total_size = 1;
    for (size_t i = 0; i < dim_exps.size(); ++i) {
      auto dim_val = dim_exps[i]->GenIR(bb, builder, symtab);
      int dim_size = dim_val->IsConst() ? dim_val->GetConstValue() : 0;
      computed_dims.push_back(dim_size);
      total_size *= dim_size;
    }
    symtab.array_dims[ident] = computed_dims;

    alloc_type = "i32";
  }

  if (bb == nullptr && program != nullptr) {
    symtab.is_global[ident] = true;
    
    std::unique_ptr<KoopaValue> init_value = nullptr;
    if (has_init) {
      init_value = init_val->GenIR(bb, builder, symtab);
    } else {
      init_value = std::make_unique<IntConst>(0);
    }
    
    program->AddGlobal(ident, alloc_type, std::move(init_value));
    return nullptr;
  }

  int addr_id = builder.NewId();
  symtab.var_addrs[ident] = addr_id;

  bb->AddInst(std::make_unique<AllocInst>(addr_id, alloc_type));

  if (has_init) {
    auto val = init_val->GenIR(bb, builder, symtab);
    if (!dims.empty()) {
      auto* init_list = dynamic_cast<InitListAST*>(init_val.get());
      int init_count = 0;
      if (init_list) {
        for (const auto &item : init_list->items) {
          auto item_val = item->GenIR(bb, builder, symtab);
          int elem_addr_id = builder.NewId();
          bb->AddInst(std::make_unique<GetPtrInst>(elem_addr_id, addr_id, std::make_unique<IntConst>(init_count)));
          bb->AddInst(std::make_unique<StoreInst>(std::move(item_val), elem_addr_id));
          init_count++;
        }
      } else {
        bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id));
        init_count = 1;
      }
      // Zero-fill remaining elements
      int total_size = 0;
      if (symtab.IsArray(ident)) {
        total_size = 1;
        for (int d : symtab.array_dims[ident]) {
          total_size *= d;
        }
      }
      for (int i = init_count; i < total_size; i++) {
        int elem_addr_id = builder.NewId();
        bb->AddInst(std::make_unique<GetPtrInst>(elem_addr_id, addr_id, std::make_unique<IntConst>(i)));
        bb->AddInst(std::make_unique<StoreInst>(std::make_unique<IntConst>(0), elem_addr_id));
      }
    } else {
      bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id));
    }
  }
  return nullptr;
}

BinaryOp char_to_binary_op(char op) {
  switch (op) {
    case '+': return BinaryOp::ADD;
    case '-': return BinaryOp::SUB;
    case '*': return BinaryOp::MUL;
    case '/': return BinaryOp::DIV;
    case '%': return BinaryOp::MOD;
    case '<': return BinaryOp::LT;
    case '>': return BinaryOp::GT;
    case 'L': return BinaryOp::LE;
    case 'G': return BinaryOp::GE;
    case 'E': return BinaryOp::EQ;
    case 'N': return BinaryOp::NE;
    case '&': return BinaryOp::AND;
    case '|': return BinaryOp::OR;
    default: return BinaryOp::ADD;
  }
}

std::unique_ptr<KoopaValue> StmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  if (type == StmtType::RETURN) {
    bool is_void_func = g_current_func && g_current_func->IsVoid();
    if (exp) {
      auto exp_val = exp->GenIR(bb, builder, symtab);
      bb->AddInst(std::make_unique<RetInst>(std::move(exp_val), is_void_func));
    } else {
      bb->AddInst(std::make_unique<RetInst>(nullptr, is_void_func));
    }
  } else if (type == StmtType::ASSIGN) {
    auto lval_ptr = static_cast<LValAST*>(lval.get());
    auto exp_val = exp->GenIR(bb, builder, symtab);
    
    bool is_global = symtab.IsGlobal(lval_ptr->ident);
    std::string base_ref = is_global ? ("@" + lval_ptr->ident) : "";
    int addr_id = is_global ? -1 : symtab.GetVarAddr(lval_ptr->ident);
    
    if (symtab.IsArray(lval_ptr->ident) && !lval_ptr->indexes.empty()) {
      int current_ptr_id = addr_id;
      std::string current_name = base_ref;
      bool current_is_named = is_global;
      
      for (size_t i = 0; i < lval_ptr->indexes.size(); ++i) {
        int new_ptr_id = builder.NewId();
        if (current_is_named) {
          bb->AddInst(std::make_unique<GetPtrInst>(new_ptr_id, -1, std::move(lval_ptr->indexes[i]->GenIR(bb, builder, symtab)), current_name));
        } else {
          bb->AddInst(std::make_unique<GetPtrInst>(new_ptr_id, current_ptr_id, std::move(lval_ptr->indexes[i]->GenIR(bb, builder, symtab))));
        }
        current_ptr_id = new_ptr_id;
        current_is_named = false;
        current_name = "";
      }
      
      bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), current_ptr_id));
    } else {
      if (is_global) {
        bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), "@" + lval_ptr->ident));
      } else {
        bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
      }
    }
  } else if (type == StmtType::EXPR) {
    if (exp) {
      exp->GenIR(bb, builder, symtab);
    }
  } else if (type == StmtType::EMPTY) {
  }
  return nullptr;
}

std::unique_ptr<KoopaValue> LValAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  if (symtab.IsConst(ident)) {
    return std::make_unique<IntConst>(symtab.GetConstValue(ident));
  } else if (symtab.IsVar(ident)) {
    bool is_global = symtab.IsGlobal(ident);
    std::string ir_name = ident;
    auto rename_it = symtab.rename_map.find(ident);
    if (rename_it != symtab.rename_map.end()) {
      ir_name = rename_it->second;
    }
    std::string base_ref = is_global ? ("@" + ir_name) : "";
    int addr_id = is_global ? -1 : symtab.GetVarAddr(ident);
    
    if (symtab.IsArray(ident) && !indexes.empty()) {
      int current_ptr_id = addr_id;
      std::string current_name = base_ref;
      bool current_is_named = is_global;
      
      for (size_t i = 0; i < indexes.size(); ++i) {
        int new_ptr_id = builder.NewId();
        if (current_is_named) {
          bb->AddInst(std::make_unique<GetPtrInst>(new_ptr_id, -1, std::move(indexes[i]->GenIR(bb, builder, symtab)), current_name));
        } else {
          bb->AddInst(std::make_unique<GetPtrInst>(new_ptr_id, current_ptr_id, std::move(indexes[i]->GenIR(bb, builder, symtab))));
        }
        current_ptr_id = new_ptr_id;
        current_is_named = false;
        current_name = "";
      }
      
      int load_id = builder.NewId();
      bb->AddInst(std::make_unique<LoadInst>(load_id, current_ptr_id));
      return std::make_unique<ValueRef>(load_id);
    } else if (symtab.IsArray(ident) && indexes.empty()) {
      if (is_global || addr_id == -1) {
        return std::make_unique<ValueRef>("@" + ir_name);
      } else {
        int id = builder.NewId();
        bb->AddInst(std::make_unique<LoadInst>(id, addr_id));
        return std::make_unique<ValueRef>(id);
      }
    } else {
      int addr_id = symtab.GetVarAddr(ident);
      
      int id = builder.NewId();
      if (is_global) {
        bb->AddInst(std::make_unique<LoadInst>(id, "@" + ir_name));
      } else {
        bb->AddInst(std::make_unique<LoadInst>(id, addr_id));
      }
      return std::make_unique<ValueRef>(id);
    }
  }
  return std::make_unique<IntConst>(0);
}

std::unique_ptr<KoopaValue> LValAST::GenIRPtr(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (symtab.IsArray(ident)) {
    bool is_global = symtab.IsGlobal(ident);
    std::string ir_name = ident;
    auto rename_it = symtab.rename_map.find(ident);
    if (rename_it != symtab.rename_map.end()) {
      ir_name = rename_it->second;
    }
    std::string base_ref = is_global ? ("@" + ir_name) : "";
    int addr_id = is_global ? -1 : symtab.GetVarAddr(ident);
    
    if (!indexes.empty()) {
      int current_ptr_id = addr_id;
      std::string current_name = base_ref;
      bool current_is_named = is_global;
      
      for (size_t i = 0; i < indexes.size(); ++i) {
        int new_ptr_id = builder.NewId();
        if (current_is_named) {
          bb->AddInst(std::make_unique<GetPtrInst>(new_ptr_id, -1, std::move(indexes[i]->GenIR(bb, builder, symtab)), current_name));
        } else {
          bb->AddInst(std::make_unique<GetPtrInst>(new_ptr_id, current_ptr_id, std::move(indexes[i]->GenIR(bb, builder, symtab))));
        }
        current_ptr_id = new_ptr_id;
        current_is_named = false;
        current_name = "";
      }
      return std::make_unique<ValueRef>(current_ptr_id);
    } else {
      if (is_global || addr_id == -1) {
        return std::make_unique<ValueRef>("@" + ir_name);
      } else {
        return std::make_unique<ValueRef>(addr_id);
      }
    }
  }
  return GenIR(bb, builder, symtab);
}

std::unique_ptr<KoopaValue> NumberAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  return std::make_unique<IntConst>(value);
}

std::unique_ptr<KoopaValue> UnaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
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

std::unique_ptr<KoopaValue> CallExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  std::vector<std::unique_ptr<KoopaValue>> args_ir;
  for (size_t i = 0; i < args.size(); ++i) {
    bool need_ptr = symtab.IsFuncParamArray(ident, i);
    if (!need_ptr) {
      if (auto lval = dynamic_cast<const LValAST*>(args[i].get())) {
        if (symtab.IsArray(lval->ident) && lval->indexes.empty()) {
          need_ptr = true;
        }
      }
    }
    
    if (need_ptr) {
      if (auto lval = dynamic_cast<const LValAST*>(args[i].get())) {
        args_ir.push_back(lval->GenIRPtr(bb, builder, symtab));
      } else {
        args_ir.push_back(args[i]->GenIR(bb, builder, symtab));
      }
    } else {
      args_ir.push_back(args[i]->GenIR(bb, builder, symtab));
    }
  }

  bool is_void_func = symtab.IsFuncVoid(ident);

  if (is_void_func) {
    bb->AddInst(std::make_unique<CallInst>(ident, std::move(args_ir), true));
    return nullptr;
  } else {
    int id = builder.NewId();
    bb->AddInst(std::make_unique<CallInst>(ident, std::move(args_ir), false, id));
    return std::make_unique<ValueRef>(id);
  }
}

std::unique_ptr<KoopaValue> BinaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
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
    bb->AddInst(std::make_unique<BinaryOpInst>(id1, BinaryOp::NE, std::move(lhs), std::make_unique<IntConst>(0)));
    
    int id2 = builder.NewId();
    bb->AddInst(std::make_unique<BinaryOpInst>(id2, BinaryOp::NE, std::move(rhs), std::make_unique<IntConst>(0)));
    
    int id3 = builder.NewId();
    bb->AddInst(std::make_unique<BinaryOpInst>(id3, char_to_binary_op(op), std::make_unique<ValueRef>(id1), std::make_unique<ValueRef>(id2)));
    
    return std::make_unique<ValueRef>(id3);
  }
  
  int id = builder.NewId();
  bb->AddInst(std::make_unique<BinaryOpInst>(id, char_to_binary_op(op), std::move(lhs), std::move(rhs)));
  return std::make_unique<ValueRef>(id);
}

std::unique_ptr<KoopaValue> IfStmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
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
  
  g_current_bb = then_bb;
  then_stmt->GenIR(then_bb, builder, symtab);
  
  BasicBlock* then_last_bb = g_current_bb;
  bool then_has_ret = then_bb->HasTerminator();
  if (!then_has_ret && then_last_bb && then_last_bb != then_bb) {
    then_has_ret = then_last_bb->HasTerminator();
  }
  
  bool else_has_ret = false;
  if (else_stmt) {
    else_bb = g_current_func->CreateBlock(else_label);
    
    g_current_bb = else_bb;
    else_stmt->GenIR(else_bb, builder, symtab);
    
    BasicBlock* else_last_bb = g_current_bb;
    else_has_ret = else_bb->HasTerminator();
    if (!else_has_ret && else_last_bb && else_last_bb != else_bb) {
      else_has_ret = else_last_bb->HasTerminator();
    }
    
    if (!then_has_ret || !else_has_ret) {
      end_bb = g_current_func->CreateBlock(end_label);
      g_end_block_stack.push_back(end_bb);
      pushed_to_stack = true;
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
    
    if (end_bb) {
      g_current_bb = end_bb;
    }
  } else {
    if (g_end_block_stack.empty()) {
      end_bb = g_current_func->CreateBlock(end_label);
      g_end_block_stack.push_back(end_bb);
      pushed_to_stack = true;
    } else {
      end_bb = g_end_block_stack.back();
    }
    
    current_bb->AddInst(std::make_unique<BranchInst>(std::move(cond_val), then_label, end_bb->GetName()));
    
    if (!then_has_ret) {
      if (then_last_bb && then_last_bb != end_bb && !then_last_bb->HasTerminator()) {
        then_last_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      } else if (then_bb != end_bb && !then_bb->HasTerminator()) {
        then_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
      }
      g_current_bb = end_bb;
    }
  }
  
  if (pushed_to_stack) {
    g_end_block_stack.pop_back();
  }
  
  return nullptr;
}

std::unique_ptr<KoopaValue> WhileStmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  BasicBlock* current_bb = g_current_bb ? g_current_bb : bb;
  
  int id = builder.NewId();
  std::string cond_label = "w" + std::to_string(id * 3 + 1);
  std::string body_label = "w" + std::to_string(id * 3 + 2);
  std::string end_label = "w" + std::to_string(id * 3 + 3);
  
  size_t block_count_before = g_current_func->GetBlockCount();
  
  BasicBlock* cond_bb = g_current_func->CreateBlock(cond_label);
  BasicBlock* body_bb = g_current_func->CreateBlock(body_label);
  BasicBlock* end_bb = g_current_func->CreateBlock(end_label);
  
  cond_bb->SetProtected(true);
  body_bb->SetProtected(true);
  
  current_bb->AddInst(std::make_unique<JumpInst>(cond_label));
  
  LoopContext loop_ctx;
  loop_ctx.cond_block = cond_bb;
  loop_ctx.end_block = end_bb;
  g_loop_stack.push_back(loop_ctx);
  
  g_current_bb = cond_bb;
  auto cond_val = cond->GenIR(cond_bb, builder, symtab);
  cond_bb->AddInst(std::make_unique<BranchInst>(std::move(cond_val), body_label, end_label));
  
  g_current_bb = body_bb;
  g_loop_depth++;
  body->GenIR(body_bb, builder, symtab);
  g_loop_depth--;
  
  for (size_t i = block_count_before; i < g_current_func->GetBlockCount(); ++i) {
    BasicBlock* blk = g_current_func->GetBlock(i);
    if (blk == cond_bb || blk == body_bb || blk == end_bb) continue;
    if (blk->IsProtected()) continue;
    
    if (!blk->HasTerminator() && !blk->IsEmpty()) {
      blk->AddInst(std::make_unique<JumpInst>(cond_label));
    }
  }
  
  if (body_bb && !body_bb->HasTerminator()) {
    body_bb->AddInst(std::make_unique<JumpInst>(cond_label));
  }
  
  BasicBlock* body_last_bb = g_current_bb;
  if (body_last_bb && body_last_bb != body_bb && !body_last_bb->HasTerminator() && !body_last_bb->IsProtected()) {
    body_last_bb->AddInst(std::make_unique<JumpInst>(cond_label));
  }
  
  g_loop_stack.pop_back();
  
  cond_bb->SetProtected(false);
  body_bb->SetProtected(false);

  g_current_func->MoveBlockToEnd(end_bb);
  g_current_bb = end_bb;
  
  return nullptr;
}

std::unique_ptr<KoopaValue> BreakStmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  BasicBlock* current_bb = g_current_bb ? g_current_bb : bb;
  
  if (!g_loop_stack.empty()) {
    BasicBlock* end_bb = g_loop_stack.back().end_block;
    current_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
  }
  
  return nullptr;
}

std::unique_ptr<KoopaValue> ContinueStmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  BasicBlock* current_bb = g_current_bb ? g_current_bb : bb;
  
  if (!g_loop_stack.empty()) {
    BasicBlock* cond_bb = g_loop_stack.back().cond_block;
    current_bb->AddInst(std::make_unique<JumpInst>(cond_bb->GetName()));
  }
  
  return nullptr;
}

std::unique_ptr<KoopaValue> InitListAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  return std::make_unique<IntConst>(0);
}

std::unique_ptr<KoopaValue> FuncFParamAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab, Program *program) const {
  return nullptr;
}
