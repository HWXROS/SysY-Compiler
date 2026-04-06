# Lv7. while 循环实现笔记

## 概述

本章实现了支持 while 循环、break 和 continue 语句的编译器。循环是程序控制流的核心构造，允许重复执行代码块直到条件不满足。

## 语法扩展

### 词法分析 (sysy.l)

添加三个新的关键字：

```lex
"while"         { return WHILE; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }
```

### 语法分析 (sysy.y)

#### 新增 token 声明

```yacc
%token CONST INT RETURN IF ELSE WHILE BREAK CONTINUE
```

#### 语法规则

在 `Stmt` 非终结符中新增三条规则：

```yacc
Stmt
  : ... [Lv6 已有规则] ...
  | WHILE '(' Exp ')' Stmt {
      // while 循环
      auto ast = new WhileStmtAST();
      ast->cond = unique_ptr<BaseAST>($3);
      ast->body = unique_ptr<BaseAST>($5);
      $$ = ast;
    }
  | BREAK ';' {
      // break 语句（跳出最近一层循环）
      auto ast = new BreakStmtAST();
      $$ = ast;
    }
  | CONTINUE ';' {
      // continue 语句（跳到循环条件判断处）
      auto ast = new ContinueStmtAST();
      $$ = ast;
    }
  ;
```

#### 语义约束

- `break` 只能出现在循环体内（while 内部）
- `continue` 只能出现在循环体内（while 内部）
- break/continue 跳转到的是**最近一层**循环的对应位置

## AST 设计

### 新增 AST 节点类

#### WhileStmtAST — while 循环

```cpp
class WhileStmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> cond;   // 条件表达式
  std::unique_ptr<BaseAST> body;   // 循环体（一条 Stmt，通常是 Block）
};
```

#### BreakStmtAST — break 语句

```cpp
class BreakStmtAST : public BaseAST {
 public:
  // 无字段，break 的目标由运行时上下文决定
};
```

#### ContinueStmtAST — continue 语句

```cpp
class ContinueStmtAST : public BaseAST {
 public:
  // 无字段，continue 的目标由运行时上下文决定
};
```

#### LoopContext — 循环上下文（辅助结构）

```cpp
struct LoopContext {
  BasicBlock* cond_block;  // 条件判断块（continue 跳转目标）
  BasicBlock* end_block;   // 循环结束块（break 跳转目标）
};
```

#### StmtType 枚举扩展

```cpp
enum class StmtType {
  RETURN,
  ASSIGN,
  EXPR,      // 表达式语句
  EMPTY,     // 空语句
  BLOCK,     // 块语句
  BREAK,     // break 语句     ← 新增
  CONTINUE   // continue 语句  ← 新增
};
```

### 结构图

```
CompUnit (编译单元)
  │
  └── FuncDef (函数定义)
       │
       └── Block (函数体)
            │
            └── BlockItem
                 │
                 └── Stmt (语句)
                      │
                      ├── ... (Lv5/Lv6 的语句类型)
                      │
                      ├── WhileStmt (while 循环)        ← 新增
                      │    │
                      │    ├── "while"
                      │    ├── "("
                      │    ├── Exp (条件表达式)
                      │    ├── ")"
                      │    └── Stmt (循环体)
                      │         │
                      │         └── ... (递归：可以是 Block、if、另一个 while 等)
                      │
                      ├── BreakStmt (break 语句)         ← 新增
                      │    └── "break" ";"
                      │
                      └── ContinueStmt (continue 语句)   ← 新增
                           └── "continue" ";"
```

## IR 生成

### 基本块结构

while 循环需要生成 **4 个基本块**：

```
[当前块]          ; 进入循环前执行的代码
  jump %cond      ; 跳到条件判断

%cond:            ; 条件判断块
  br %cond_val, %body, %end   ; 条件为真→body，为假→end

%body:            ; 循环体块
  [循环体代码]
  [jump %cond]    ; 回边：回到条件判断（如果 body 没有以 ret/break/continue 结束）

%end:             ; 循环结束块（循环后的代码从这里继续）
  [后续代码]
```

### 控制流图

```
                    ┌─────────────┐
                    │   当前块     │
                    │  jump %cond │
                    └──────┬──────┘
                           ▼
                    ┌─────────────┐
              ┌────▶│   %cond     │◀────────┐
              │     │  br cond    │         │
              │     └──┬──────┬───┘         │
              │  true│        │false        │
              │        ▼        ▼            │
              │  ┌──────────┐ ┌──────────┐  │
              │  │  %body   │ │  %end    │  │
              │  │ [循环体]  │ │ [后续]   │  │
              │  │jump %cond │ │          │  │
              │  └─────┬────┘ └──────────┘  │
              │        │                     │
              └────────┘─────────────────────┘
                  (回边 / continue)

  break: body → 直接跳到 %end
  continue: body → 直接跳到 %cond
```

### IR 生成算法 — WhileStmtAST

```cpp
std::unique_ptr<KoopaValue> WhileStmtAST::GenIR(
    BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {

  BasicBlock* current_bb = g_current_bb ? g_current_bb : bb;

  // 1. 为三个基本块生成唯一标签
  int id = builder.NewId();
  std::string cond_label = "w" + std::to_string(id * 3 + 1);   // 条件块
  std::string body_label = "w" + std::to_string(id * 3 + 2);   // 循环体块
  std::string end_label  = "w" + std::to_string(id * 3 + 3);   // 结束块

  size_t block_count_before = g_current_func->GetBlockCount();

  // 2. 创建三个基本块
  BasicBlock* cond_bb = g_current_func->CreateBlock(cond_label);
  BasicBlock* body_bb = g_current_func->CreateBlock(body_label);
  BasicBlock* end_bb  = g_current_func->CreateBlock(end_label);

  // 3. 保护 cond_bb 和 body_bb（防止外层循环的修复循环误修改它们）
  cond_bb->SetProtected(true);
  body_bb->SetProtected(true);

  // 4. 当前块跳转到条件块
  current_bb->AddInst(std::make_unique<JumpInst>(cond_label));

  // 5. 将循环上下文压入栈（供 break/continue 使用）
  LoopContext loop_ctx;
  loop_ctx.cond_block = cond_bb;
  loop_ctx.end_block = end_bb;
  g_loop_stack.push_back(loop_ctx);

  // 6. 在条件块中生成条件表达式和分支指令
  g_current_bb = cond_bb;
  auto cond_val = cond->GenIR(cond_bb, builder, symtab);
  cond_bb->AddInst(std::make_unique<BranchInst>(
      std::move(cond_val), body_label, end_label));

  // 7. 在循环体块中生成循环体代码
  g_current_bb = body_bb;
  g_loop_depth++;
  body->GenIR(body_bb, builder, symtab);
  g_loop_depth--;

  // 8. 修复循环体内部无 terminator 的非保护块 → 加回边到 cond
  for (size_t i = block_count_before; i < g_current_func->GetBlockCount(); ++i) {
    BasicBlock* blk = g_current_func->GetBlock(i);
    if (blk == cond_bb || blk == body_bb || blk == end_bb) continue;
    if (blk->IsProtected()) continue;

    if (!blk->HasTerminator() && !blk->IsEmpty()) {
      blk->AddInst(std::make_unique<JumpInst>(cond_label));
    }
  }

  // 9. 如果 body_bb 本身没有 terminator，给它加回边
  if (body_bb && !body_bb->HasTerminator()) {
    body_bb->AddInst(std::make_unique<JumpInst>(cond_label));
  }

  // 10. 弹出循环上下文
  g_loop_stack.pop_back();

  // 11. 取消保护标记
  cond_bb->SetProtected(false);
  body_bb->SetProtected(false);

  // 12. 清空 end_bb 中可能被错误填充的指令
  while (!end_bb->IsEmpty()) {
    end_bb->RemoveLastInst();
  }

  // 13. 空 end_bb 需要 terminator → 跳到 if-end 栈顶（如果有）
  if (end_bb->IsEmpty() && !g_end_block_stack.empty()) {
    BasicBlock* target = g_end_block_stack.back();
    if (end_bb != target) {
      end_bb->AddInst(std::make_unique<JumpInst>(target->GetName()));
    }
  }

  // 14. 设置当前块为 end_bb（后续代码从这里继续）
  g_current_bb = end_bb;

  return nullptr;
}
```

