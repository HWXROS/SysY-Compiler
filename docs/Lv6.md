# Lv6. if 语句实现笔记

## 概述

本章实现了支持 if/else 语句的编译器。if 语句是控制流的基础，允许程序根据条件执行不同的代码路径。

## 语法扩展

### 词法分析 (sysy.l)

添加两个新的关键字：

```lex
"if"            { return IF; }
"else"          { return ELSE; }
```

### 语法分析 (sysy.y)

#### 语法规则

```yacc
Stmt
  : IF '(' Exp ')' Stmt {
      // if 语句（无 else）
    }
  | IF '(' Exp ')' Stmt ELSE Stmt {
      // if-else 语句
    }
  ;
```

#### 就近匹配原则

else 总是与最近的未匹配的 if 匹配，例如：
```c
if (x) if (y) ...; else ...;
// 等价于
if (x) {
  if (y) {
    ...;
  } else {
    ...;
  }
}
```

## AST 设计

### IfStmtAST 类

```cpp
class IfStmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> cond;       // 条件表达式
  std::unique_ptr<BaseAST> then_stmt;  // then 分支语句
  std::unique_ptr<BaseAST> else_stmt;  // else 分支语句（可选）
};
```

### 结构图

```
CompUnit (编译单元)
  │
  └── FuncDef (函数定义)
       │
       ├── FuncType (函数类型)
       ├── IDENT (函数名)
       └── Block (函数体/语句块)
            │
            └── BlockItem (块内项目)
                 │
                 ├── Decl (声明)
                 │    └── ... (同 Lv5)
                 │
                 └── Stmt (语句)
                      │
                      ├── ... (Lv5 的语句类型)
                      │
                      └── IfStmt (if 语句)
                           │
                           ├── "if"
                           ├── "("
                           ├── Exp (条件表达式)
                           │    └── ... (同 Lv5)
                           ├── ")"
                           ├── Stmt (then 分支)
                           │    └── ... (递归结构)
                           └── ["else" Stmt] (else 分支，可选)
                                └── ... (递归结构)
```

## IR 生成

### 基本块结构

if/else 语句需要生成多个基本块：

```
entry:
  ; 计算条件
  br %cond, %true_label, %false_label

true_label:
  ; then 分支代码
  [jump %end_label]  ; 如果没有以 ret 结束

false_label:
  ; else 分支代码（如果有）
  [jump %end_label]  ; 如果没有以 ret 结束

end_label:
  ; if 语句后的代码（如果有）
```

### 分支指令

在 ir.h 中添加两种跳转指令：

```cpp
// 条件分支
class BranchInst : public Instruction {
  std::unique_ptr<KoopaValue> cond;
  std::string true_label;
  std::string false_label;
};

// 无条件跳转
class JumpInst : public Instruction {
  std::string target_label;
};
```

### IR 生成算法

```cpp
std::unique_ptr<KoopaValue> IfStmtAST::GenIR(...) {
  // 1. 生成条件表达式
  auto cond_val = cond->GenIR(bb, builder, symtab);
  
  // 2. 创建标签
  int id = builder.NewId();
  std::string true_label = "true" + std::to_string(id);
  std::string false_label = "false" + std::to_string(id);
  std::string end_label = "end" + std::to_string(id);
  
  // 3. 生成条件分支指令
  bb->AddInst(std::make_unique<BranchInst>(std::move(cond_val), true_label, false_label));
  
  // 4. 生成 then 分支基本块
  BasicBlock* true_bb = g_current_func->CreateBlock(true_label);
  then_stmt->GenIR(true_bb, builder, symtab);
  
  // 5. 检查 then 分支是否以跳转/返回结束
  if (!then_jumps) {
    true_bb->AddInst(std::make_unique<JumpInst>(end_label));
  }
  
  // 6. 生成 else 分支基本块（如果有）
  if (else_stmt) {
    BasicBlock* false_bb = g_current_func->CreateBlock(false_label);
    else_stmt->GenIR(false_bb, builder, symtab);
    
    if (!else_jumps) {
      false_bb->AddInst(std::make_unique<JumpInst>(end_label));
    }
  } else {
    // 没有 else 分支，false 直接跳转到 end
    BasicBlock* false_bb = g_current_func->CreateBlock(false_label);
    false_bb->AddInst(std::make_unique<JumpInst>(end_label));
  }
  
  // 7. 生成 end 基本块（如果需要）
  if (need_end_block) {
    g_current_func->CreateBlock(end_label);
  }
  
  return nullptr;
}
```

### 全局上下文

为了在 AST 节点中向函数添加多个基本块，使用全局上下文：

```cpp
// ast.h
extern thread_local Function* g_current_func;
extern thread_local BasicBlock* g_current_bb;
extern thread_local std::vector<BasicBlock*> g_end_block_stack;

// ast.cpp
thread_local Function* g_current_func = nullptr;
thread_local BasicBlock* g_current_bb = nullptr;
thread_local std::vector<BasicBlock*> g_end_block_stack;
```

### Function 类扩展

