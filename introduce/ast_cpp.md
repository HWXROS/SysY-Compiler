# ast.cpp — AST 的 IR 生成实现

## 文件作用

**ast.cpp** 是 **ast.h** 中各个 AST 类的成员函数实现，其中最核心的是 `GenIR` 方法 — 将 AST 转换为中间表示（IR）。每个 AST 节点通过 `GenIR` 生成对应的 Koopa IR 指令，逐步将高层代码翻译成底层指令序列。

---

## 重要函数

### CompUnitAST::GenIR — 程序入口

```cpp
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  auto func = static_cast<FuncDefAST*>(func_def.get());
  program->AddFunc(func->GenIR());
  return program;
}
```

**作用：** 遍历编译单元，调用函数定义的 `GenIR`，生成完整的 IR 程序。

---

### FuncDefAST::GenIR — 函数定义

```cpp
std::unique_ptr<Function> FuncDefAST::GenIR() const {
  auto func = std::make_unique<Function>(ident, "i32");  // 创建函数
  g_current_func = func.get();                            // 设置全局当前函数

  SymbolTable symtab;                                      // 创建根符号表
  IRBuilder builder;
  auto entry_bb = std::make_unique<BasicBlock>("entry");
  g_current_bb = entry_bb.get();
  func->AddBlock(std::move(entry_bb));

  block->GenIR(g_current_bb, builder, symtab);            // 生成函数体 IR

  g_current_func = nullptr;
  g_current_bb = nullptr;
  return func;
}
```

**关键点：**
- 创建根 `SymbolTable`（无父指针）
- 创建 entry 基本块
- 调用 `BlockAST::GenIR` 生成函数体

---

### BlockAST::GenIR — 语句块（两个重载）

#### 第一个重载：创建新 BasicBlock（用于函数体）

```cpp
std::unique_ptr<BasicBlock> BlockAST::GenIR(IRBuilder &builder, SymbolTable &symtab) const {
  auto bb = std::make_unique<BasicBlock>("entry");
  SymbolTable new_symtab(&symtab);              // 创建子作用域
  for (const auto &item : items) {
    item->GenIR(bb.get(), builder, new_symtab); // 生成每条语句的 IR
  }
  return bb;
}
```

#### 第二个重载：复用已有 BasicBlock（用于嵌套块）

```cpp
std::unique_ptr<KoopaValue> BlockAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  SymbolTable new_symtab(&symtab);              // 创建子作用域
  for (const auto &item : items) {
    if (g_current_bb && g_current_bb->HasTerminator()) break;
    if (g_current_bb) {
      item->GenIR(g_current_bb, builder, new_symtab);
    } else {
      item->GenIR(bb, builder, new_symtab);
    }
  }

  // 块结束后：如果不在循环内，且 end_block_stack 非空，跳转到外层 end 块
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
```

**关键点：**
- 每进入一个块就创建新的 `SymbolTable`（父指针指向外层）
- 遍历块内所有语句/声明，依次生成 IR
- 嵌套块复用外层 BasicBlock，而不是创建新的

---

### VarDeclAST/VarDefAST::GenIR — 变量声明

```cpp
std::unique_ptr<KoopaValue> VarDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  int addr_id = builder.NewId();                              // 分配新地址ID
  bb->AddInst(std::make_unique<AllocInst>(addr_id));          // 生成 alloc 指令
  symtab.var_addrs[ident] = addr_id;                           // 注册到符号表

  if (has_init) {
    auto val = init_val->GenIR(bb, builder, symtab);           // 计算初值
    bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id)); // store 指令
  }
  return nullptr;
}
```

**示例：** `int a = 1;` 生成：
```
%0 = alloc i32
store 1, %0
```

---

### ConstDeclAST/ConstDefAST::GenIR — 常量声明

```cpp
std::unique_ptr<KoopaValue> ConstDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  auto val = init_val->GenIR(bb, builder, symtab);  // 计算常量值
  int const_val = val->GetConstValue();             // 提取常量值
  symtab.const_values[ident] = const_val;           // 记录到符号表（不生成 IR）
  return nullptr;
}
```

**关键点：** 常量只在符号表中记录值，不生成任何 IR 指令。

---

### StmtAST::GenIR — 语句生成