### IR 生成算法 — BreakStmtAST

```cpp
std::unique_ptr<KoopaValue> BreakStmtAST::GenIR(
    BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {

  BasicBlock* current_bb = g_current_bb ? g_current_bb : bb;

  if (!g_loop_stack.empty()) {
    // 跳到最近一层循环的结束块
    BasicBlock* end_bb = g_loop_stack.back().end_block;
    current_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
  }

  return nullptr;
}
```

### IR 生成算法 — ContinueStmtAST

```cpp
std::unique_ptr<KoopaValue> ContinueStmtAST::GenIR(
    BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {

  BasicBlock* current_bb = g_current_bb ? g_current_bb : bb;

  if (!g_loop_stack.empty()) {
    // 跳到最近一层循环的条件块
    BasicBlock* cond_bb = g_loop_stack.back().cond_block;
    current_bb->AddInst(std::make_unique<JumpInst>(cond_bb->GetName()));
  }

  return nullptr;
}
```

### 全局状态变量

```cpp
// ast.h / ast.cpp
extern thread_local Function* g_current_func;           // 当前函数
extern thread_local BasicBlock* g_current_bb;           // 当前基本块
extern thread_local std::vector<BasicBlock*> g_end_block_stack;  // if-end 块栈
extern thread_local std::vector<LoopContext> g_loop_stack;       // 循环上下文栈
extern thread_local int g_loop_depth;                   // 当前嵌套循环深度
extern thread_local std::set<BasicBlock*> g_protected_blocks;   // 受保护的块集合
```

| 变量 | 类型 | 用途 |
|------|------|------|
| `g_current_func` | `Function*` | 当前正在生成 IR 的函数 |
| `g_current_bb` | `BasicBlock*` | 当前正在添加指令的基本块 |
| `g_end_block_stack` | `vector<BasicBlock*>` | 嵌套 if 语句的 end 块栈 |
| `g_loop_stack` | `vector<LoopContext>` | 嵌套 while 循环的上下文栈 |
| `g_loop_depth` | `int` | 当前嵌套循环层数（0 表示不在循环内） |
| `g_protected_blocks` | `set<BasicBlock*>` | 被 SetProtected 标记的块 |

### BlockAST 的循环感知修改

```cpp
// BlockAST::GenIR() 末尾 — 仅在非循环体内添加自动跳转
if (g_current_bb && !g_current_bb->HasTerminator() && g_loop_depth == 0) {
  if (!g_end_block_stack.empty()) {
    BasicBlock* outer_end = g_end_block_stack.back();
    if (g_current_bb != outer_end) {
      g_current_bb->AddInst(std::make_unique<JumpInst>(outer_end->GetName()));
    }
  }
}
```

**关键点**：`g_loop_depth == 0` 条件确保只有在**不在任何循环体内**时才添加自动跳转。循环体内的回边由 `WhileStmtAST` 统一管理。

### BasicBlock 扩展方法

```cpp
class BasicBlock {
  bool is_protected = false;  // 保护标志

 public:
  void SetProtected(bool v) { is_protected = v; }
  bool IsProtected() const { return is_protected; }
  bool HasTerminator() const;  // 是否已有终止指令
  bool IsEmpty() const;        // 是否没有任何指令
  Instruction* GetLastInst() const;  // 获取最后一条指令
  void RemoveLastInst();       // 删除最后一条指令
};
```

---

## RISC-V 代码生成

### 分支指令映射（与 Lv6 相同）

| Koopa IR | RISC-V | 说明 |
|----------|--------|------|
| `br %cond, %true, %false` | `bnez a0, %true` + `j %false` | 条件分支 |
| `jump %target` | `j %target` | 无条件跳转 |

break 和 continue 在 Koopa IR 层面已经展开为普通的 `jump` 指令，RISC-V 生成无需特殊处理。

---

## 错误剖析与解决方案

本章的实现过程中遇到了多个复杂的控制流问题，以下是详细的错误分析和解决方案。

### 错误1：隐式 Jump 导致死循环（最核心的问题）

**问题描述**：
`BasicBlock::Dump()` 和 `Function::Dump()` 中存在**隐式 jump 机制**——当某个基本块没有终止指令时，Dump 会自动输出 `jump %next_block`，其中 `next_block` 是 vector 中下一个块的名称。这在嵌套循环中会导致**错误的跳转目标**，形成死循环。

**错误机制**：

```
Function::Dump() 遍历 blocks[i]，对每个块调用 blocks[i]->SetNextBlock(blocks[i+1]->GetName())
BasicBlock::Dump() 输出: if (!HasTerminator() && !next_block.empty()) → "  jump %next_block"
```

**问题场景**（11_complex.c）：

```c
while (1) {                          // 外层 while → w1/w2/w3
  int a = 1, b = 2;
  if (a == 1) {
    while (a < b) {                 // 中层 while → w19/w20/w21
      while (a < b || b - 1 == 0) { // 内层 while → w31/w32/w33
        a = a + 1;
      }
      b = 1;
      a = a + 1;
      if (3) continue;
    }
  } else if (b == 6) {
    break;
  }
  ...
}
return -1;
```