```cpp
class Function {
  std::vector<std::unique_ptr<BasicBlock>> blocks;
 public:
  BasicBlock* CreateBlock(const std::string &name) {
    blocks.push_back(std::make_unique<BasicBlock>(name));
    return blocks.back().get();
  }
  BasicBlock* getBlock(size_t index) { return blocks[index].get(); }
  size_t blockCount() const { return blocks.size(); }
};
```

## RISC-V 代码生成

### 分支指令映射

| Koopa IR | RISC-V |
|----------|--------|
| `br %cond, %true, %false` | `bnez a0, %true` + `j %false` |
| `jump %target` | `j %target` |

### Visit 函数实现

```cpp
void RiscVGenerator::Visit(const koopa_raw_branch_t &branch) {
  // 生成条件代码
  Visit(branch.cond);
  
  // 获取标签
  std::string true_label = "%" + std::string(branch.true_bb->name);
  std::string false_label = "%" + std::string(branch.false_bb->name);
  
  // 生成条件跳转
  *os_ << "  bnez a0, " << true_label << "\n";
  *os_ << "  j " << false_label << "\n";
}

void RiscVGenerator::Visit(const koopa_raw_jump_t &jump) {
  std::string target_label = "%" + std::string(jump.target->name);
  *os_ << "  j " << target_label << "\n";
}
```

---

## 错误剖析与解决方案

### 错误1：基本块顺序问题

**问题描述**：
entry 块必须在函数的第一个位置，但 if 语句生成的块可能插入到错误的位置。

**错误现象**：
```
fun @main(): i32 {
%b22:           ; 错误！entry 不是第一个块
  store 3, %1
  jump %b24
%entry:         ; entry 块应该在最前面
  %0 = alloc i32
  ...
}
```

**解决方案**：
在 `FuncDefAST::GenIR()` 中，先创建并添加 entry 块到函数中，然后再处理函数体内的语句：

```cpp
std::unique_ptr<Function> FuncDefAST::GenIR() const {
  auto func = std::make_unique<Function>(ident, "i32");
  g_current_func = func.get();
  
  auto entry_bb = std::make_unique<BasicBlock>("entry");
  g_current_bb = entry_bb.get();
  func->AddBlock(std::move(entry_bb));  // 先添加 entry 块
  
  block->GenIR(g_current_bb, builder, symtab);  // 再处理语句
  return func;
}
```

---

### 错误2：return 语句后的冗余跳转

**问题描述**：
在 return 语句后面又生成了 jump 指令，导致不可达代码。

**错误现象**：
```
%true3:
  ret 1
  jump %end3    ; 错误！return 后的代码不可达
%end3:
```

**解决方案**：
在 `BasicBlock` 类中添加 `HasTerminator()` 方法，检查块是否已有终止指令：

```cpp
class BasicBlock {
 public:
  bool HasTerminator() const {
    if (insts.empty()) return false;
    auto last = insts.back().get();
    return dynamic_cast<RetInst*>(last) || 
           dynamic_cast<JumpInst*>(last) || 
           dynamic_cast<BranchInst*>(last);
  }
};
```

在添加 jump 指令前检查：
```cpp
if (!then_bb->HasTerminator()) {
  then_bb->AddInst(std::make_unique<JumpInst>(end_bb->GetName()));
}
```

---

### 错误3：基本块名称冲突

**问题描述**：
使用 "true"、"false"、"else" 等关键字作为基本块名称，与 Koopa IR 关键字冲突。

**错误现象**：
```
error: invalid basic block name '%else_3'
```

**解决方案**：
使用纯数字编号的基本块名称：
```cpp
int id = builder.NewId();
std::string then_label = "b" + std::to_string(id * 3 + 1);
std::string else_label = "b" + std::to_string(id * 3 + 2);
std::string end_label = "b" + std::to_string(id * 3 + 3);
```

---

### 错误4：空基本块导致解析错误

**问题描述**：
创建了空的基本块（没有任何指令），导致 Koopa IR 解析器报错。

**错误现象**：
```
error: expected character '=', found character ':'
```

**解决方案**：
在 `BasicBlock::Dump()` 中跳过空块：
```cpp
void BasicBlock::Dump(std::ostream &os) const {
  if (insts.empty()) return;  // 跳过空块
  os << "%" << name << ":\n";
  for (const auto &inst : insts) {
    inst->Dump(os);
  }
}
```

---

### 错误5：嵌套 if 语句的 end 块管理（最关键的问题）

**问题描述**：
在嵌套的 if 语句中，内层 if 语句的 end 块没有正确跳转到外层的 end 块，导致控制流错误。

**测试用例** (7_complex.c)：
```c
int main() {
  int a = 0;
  const int b = 1 - 1 * 2 + 1;  // b = 0
  int c = 1, d = 2;
  if (a || b) {        // 0 || 0 = false，进入 else
    c = 3;
  } else {
    d = 3;             // d = 3
    int a = 1;         // 遮蔽外层 a
    if (a || b) {      // 1 || 0 = true
      c = 4;           // c = 4
    } else {
      d = 4;
    }
    if (a == 0) return 1;           // a=1，不执行
    else if (a == 0 && a == -1) return 2;  // a=1，不执行
  }
  return a + b + c + d;  // 应该返回 0 + 0 + 4 + 3 = 7
}
```