```cpp
std::unique_ptr<KoopaValue> StmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (type == StmtType::RETURN) {
    auto exp_val = exp->GenIR(bb, builder, symtab);
    bb->AddInst(std::make_unique<RetInst>(std::move(exp_val)));
  } else if (type == StmtType::ASSIGN) {
    auto lval_ptr = static_cast<LValAST*>(lval.get());
    auto exp_val = exp->GenIR(bb, builder, symtab);
    int addr_id = symtab.GetVarAddr(lval_ptr->ident);  // 查符号表找变量地址
    bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
  }
  return nullptr;
}
```

---

### LValAST::GenIR — 左值（变量读取）

```cpp
std::unique_ptr<KoopaValue> LValAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (symtab.IsConst(ident)) {
    return std::make_unique<IntConst>(symtab.GetConstValue(ident)); // 返回常量值
  } else if (symtab.IsVar(ident)) {
    int addr_id = symtab.GetVarAddr(ident);  // 查变量地址
    int id = builder.NewId();
    bb->AddInst(std::make_unique<LoadInst>(id, addr_id)); // 生成 load 指令
    return std::make_unique<ValueRef>(id);
  }
  return std::make_unique<IntConst>(0);
}
```

---

### BinaryExprAST::GenIR — 二元表达式

```cpp
std::unique_ptr<KoopaValue> BinaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  auto lhs = left->GenIR(bb, builder, symtab);  // 生成左操作数
  auto rhs = right->GenIR(bb, builder, symtab);  // 生成右操作数

  // 如果左右都是常量，直接在编译期计算（常量折叠）
  if (lhs->IsConst() && rhs->IsConst()) {
    int result = /* 根据 op 计算 */;
    return std::make_unique<IntConst>(result);
  }

  // 否则生成二元运算指令
  int id = builder.NewId();
  bb->AddInst(std::make_unique<BinaryOpInst>(id, op, std::move(lhs), std::move(rhs)));
  return std::make_unique<ValueRef>(id);
}
```

---

### IfStmtAST::GenIR — if 语句

核心流程：
1. 生成条件表达式 → 产生条件值
2. 创建 then 块、else 块（如果有）、end 块
3. 生成条件分支 `br cond, then_bb, else_bb`
4. then 分支和 else 分支各自生成 IR，末尾添加跳转到 end 块的无条件跳转
5. 设置 `g_current_bb = end_bb`，继续后续语句

使用 `g_end_block_stack` 管理嵌套 if 的 end 块，确保 else-if 等嵌套结构正确汇聚。

---

### WhileStmtAST::GenIR — while 循环

核心流程：
1. 创建 3 个基本块：`cond_bb`（条件）、`body_bb`（循环体）、`end_bb`（结束后）
2. 从当前块跳转到 `cond_bb`
3. 在 `cond_bb` 生成条件表达式和 `br cond, body_bb, end_bb`
4. 在 `body_bb` 生成循环体 IR
5. **修复循环回边**：扫描所有新创建的块，给没有 terminator 的块添加 `jump cond_bb`（确保循环体能跳回条件）
6. 设置 `g_current_bb = end_bb`

**`g_loop_stack`**：维护循环上下文栈，供 `BreakStmt` 和 `ContinueStmt` 查找目标块。

---

### BreakStmtAST/ContinueStmtAST::GenIR

```cpp
// Break: 跳转到循环的 end 块
if (!g_loop_stack.empty()) {
  BasicBlock* end_bb = g_loop_stack.back().end_block;
  current_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
}

// Continue: 跳转到循环的 cond 块
if (!g_loop_stack.empty()) {
  BasicBlock* cond_bb = g_loop_stack.back().cond_block;
  current_bb->AddInst(std::make_unique<JumpInst>(cond_bb->GetName()));
}
```

---

## 全局线程局部变量详解

```cpp
thread_local Function* g_current_func = nullptr;           // 当前正在生成 IR 的函数
thread_local BasicBlock* g_current_bb = nullptr;             // 当前正在添加指令的基本块
thread_local std::vector<BasicBlock*> g_end_block_stack;   // if 语句的 end 块栈
thread_local std::vector<LoopContext> g_loop_stack;        // 循环上下文栈
thread_local int g_loop_depth = 0;                         // 当前循环嵌套深度
thread_local std::set<BasicBlock*> g_protected_blocks;     // 受保护的基本块集合
```

这些 `thread_local` 变量保证在递归/深层嵌套时，每个线程都有独立的控制流状态。