**生成的错误 IR**（简化）：
```
%w19:                              ; 中层 cond
  br ..., %w20, %w21
%w20:                              ; 中层 body（无 terminator）
                                    ↓ 隐式 jump 到下一个物理块！
%w21:                              ; 中层 end（空，无 terminator）
                                    ↓ 隐式 jump 到下一个物理块！
%w31:                              ; 内层 cond
  br ..., %w32, %w33
%w32:                              ; 内层 body
  jump %w31                         ; 正确回边
%w33:                              ; 内层 end
  ... → b18 → w19                  ; 回到中层 cond
```

**死循环路径**：`w19 → w20 → w21(隐式→w31) → w32 → w33 → b18 → w19 → ♻️`

根因：`%w21`(中层 end) 是空块，位于 `%w31`(内层 cond) **之前**，隐式 jump 把它指向了内层 cond！

**解决方案**：

**完全去掉隐式 jump 机制**，所有 terminator 由 AST GenIR 阶段显式管理：

```cpp
// ir.h — BasicBlock::Dump()
void Dump(std::ostream &os) const {
  os << "%" << name << ":\n";
  for (const auto &inst : insts) {
    inst->Dump(os);
  }
  // 不再输出隐式 jump！
}

// ir.h — Function::Dump()
void Dump(std::ostream &os) const {
  os << "fun @" << name << "(): " << ret_type << " {\n";
  for (size_t i = 0; i < blocks.size(); ++i) {
    blocks[i]->Dump(os);  // 不再设置 next_block
  }
  if (!blocks.empty() && !blocks.back()->HasTerminator()) {
    os << "  ret 0\n";  // 函数末尾兜底
  }
  os << "}\n";
}
```

同时，WhileStmtAST 显式管理 end_bb 的 terminator：

```cpp
// 清空 end_bb 中可能被 IfStmtAST 等错误填充的指令
while (!end_bb->IsEmpty()) {
  end_bb->RemoveLastInst();
}

// 空 end_bb → 跳到 if-end 栈顶（如果有），否则保持空（函数末尾兜底 ret 0）
if (end_bb->IsEmpty() && !g_end_block_stack.empty()) {
  BasicBlock* target = g_end_block_stack.back();
  if (end_bb != target) {
    end_bb->AddInst(std::make_unique<JumpInst>(target->GetName()));
  }
}
```

---

### 错误2：BlockAST 自动跳转干扰循环回边

**问题描述**：
`BlockAST::GenIR()` 在处理完所有语句后，会自动给当前块添加一个跳转到 `g_end_block_stack` 顶部（if 的 end 块）的 jump。但在循环体内，这个自动跳转会与 while 的回边冲突。

**错误场景**（05_if_while.c）：

```c
int main() {
  int a = 1;
  if (a > 1) {           // 不进入
    while (1);           // 死循环（但不会执行）
  } else {
    while (a < 10) {     // 进入这个 while
      a = a + 2;         // a: 1→3→5→7→9→11
    }
  }
  return a;               // 应返回 11
}
```

**错误现象**：返回 3（只执行了一次循环体）

**错误原因**：else 分支中的 while 循环体 `%w17` 被 BlockAST 添加了 `jump %b12`(if-end)，而不是正确的 `jump %w16`(while-cond)。导致第一次迭代后就跳出循环了。

**解决方案**：

在 BlockAST 的自动跳转中加入循环深度检查：

```cpp
if (g_current_bb && !g_current_bb->HasTerminator() && g_loop_depth == 0) {
  // 只有不在循环体内才添加自动跳转
  if (!g_end_block_stack.empty()) {
    BasicBlock* outer_end = g_end_block_stack.back();
    if (g_current_bb != outer_end) {
      g_current_bb->AddInst(std::make_unique<JumpInst>(outer_end->GetName()));
    }
  }
}
```

循环体内的回边完全由 `WhileStmtAST` 的修复循环统一管理。

---

### 错误3：IfStmtAST 在循环体内添加错误跳转

**问题描述**：
`IfStmtAST::GenIR()` 在 then/else 分支结束后会给它们添加跳转到 end 块的 jump。当 if 位于循环体内时，这些跳转可能指向不正确的目标。

**错误场景**（11_complex.c 中的 b18）：
```
%b18:  (if(a==1) 的 else 分支 end)
  jump %w19   ← 错误！应该让后续代码自然流过
```

这个问题在去掉隐式 jump 后，配合 WhileStmtAST 的 end_bb 清空+显式管理策略得到了解决。IfStmtAST 的跳转仍然会被添加，但 WhileStmtAST 会在清空 end_bb 时移除它们。

---

### 错误4：嵌套循环的外层修复循环误伤内层块

**问题描述**：
`WhileStmtAST` 有一个**修复循环**，用于给循环体内缺少 terminator 的块添加回边（`jump %cond`）。但在嵌套循环中，外层的修复循环可能会错误地修改内层 while 创建的 cond/body/end 块。

**错误场景**：

```
外层 while (cond=w19, body=w20, end=w21):
  修复循环遍历所有新创建的块...
  发现内层 while 的 body 块 %w31 没有 terminator
  错误地给它加上了 jump %w19（外层 cond）！！
  应该是 jump %w33（内层 cond）
```

**解决方案**：使用 `SetProtected` 保护机制

```cpp
// 创建块时设置保护标志
cond_bb->SetProtected(true);
body_bb->SetProtected(true);

// 修复循环跳过受保护的块
for (size_t i = block_count_before; i < g_current_func->GetBlockCount(); ++i) {
  BasicBlock* blk = g_current_func->GetBlock(i);
  if (blk == cond_bb || blk == body_bb || blk == end_bb) continue;
  if (blk->IsProtected()) continue;  // 跳过内层循环的块

  if (!blk->HasTerminator() && !blk->IsEmpty()) {
    blk->AddInst(std::make_unique<JumpInst>(cond_label));
  }
}

// body_bb 单独处理（它属于当前循环，不是内层的）
if (body_bb && !body_bb->HasTerminator()) {
  body_bb->AddInst(std::make_unique<JumpInst>(cond_label));
}

// 循环结束后取消保护（允许外层循环正常使用）
cond_bb->SetProtected(false);
body_bb->SetProtected(false);
```

**设计要点**：
- `cond_bb` 被保护：它有自己的 branch terminator，不需要也不应该被修改
- `body_bb` 被保护：防止被外层修复循环误改，但由当前循环自己负责添加回边
- `end_bb` 不被保护：它可能在后续被其他机制（如 if-end 栈）正确填充

---