**错误现象**：
```
your answer: 2
expected: 7
```

**错误分析**：

生成的错误 IR：
```
%b50:
  br %23, %b73, %b51   ; if (a == 0 && a == -1) return 2; else ...
%b51:
  jump %b73            ; 错误！应该跳到 %b24（外层 if 的 end 块）
%b73:
  ret 2                ; 错误地返回 2
```

问题根源：
1. if3: `if (a == 0) return 1; else if (...) return 2;`
2. if3 有 else 分支，创建了 end 块（%b51）并 push 到栈
3. if4（else if 部分）没有 else 分支，复用了栈顶的 end 块（%b51）
4. 当 if4 处理完后，%b51 没有 terminator，需要跳到外层的 end 块
5. 但代码没有正确处理这种情况，导致 %b51 错误地跳到了 %b73

**解决方案**：

使用 end 块栈来管理嵌套 if 语句的结束块：

```cpp
// 全局变量
thread_local std::vector<BasicBlock*> g_end_block_stack;

// IfStmtAST::GenIR() 中
if (else_stmt) {
  // 有 else 分支，创建新的 end 块
  end_bb = g_current_func->CreateBlock(end_label);
  g_end_block_stack.push_back(end_bb);
  pushed_to_stack = true;
} else {
  // 没有 else 分支，复用栈顶的 end 块
  if (g_end_block_stack.empty()) {
    end_bb = g_current_func->CreateBlock(end_label);
    g_end_block_stack.push_back(end_bb);
    pushed_to_stack = true;
  } else {
    end_bb = g_end_block_stack.back();  // 复用外层的 end 块
  }
}
```

在 `BlockAST::GenIR()` 中，处理完所有语句后，检查是否需要跳转到外层的 end 块：

```cpp
std::unique_ptr<KoopaValue> BlockAST::GenIR(...) {
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
  
  // 关键修复：处理完所有语句后，检查是否需要跳转到外层的 end 块
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
```

**修复后的正确 IR**：
```
%b50:
  br %23, %b73, %b51   ; if (a == 0 && a == -1) return 2; else ...
%b51:
  jump %b24            ; 正确！跳到外层 if 的 end 块
%b73:
  ret 2
%b24:
  %14 = load %0
  %15 = add %14, 0
  %16 = load %1
  %17 = add %15, %16
  %18 = load %2
  %19 = add %17, %18
  ret %19              ; 正确返回 7
```

---

## 关键经验总结

### 1. 控制流管理

- **end 块栈**：使用栈结构管理嵌套 if 语句的结束块，确保内层 if 结束后能正确跳转到外层
- **terminator 检查**：在添加跳转指令前，必须检查块是否已有终止指令
- **块复用**：没有 else 分支的 if 语句应该复用外层的 end 块，避免创建冗余块

### 2. 调试技巧

- **打印 IR**：遇到控制流问题时，首先打印生成的 IR，手动追踪控制流路径
- **简化测试**：创建最小化的测试用例，逐步验证每个 if 语句的行为
- **边界情况**：特别注意嵌套 if、空 else 分支、return 后的代码等情况

### 3. 设计原则

- **延迟跳转**：不要在 if 语句处理完后立即添加跳转，而是在 Block 中统一处理
- **全局状态**：使用全局变量跟踪当前函数、当前块和 end 块栈
- **防御性编程**：添加 null 检查和 terminator 检查，避免生成无效 IR

---

## 测试用例

### 1. 基本 if-else

```c
int main() {
  int a = 1;
  if (a == 1) {
    return 1;
  } else {
    return 0;
  }
}
```

生成的 IR：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 1, %0
  %1 = load %0
  %2 = eq %1, 1
  br %2, %b4, %b5
%b4:
  ret 1
%b5:
  ret 0
}
```

### 2. 无 else 的 if

```c
int main() {
  if (1) {
    return 1;
  }
  return 0;
}
```

### 3. 嵌套 if

```c
int main() {
  int a = 1;
  if (a == 1) {
    if (a == 2) {
      return 100;
    }
    return 1;
  }
  return 0;
}
```

### 4. 复杂嵌套（7_complex）

```c
int main() {
  int a = 0;
  const int b = 1 - 1 * 2 + 1;
  int c = 1, d = 2;
  if (a || b) {
    c = 3;
  } else {
    d = 3;
    int a = 1;
    if (a || b) {
      c = 4;
    } else {
      d = 4;
    }
    if (a == 0) return 1;
    else if (a == 0 && a == -1) return 2;
  }
  return a + b + c + d;  // 返回 7
}
```

## 总结

本章实现了 if/else 语句的完整编译流程：
1. 词法分析识别 if/else 关键字
2. 语法分析构建 if 语句 AST
3. IR 生成创建多基本块结构和分支指令
4. RISC-V 代码生成将分支指令映射到条件跳转指令

**最关键的挑战**是管理嵌套 if 语句的控制流，通过 end 块栈和在 Block 中统一处理跳转，成功解决了这个问题。