### 错误5：bridge 机制导致 block 顺序错乱

**问题描述**（已废弃方案）：
曾尝试在 WhileStmtAST 开头检查 `current_bb` 是否非空且无 terminator，如果是则创建一个 bridge block 来承载 `jump %cond`。但这导致 `current_bb` 被切换到新的 bridge block，使得后续（如内层 while 的 block）被创建在错误的位置，破坏了 CFG 的拓扑顺序。

**教训**：不要在 GenIR 过程中随意切换 `current_bb`，这会影响后续 block 的创建位置。

---

## 关键经验总结

### 1. 控制流管理的分层架构

```
┌─────────────────────────────────────────────────┐
│  Layer 3: WhileStmtAST                          │
│  • 管理循环的 cond/body/end 三个块               │
│  • 修复循环体内无 terminator 的块（加回边）       │
│  • 清空并显式管理 end_bb 的 terminator           │
├─────────────────────────────────────────────────┤
│  Layer 2: IfStmtAST                             │
│  • 管理 if/else 的 then/else/end 三个块         │
│  • 给 then/else 分支添加到 end 的 jump           │
├─────────────────────────────────────────────────┤
│  Layer 1: BlockAST                              │
│  • 处理完所有语句后添加自动跳转                   │
│  • 仅在 g_loop_depth == 0 时生效                │
├─────────────────────────────────────────────────┤
│  Layer 0: Function::Dump()                       │
│  • 兜底：末尾块无 terminator 时输出 ret 0        │
└─────────────────────────────────────────────────┘
```

### 2. 全局状态的协调

| 状态 | Who Pushes | Who Pops | 用途 |
|------|-----------|----------|------|
| `g_loop_stack` | WhileStmtAST | WhileStmtAST | break/continue 找目标 |
| `g_end_block_stack` | IfStmtAST | IfStmtAST | 嵌套 if 的 end 块复用 |
| `g_loop_depth` | WhileStmtAST (+1/-1) | WhileStmtAST | BlockAST 判断是否在循环内 |
| `is_protected` | WhileStmtAST | WhileStmtAST | 防止外层修复循环误伤 |

### 3. 设计原则

- **显式优于隐式**：所有 jump 必须在 GenIR 阶段显式生成，Dump 阶段不做隐式推断
- **保护机制**：嵌套结构中，内层创建的关键块需要保护，防止外层逻辑误修改
- **统一修复**：WhileStmtAST 在结束时统一扫描并修复循环体内所有缺 terminator 的块
- **end_bb 清空策略**：先清空再按需填充，避免残留的错误跳转

---

## 测试用例

### 测试 00：基本 while 循环

```c
int main() {
  int a = 0;
  while (a < 10) {
    a = a + 1;
  }
  return a;
}
```

**预期结果**：返回 10

**生成的 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 0, %0
  jump %w4
%w4:
  %2 = load %0
  %3 = lt %2, 10
  br %3, %w5, %w6
%w5:
  %4 = load %0
  %5 = add %4, 1
  store %5, %0
  jump %w4
%w6:
  %6 = load %0
  ret %6
}
```

### 测试 01：while 计算幂

```c
int main() {
  int res = 1, base = 2;
  while (base > 0) {
    res = res * base;
    base = base - 1;
  }
  return res;
}
```

**预期结果**：返回 2（2^1 = 2）

### 测试 02：while 条件恒假

```c
int main() {
  int a = 0;
  while (0) {
    a = a + 1;
  }
  return a;
}
```

**预期结果**：返回 0（循环体不执行）

### 测试 03：while 条件恒真

```c
int main() {
  while (1) {
    break;
  }
  return 42;
}
```

**预期结果**：返回 42（break 跳出循环）

### 测试 04：while 内嵌套 if

```c
int main() {
  int a = 0;
  while (a < 5) {
    if (a < 3) {
      a = a + 2;
    } else {
      a = a + 1;
    }
  }
  return a;
}
```

**预期结果**：a 变化：0→2→4→5，返回 5

### 测试 05：if 内嵌套 while

```c
int main() {
  int a = 1;
  if (a > 1) {
    while (1);
  } else {
    while (a < 10) {
      a = a + 2;
    }
  }
  return a;
}
```

**预期结果**：a=1 不进入 if，进入 else 的 while：1→3→5→7→9→11，返回 11

### 测试 06：嵌套 while

```c
int main() {
  int a = 1, b = 2;
  while (a < 10) {
    a = a + 1;
    while (a < 5 && b < 10) {
      b = b + 1;
    }
    while (b < 20) {
      while (b < 6 || b == 6) {
        b = b + 1;
      }
      b = b + 2;
    }
  }
  return a + b;
}
```

**预期结果**：测试三层嵌套 while 的正确性

### 测试 07：基本 break

```c
int main() {
  int a = 0;
  while (1) {
    a = a + 1;
    if (a > 5) break;
  }
  return a;
}
```

**预期结果**：返回 6

### 测试 08：if 内 break

```c
int main() {
  int a = 0;
  while (1) {
    a = a + 1;
    if (a >= 3) {
      break;
    }
  }
  return a;
}
```

**预期结果**：返回 3

### 测试 09：基本 continue

```c
int main() {
  int sum = 0, i = 0;
  while (i < 10) {
    i = i + 1;
    if (i % 2 == 0) continue;
    sum = sum + i;
  }
  return sum;
}
```

**预期结果**：返回 25（1+3+5+7+9）

### 测试 10：if 内 continue

```c
int main() {
  int a = 0;
  while (a < 10) {
    a = a + 1;
    if (a <= 5) continue;
    if (a >= 8) break;
  }
  return a;
}
```

**预期结果**：返回 8

### 测试 11：复杂嵌套（最难的测试）

```c
int main() {
  while (1) {
    int a = 1, b = 2;
    {
      if (a == 1) {
        while (a < b) {
          while (a < b || b - 1 == 0) {
            a = a + 1;
          }
          b = 1;
          a = a + 1;
          if (3) continue;
        }
      } else if (b == 6) {
        break;
      }
      int b = 6;
      if (b == 6) return 8 * (10 || b);
      else while (0);
    }
  }
  return -1;
}
```

**预期结果**：返回 80（8 * (10 || 6) = 8 * 10 = 80）

**复杂度分析**：
- 三层 while 嵌套（外层、中层、内层）
- 两层 if-else 嵌套
- break 和 continue 同时出现
- Block 作用域遮蔽（内层 `int b = 6` 遮蔽外层 `b`）
- return 在循环内部提前退出

---

## 总结

本章实现了 while 循环、break 和 continue 语句的完整编译流程：

1. **词法分析**：识别 `while`、`break`、`continue` 三个关键字
2. **语法分析**：构建 WhileStmtAST、BreakStmtAST、ContinueStmtAST 节点
3. **IR 生成**：
   - 创建 cond/body/end 三个基本块
   - 使用 `g_loop_stack` 管理嵌套循环上下文
   - 使用 `g_loop_depth` 控制 BlockAST 的自动跳转行为
   - 使用 `SetProtected` 机制防止嵌套循环间的干扰
   - 完全去除隐式 jump，所有 terminator 显式管理
4. **RISC-V 代码生成**：break/continue 展开为普通 jump，无需特殊处理

**最核心的挑战**是嵌套循环中的控制流管理，通过以下机制的协同工作成功解决：
- **隐式 jump 移除**：消除基于物理位置的错误跳转
- **保护机制**：防止外层循环误伤内层循环的块
- **end_bb 清空+显式管理**：确保循环结束块有正确的 terminator
- **修复循环**：统一给循环体内缺 terminator 的块添加正确回边

---

## 困难测试用例深度调试记录

本章开发过程中，测试 04、05、11 反复卡住，经历了多轮修复。以下是每个测试的完整调试历程。

---

### 测试 04_while_if — 循环体内 if 的回边断裂

#### 源码与预期行为

```c
int main() {
  int i = 0, sum = 0;
  while (i < 10) {           // 外层 while
    if (i == 5) {            // if-else 在循环体内
      sum = sum + 1;
    } else {
      sum = sum + i;
    }
    if (sum > 10) sum = sum - 1;
    i = i + 1;
  }
  return sum;
}
```

**预期结果**：i 从 0 遍历到 9，计算 sum。手动追踪：
```
i=0: sum=0+0=0;   sum>10? no; i=1
i=1: sum=0+1=1;   sum>10? no; i=2
i=2: sum=1+2=3;   sum>10? no; i=3
i=3: sum=3+3=6;   sum>10? no; i=4
i=4: sum=6+4=10;  sum>10? no; i=5
i=5: sum=10+1=11; sum>10? yes→10; i=6
i=6: sum=10+6=16; sum>10? yes→15; i=7
i=7: sum=15+7=22; sum>10? yes→21; i=8
i=8: sum=21+8=29; sum>10? yes→28; i=9
i=9: sum=28+9=37; sum>10? yes→36; i=10 → 退出循环
返回 36
```

#### 正确 IR（最终版本）

```
fun @main(): i32 {
%entry:
  %0 = alloc i32; store 0, %0       ; i = 0
  %1 = alloc i32; store 0, %1       ; sum = 0
  jump %w7                          ; 进入 while

%w7:                               ; while cond: i < 10 ?
  %3 = load %0
  %4 = lt %3, 10
  br %4, %w8, %w9                   ; true→body, false→end

%w8:                               ; while body（包含 if-else）
  %5 = load %0
  %6 = eq %5, 5                     ; if (i == 5)
  br %6, %b22, %b23                 ; then→b22, else→b23

%w9:                               ; while end
  %20 = load %1
  ret %20                           ; return sum

%b22:                              ; if then: sum = sum + 1
  %8 = load %1
  %9 = add %8, 1
  store %9, %1
  jump %b24                         ; → if-end

%b23:                              ; if else: sum = sum + i
  %10 = load %1
  %11 = load %0
  %12 = add %10, %11
  store %12, %1
  jump %b24                         ; → if-end

%b24:                              ; if-end: 第二个 if (sum > 10)
  %13 = load %1
  %14 = gt %13, 10
  br %14, %b46, %b48                ; true→减1, false→继续

%b46:                              ; sum = sum - 1
  %16 = load %1
  %17 = sub %16, 1
  store %17, %1
  jump %b48                         ; → 后续代码

%b48:                              ; i = i + 1
  %18 = load %0
  %19 = add %18, 1
  store %19, %0
  jump %w7                          ; ★ 回边到 while cond！
}
```

#### 调试历程

**问题阶段 1：IfStmtAST 的 then/else 分支不跳转到 end 块**

早期实现中，为了"避免循环体内的自动跳转"，曾尝试让 IfStmtAST 在 `g_loop_depth > 0` 时**不添加** then/else 到 end 块的 jump：

```cpp
// 错误做法！
if (g_loop_depth == 0 && !then_bb->HasTerminator()) {
  then_bb->AddInst(std::make_unique<JumpInst>(end_label));
}
if (g_loop_depth == 0 && !else_bb->HasTerminator()) {
  else_bb->AddInst(std::make_unique<JumpInst>(end_label));
}
```

**后果**：`%b22`(then) 和 `%b23`(else) 没有 terminator，控制流从它们直接"掉落"到下一个物理块——而下一个物理块恰好是 `%w9`(while end)，导致：
- 第一次迭代中 if(i==5) 为假时，从 b23 掉落到 w9 → 直接 return sum（此时 sum 值不对）
- 或者更糟糕的情况：控制流完全混乱

**错误 IR 表现**：
```
%b22:          ; then 分支 — 无 terminator！
  ...
  ; 缺少 jump %b24
%b23:          ; else 分支 — 无 terminator！
  ...
  ; 缺少 jump %b24
%b24:          ; 但执行到这里了？还是跳过了？
```

**修复**：恢复 IfStmtAST 中 then/else 到 end 的无条件 jump（无论是否在循环体内）。if 内部的跳转结构是**局部的、自洽的**——它只负责把 then/else 汇聚到自己的 end 块，不应该被外部循环状态影响。

```cpp
// 正确做法：IfStmtAST 总是添加内部汇聚跳转
if (!then_bb->HasTerminator()) {
  then_bb->AddInst(std::make_unique<JumpInst>(end_label));  // 无条件添加
}
if (!else_bb->HasTerminator()) {
  else_bb->AddInst(std::make_unique<JumpInst>(end_label));  // 无条件添加
}
```

**问题阶段 2：while body 最后缺少回边**

恢复了 if 的内部跳转后，又遇到了新问题：`%b48`(i=i+1 之后) 没有跳转回到 `%w7`(while cond)。这是因为当时 BlockAST 的自动跳转被 `g_loop_depth > 0` 完全抑制了。

**错误表现**：程序只执行一次循环体就返回了错误的 sum 值。

**根因分析**：BlockAST 和 WhileStmtAST 的职责边界不清。
- **BlockAST 的职责**：处理完一个语句块后，如果当前块没有 terminator 且不在循环内，跳到 if-end 栈顶
- **WhileStmtAST 的职责**：确保循环体所有路径最终都能回到 cond 块

两者不能简单地用一个 `g_loop_depth` 标志来互斥。正确的设计是：
- BlockAST 在循环体内**不添加**自动跳转（因为不知道应该跳到哪里）
- WhileStmtAST 在结束时**统一扫描并修复**所有缺 terminator 的块

**最终修复**：
```cpp
// WhileStmtAST 中的修复循环
for (size_t i = block_count_before; i < g_current_func->GetBlockCount(); ++i) {
  BasicBlock* blk = g_current_func->GetBlock(i);
  if (blk == cond_bb || blk == body_bb || blk == end_bb) continue;
  if (blk->IsProtected()) continue;

  if (!blk->HasTerminator() && !blk->IsEmpty()) {
    blk->AddInst(std::make_unique<JumpInst>(cond_label));  // 加回边！
  }
}
```

这样 `%b48` 就会被修复循环发现没有 terminator，并被加上 `jump %w7`。

#### 关键教训

| 教训 | 说明 |
|------|------|
| **局部自洽性** | IfStmtAST 的内部跳转结构是完整的，不应受外部循环状态影响 |
| **职责分离** | BlockAST 管理非循环内的自动跳转；WhileStmtAST 管理循环内的回边 |
| **统一修复优于逐个修补** | 不要在每个 Stmt 类型中分别处理循环回边，而是由 WhileStmtAST 统一扫描 |

---

### 测试 05_if_while — else 分支中 while 的回边被劫持

#### 源码与预期行为

```c
int main() {
  int a = 1;
  if (a > 1) {             // a=1, 不进入
    while(1);              // 死循环（但不会执行）
  } else {                  // 进入 else
    while (a < 10) {        // 执行这个 while
      a = a + 2;
    }
  }
  return a;
}
```

**预期行为**：a=1 不满足 `a > 1`，进入 else 分支的 while：
```
a: 1 → 3 → 5 → 7 → 9 → 11 (不满足 < 10)
返回 11
```

#### 正确 IR（最终版本）

```
fun @main(): i32 {
%entry:
  %0 = alloc i32; store 1, %0     ; a = 1
  %1 = load %0
  %2 = gt %1, 1                    ; if (a > 1)
  br %2, %b10, %b11                 ; false→b11(else)

%b10:                              ; if then: while(1);
  jump %w13                        ; 进入死循环

%b11:                              ; if else: while(a < 10)
  jump %w16                        ; 进入中层 while

%b12:                              ; if-end / while-end 汇聚点
  %10 = load %0
  ret %10                          ; return a

%w13:                              ; while(1) 的 cond
  br 1, %w14, %w15                 ; always true→body
%w14:                              ; while(1) 的 body（空）
  jump %w13                        ; 回边 → 死循环 ♻️
%w15:                              ; while(1) 的 end（不可达）
  jump %b12                        ; → return

%w16:                              ; while(a<10) 的 cond
  %6 = load %0
  %7 = lt %6, 10
  br %7, %w17, %w18                ; true→body, false→end
%w17:                              ; while(a<10) 的 body
  %8 = load %0
  %9 = add %8, 2
  store %9, %0
  jump %w16                        ; ★ 正确回边到 %w16！
%w18:                              ; while(a<10) 的 end
  jump %b12                        ; → return a
}
```

#### 调试历程

**错误现象**：返回 **3**（预期 11）

只执行了一次循环体（a: 1→3）就退出了！

**错误 IR（中间版本）**：
```
%w17:                              ; while(a<10) 的 body
  %8 = load %0
  %9 = add %8, 2
  store %9, %0
  jump %b12        ; ❌ 错误！跳到了 if-end 而不是 while-cond %w16
```

**根因分析**：这是 **BlockAST 自动跳转与 while 回边的冲突**问题。

执行流程追踪：
1. 进入 `if(a>1)` 的 else 分支 → `%b11`
2. `%b11` 跳到 `%w16`(while cond)
3. `%w16`: a=1 < 10 → true → `%w17`(body)
4. `%w17`: a = 1+2 = 3, store
5. **关键点**：`%w17` 处理完后，BlockAST 发现当前块没有 terminator
6. 此时 `g_end_block_stack` 栈顶是 `%b12`(if 的 end 块)
7. BlockAST 给 `%w17` 加上了 `jump %b12`
8. **但是**！`%w17` 应该加的是 `jump %w16`(while cond 的回边)

**为什么会出错**：当时的代码没有区分"循环体内部的正常出口"和"跳出循环"。BlockAST 不知道自己正在处理的是循环体的一部分，所以按常规逻辑跳到了 if-end 栈顶。

**第一次尝试修复（错误方案）**：在 WhileStmtAST 的 body 结束后强制覆盖最后一条指令：

```cpp
// 错误做法：直接覆盖
BasicBlock* last = g_current_bb;
if (last && !last->IsProtected() && last->HasTerminator()) {
  last->RemoveLastInst();         // 删除 BlockAST 加的错误 jump
  last->AddInst(std::make_unique<JumpInst>(cond_label));  // 替换为正确回边
}
```

这虽然能修好 05，但会导致其他测试出问题——有些情况下 BlockAST 加的 jump 是正确的，不该被覆盖。

**第二次尝试修复（部分有效）**：使用 `g_in_loop_body` 布尔标志阻止 BlockAST 添加自动跳转：

```cpp
thread_local bool g_in_loop_body = false;

// WhileStmtAST 中
g_in_loop_body = true;
body->GenIR(body_bb, builder, symtab);
g_in_loop_body = false;

// BlockAST 中
if (g_current_bb && !g_current_bb->HasTerminator() && !g_in_loop_body) {
  // 只在不循环体内才添加自动跳转
  ...
}
```

这在简单场景下有效，但嵌套循环中 `g_in_loop_body` 是布尔值——内层循环结束后会设为 false，导致外层循环体的后续代码又能触发自动跳转。

**最终修复**：将布尔标志改为整数计数器 `g_loop_depth`：

```cpp
thread_local int g_loop_depth = 0;

// WhileStmtAST 中
g_loop_depth++;            // 进入循环体前 +1
body->GenIR(body_bb, builder, symtab);
g_loop_depth--;            // 出循环体后 -1

// BlockAST 中
if (g_current_bb && !g_current_bb->HasTerminator() && g_loop_depth == 0) {
  // 只有当完全不在任何循环体内时才添加自动跳转
  ...
}
```

**为什么计数器能解决嵌套问题**：

```
外层 while: g_loop_depth: 0 → 1
  ├─ if(a==1): 不影响 depth
  │    └─ 中层 while: g_loop_depth: 1 → 2
  │         ├─ 内层 while: g_loop_depth: 2 → 3
  │         │    └─ ... (depth 最终回到 2)
  │         └─ (depth 最终回到 1)
  └─ (depth 最终回到 0)

只有当 depth 回到 0 时，BlockAST 才会添加自动跳转
→ 这意味着只有最外层的、不在任何循环内的代码才会触发
→ 循环体内的代码全部由 WhileStmtAST 的修复循环统一管理
```

#### 关键教训

| 教训 | 说明 |
|------|------|
| **布尔 vs 计数器** | 嵌套场景下必须用计数器而非布尔标志来跟踪状态 |
| **BlockAST 的盲区** | BlockAST 不知道自己在循环体内，无法做出正确的跳转决策 |
| **防御性设计** | 与其试图让 BlockAST "聪明地"判断何时该跳转，不如让它简单地在循环体内静默，由 WhileStmtAST 统一负责 |

---

### 测试 11_complex — 三层嵌套循环的死循环地狱

#### 源码与预期行为

```c
int main() {
  while (1) {                       // L0: 外层死循环（只能通过 break/return 退出）
    int a = 1, b = 2;
    {                               // 匿名 Block
      if (a == 1) {                 // a=1, 进入 then
        while (a < b) {             // L1: 中层 while (1<2 → 进入)
          while (a < b || b-1==0) { // L2: 内层 while
            a = a + 1;              // a: 1→2 (此时 a<b? 2<2? false)
          }                          // L2 结束
          b = 1;                     // b = 1
          a = a + 1;                 // a = 3
          if (3) continue;           // 恒真 → continue L1
        }                            // L1: a=3, b=1, 3<1? false → L1 结束
      } else if (b == 6) {          // 不进入
        break;
      }
      int b = 6;                     // 新变量 b 遮蔽外层 b, b_new = 6
      if (b == 6)                    // 6==6 → true
        return 8 * (10 || b);        // 返回 8*10 = 80
      else while (0);               // 不执行
    }
  }
  return -1;                         // 不可达
}
```

**预期结果**：返回 **80**

**执行追踪**：
```
L0: while(1) → 进入
  a=1, b=2
  if(a==1) → true
    L1: while(a<b) → 1<2 → true
      L2: while(a<b || b-1==0) → 1<2 || 1==0 → true||false → true
        a = 2
      L2: 2<2 || 1==0 → false||false → false → L2 结束
      b = 1
      a = 3
      if(3) → continue L1
    L1: 3<1 → false → L1 结束
  int b = 6 (遮蔽)
  if(b==6) → true → return 8*(10||6) = 8*10 = 80
```

#### 正确 IR（最终版，关键路径标注）

```
fun @main(): i32 {
%entry:
  jump %w1

%w1:                               ; L0 cond: while(1)
  br 1, %w2, %w3                    ; always → w2

%w2:                               ; L0 body
  %1 = alloc i32; store 1, %1       ; a = 1
  %2 = alloc i32; store 2, %2       ; b = 2
  %3 = load %1
  %4 = eq %3, 1                     ; if (a == 1)
  br %4, %b16, %b17                 ; true→b16

%w3:                               ; L0 end (不可达)
  ret -1

%b16:                              ; if then: 进入 L1
  jump %w19

%b17:                              ; if else-if (不执行)
  %22 = load %2
  %23 = eq %22, 6
  br %23, %b73, %b18                ; → b18

%b18:                              ; if-else-end → 匿名 Block 继续
  %25 = alloc i32; store 6, %25     ; int b = 6 (遮蔽)
  %26 = load %25
  %27 = eq %26, 6                   ; if (b == 6)
  br %27, %b85, %b86                ; true→b85 → return 80!

%w19:                               ; L1 cond: while (a < b)
  %7 = load %1                      ; a
  %8 = load %2                      ; b (外层 b!)
  %9 = lt %7, %8
  br %9, %w20, %w21                 ; 1<2=true→w20, 3<1=false→w21

%w20:                               ; L1 body
  jump %w31                         ; → L2

%w21:                               ; L1 end: a>=b, 跳到匿名 Block
  jump %b18                         ; ★ 正确！不是隐式跳到 L2！

%w31:                               ; L2 cond: while (a<b || b-1==0)
  %11 = load %1; %12 = load %2
  %13 = lt %11, %12                 ; a < b
  %14 = load %2; %15 = sub %14, 1
  %16 = eq %15, 0                   ; b-1 == 0
  %17 = ne %13, 0; %18 = ne %16, 0
  %19 = or %17, %18                 ; (a<b) || (b-1==0)
  br %19, %w32, %w33

%w32:                               ; L2 body
  %20 = load %1; %21 = add %20, 1
  store %21, %1                      ; a = a + 1
  jump %w31                          ; ★ 正确回边到 L2 cond！

%w33:                               ; L2 end
  jump %b18                          ; → 匿名 Block

%b73:                              ; break (不执行)
  jump %w3                          ; → L0 end

%b85:                              ; return 80!
  %29 = load %25
  %30 = ne 10, 0; %31 = ne %29, 0
  %32 = or %30, %31                 ; 10 || b = 10 || 6 = 10
  %33 = mul 8, %32                  ; 8 * 10 = 80
  ret %33

%b86:                              ; else while(0) (不执行)
  jump %w103
...
}
```

#### 调试历程（共经历 ~10 轮修复）

##### 第 1-3 轮：基础功能调通

前几轮主要解决基本的 while/break/continue 功能：
- Round 1: 基本语法通过（00-03 通过）
- Round 2: if-while 组合基本工作（04 通过）
- Round 3: break/continue 基本可用（07-10 通过）

此时 11 的状态：**TIME LIMIT EXCEEDED（死循环）**

##### 第 4-5 轮：发现隐式 Jump 问题

**错误 IR（第 4 轮）**：
```
%w19:                               ; L1 cond
  br ..., %w20, %w21
%w20:                               ; L1 body (无 terminator)
                                    ↓ 隐式 jump 到下一个物理块！！
%w21:                               ; L1 end (空，无 terminator)
                                    ↓ 隐式 jump 到下一个物理块！！
%w31:                               ; L2 cond ← 被错误指向！
  br ..., %w32, %w33
%w32:                               ; L2 body
  jump %w31                         ; 正确回边
%w33:                               ; L2 end
  ... → b18 → w19                   ; 回到 L1 cond
                                    ↓
                            ♻️ 死循环！w19→w20→w21(隐式→w31)→w32→w33→b18→w19
```

**死循环路径图解**：
```
         ┌─── L1 cond (%w19) ──┐
         │  true↓         false│
    ┌────┤              ┌──────┤
    │    │              │ 隐式 │
    │  L1 body     L1 end│ jump │
    │  (%w20)      (%w21)│      │
    │    │              │      │
    │    │         ┌────┘      │
    │    │         ↓ 错误目标   │
    │    │    L2 cond (%w31)◄───┘
    │    │      ↓true  ↑回边
    │    │   L2 body(%w32)
    │    │      ↓false
    │    │   L2 end (%w33) → b18 → w19 ♻️
    └────┘
```

**根因**：`Function::Dump()` 会设置 `blocks[i]->next_block = blocks[i+1]->GetName()`，然后 `BasicBlock::Dump()` 对无 terminator 的块输出 `jump %next_block`。`%w21` 物理上位于 `%w31` 之前，所以隐式跳转指向了 `%w31`。

**第 5 轮尝试**：给 `%w21` 加 terminator 来阻止隐式 jump。但这引入了新问题——`%w21` 应该跳到哪里？

##### 第 6-7 轮：end_bb 目标之争

尝试了几种 `%w21`(L1 end) 的跳转目标：

| 尝试 | 目标 | 结果 |
|------|------|------|
| A | 不跳转（保持空） | 隐式 jump 还是会生效 |
| B | `jump %b18`(if-end) | 04 通过，但 05 失败（else 中 while 的 end 跳到了 if-end） |
| C | `jump %w19`(自身 cond) | 形成空循环，某些 case 死循环 |
| D | `jump %g_end_block_stack.back()` | 有时有栈有时没栈，不稳定 |

核心矛盾：**L1 的 end 块不知道自己应该在什么上下文中被使用**——它可能在 if 内部（需要跳到 if-end），也可能在顶层（不需要跳转），还可能在另一个循环内（需要跳到外层循环的某个位置）。

##### 第 8 轮：保护机制引入

为了防止 L1 的修复循环修改 L2 的块，引入了 `SetProtected`：

```cpp
cond_bb->SetProtected(true);
body_bb->SetProtected(true);
```

同时尝试只保护 `cond_bb`，让 `body_bb` 能被正常修复。但这时 `%w20`(L1 body) 被 L1 的修复循环加了 `jump %w19`——这是对的！但 `%w21`(L1 end) 仍然有问题。

##### 第 9 轮：去掉隐式 Jump（重大架构改变）

**决定性突破**：完全移除隐式 jump 机制。

```cpp
// Function::Dump() — 不再设置 next_block
for (size_t i = 0; i < blocks.size(); ++i) {
  blocks[i]->Dump(os);  // 直接 Dump，不做预处理
}

// BasicBlock::Dump() — 不再输出隐式 jump
void Dump(std::ostream &os) const {
  os << "%" << name << ":\n";
  for (const auto &inst : insts) {
    inst->Dump(os);
  }
  // 删掉了：if (!HasTerminator() && !next_block.empty()) → "  jump %next_block"
}
```

**效果**：`%w21` 不再有错误的隐式 jump 了。但它现在是空的——没有 terminator。

**配合修改**：WhileStmtAST 显式管理 end_bb：

```cpp
// 清空可能被 IfStmtAST 等错误填充的指令
while (!end_bb->IsEmpty()) {
  end_bb->RemoveLastInst();
}

// 空 end_bb → 如果有 if-end 栈，跳到栈顶
if (end_bb->IsEmpty() && !g_end_block_stack.empty()) {
  BasicBlock* target = g_end_block_stack.back();
  if (end_bb != target) {
    end_bb->AddInst(std::make_unique<JumpInst>(target->GetName()));
  }
}
```

对于 11_complex.c 中的 `%w21`(L1 end)：
- 它在 `if(a==1)` 的 then 分支内部
- `g_end_block_stack` 栈顶是这个 if 的某个 end 块
- 但实际上 `%w21` 需要跳到 `%b18`(if-else 之后的匿名 Block)

这里的关键洞察是：**WhileStmtAST 的 end_bb 不应该由 WhileStmtAST 自己决定跳到哪里**。它应该保持"干净"（空），让后续的代码自然地从它继续。但如果它是函数/块的最后一个块且为空，`Function::Dump()` 兜底输出 `ret 0`。

##### 第 10 轮：body_bb 保护 + 单独处理

最后一轮修复解决了 `%w20`(L1 body) 被错误修复的问题：

**场景**：L1 的修复循环遍历所有新创建的块时，发现了 `%w31`(L2 cond，属于内层 while)。如果 `%w31` 没有被保护，L1 可能会给它加上 `jump %w19`(L1 cond)，这就错了。

**解决方案**：
```cpp
// 1. 保护 cond 和 body
cond_bb->SetProtected(true);
body_bb->SetProtected(true);

// 2. 修复循环跳过保护的块
for (...) {
  if (blk->IsProtected()) continue;  // 跳过内层 while 的 cond/body
  ...
}

// 3. body_bb 单独处理（它属于当前 while，不是内层的）
if (body_bb && !body_bb->HasTerminator()) {
  body_bb->AddInst(std::make_unique<JumpInst>(cond_label));
}
```

**为什么 body_bb 要单独处理**：`body_bb` 是当前 while 的入口块，它**必须**有回边到 `cond_label`（除非已经有 terminator，比如以 break/return/continue 结束的情况）。但 `SetProtected(true)` 会阻止修复循环处理它，所以需要在修复循环之外单独给它加回边。

#### 最终通过的 IR 关键验证点

| 检查项 | 块 | 正确行为 | 验证 |
|--------|-----|----------|------|
| L1 body 回边 | `%w20` | `jump %w19`(L1 cond) | ✅ 由修复循环+单独处理保证 |
| L1 end 跳转 | `%w21` | `jump %b18`(匿名 Block) | ✅ 由 end_bb 显式管理保证 |
| L2 body 回边 | `%w32` | `jump %w31`(L2 cond) | ✅ 由内层 while 自己的修复循环保证 |
| L2 end 跳转 | `%w33` | `jump %b18`(匿名 Block) | ✅ 由内层 while 的 end_bb 管理 |
| 无隐式 jump | 所有空块 | 不产生基于物理位置的 jump | ✅ 隐式机制已移除 |
| 无跨层误改 | 内层块 | 不被外层修复循环修改 | ✅ SetProtected 保证 |

#### 关键教训

| 教训 | 说明 |
|------|------|
| **隐式推断不可靠** | 基于 vector 物理位置的隐式 jump 在嵌套结构中必然出错 |
| **显式管理是唯一出路** | 每个 terminator 必须由创建它的 AST 节点明确负责 |
| **保护粒度要精确** | 只保护真正需要保护的块（cond/body），不要过度保护（end） |
| **end_bb 是最难的块** | 它是多个控制流结构的交汇点，最容易出错 |
| **分层测试策略** | 先通简单测试(00-03)，再组合(if+while, 04-06)，再极端嵌套(11) |
| **打印 IR 是最好的调试工具** | 每次修改后都打印完整 IR，手工追踪控制流路径 |
