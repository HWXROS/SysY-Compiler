
## 一、源程序

```c
int main() {
  int i = 0, pow = 1;       // 语句1
  while (i < 7) {            // 语句2
    pow = pow * 2;           // 语句3
    i = i + 1;               // 语句4
  }
  return pow;                // 语句5
}
```

---

## 二、AST 结构

解析后生成的 AST 如下（简化视图）：

```
CompUnitAST
 └── FuncDefAST (ident="main")
      └── BlockAST (items:)
           ├── VarDeclAST (变量声明 i, pow)
           │    ├── VarDefAST (ident="i", init=0)
           │    └── VarDefAST (ident="pow", init=1)
           ├── WhileStmtAST
           │    ├── cond: BinaryExprAST (<)
           │    │    ├── left: LValAST("i")
           │    │    └── right: NumberAST(7)
           │    └── body: BlockAST (items:)
           │         ├── StmtAST (ASSIGN, lval=LValAST("pow"), exp=BinaryExprAST(*))
           │         └── StmtAST (ASSIGN, lval=LValAST("i"), exp=BinaryExprAST(+))
           └── StmtAST (RETURN, exp=LValAST("pow"))
```

---

## 三、AST → IR 生成过程（按执行顺序）

### 步骤 1：`FuncDefAST::GenIR` — 初始化

```cpp
SymbolTable symtab;                        // 根符号表（parent=null）
auto entry_bb = std::make_unique<BasicBlock>("entry");
```

生成 IR:
```
%entry:
```

### 步骤 2：`VarDeclAST::GenIR` — 声明 i 和 pow

#### 2a. `VarDefAST` 处理 `i = 0`

```cpp
int addr_id = builder.NewId();            // addr_id = 0
bb->AddInst(std::make_unique<AllocInst>(addr_id));  // alloc i32
symtab.var_addrs["i"] = 0;                 // 注册到符号表

auto val = init_val->GenIR(...);           // NumberAST(0) → IntConst(0)
bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id)); // store 0, %0
```

生成 IR:
```
  %0 = alloc i32
  store 0, %0
```

#### 2b. `VarDefAST` 处理 `pow = 1`

```cpp
int addr_id = builder.NewId();            // addr_id = 1
bb->AddInst(std::make_unique<AllocInst>(addr_id));  // alloc i32
symtab.var_addrs["pow"] = 1;               // 注册到符号表

auto val = init_val->GenIR(...);           // NumberAST(1) → IntConst(1)
bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id)); // store 1, %1
```

生成 IR:
```
  %1 = alloc i32
  store 1, %1
```

**此时 %entry 块内容：**
```
%entry:
  %0 = alloc i32
  store 0, %0
  %1 = alloc i32
  store 1, %1
  jump %w7          ← while 入口跳转（后面 WhileStmt 添加）
```

---

### 步骤 3：`WhileStmtAST::GenIR` — while 循环

#### 创建 3 个基本块

```cpp
BasicBlock* cond_bb  = CreateBlock("w7");   // 条件判断块
BasicBlock* body_bb = CreateBlock("w8");   // 循环体块
BasicBlock* end_bb  = CreateBlock("w9");   // 循环结束块

current_bb->AddInst(std::make_unique<JumpInst>("w7"));  // 从 entry 跳到 cond
```

生成 IR:
```
  jump %w7           ← 添加到 %entry 末尾
```

#### 3a. 条件判断 — `cond_bb` 生成条件表达式

```cpp
auto cond_val = cond->GenIR(cond_bb, builder, symtab);
```

`cond: BinaryExprAST(i < 7)`:

```
%3 = load %0        ← 加载 i 的值（从 addr 0）
%4 = lt %3, 7       ← 比较 i < 7
br %4, %w8, %w9     ← 条件分支
```

生成 IR:
```
%w7:
  %3 = load %0
  %4 = lt %3, 7
  br %4, %w8, %w9
```

#### 3b. 循环体 — `body_bb` 处理 `pow = pow * 2`

`body: BlockAST` 进入后创建新的 `SymbolTable`（parent 指向上层 symtab，但这里没有新变量声明，所以还是查外层）：

```cpp
body->GenIR(body_bb, builder, symtab);
```

语句 `pow = pow * 2` → `StmtAST(ASSIGN)`:

```cpp
auto exp_val = exp->GenIR(...);             // BinaryExprAST(pow * 2)
int addr_id = symtab.GetVarAddr("pow");    // 查表得 addr=1
bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
```

`BinaryExprAST(pow * 2)`:

```
%5 = load %1        ← 加载 pow（从 addr 1）
%6 = mul %5, 2      ← 计算 pow * 2
```

生成 IR:
```
%w8:
  %5 = load %1
  %6 = mul %5, 2
  store %6, %1
```

#### 3c. 循环体 — `body_bb` 处理 `i = i + 1`

```cpp
auto exp_val = exp->GenIR(...);             // BinaryExprAST(i + 1)
int addr_id = symtab.GetVarAddr("i");      // 查表得 addr=0
bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
```

`BinaryExprAST(i + 1)`:

```
%7 = load %0        ← 加载 i（从 addr 0）
%8 = add %7, 1      ← 计算 i + 1
```

生成 IR:
```
  %7 = load %0
  %8 = add %7, 1
  store %8, %0
  jump %w7          ← WhileStmt 修复循环：body_bb 未结束，自动加回边
```

#### 修复循环 — 扫描缺 terminator 的块

```cpp
for (size_t i = block_count_before; i < g_current_func->GetBlockCount(); ++i) {
  BasicBlock* blk = g_current_func->GetBlock(i);
  if (!blk->HasTerminator() && !blk->IsEmpty()) {
    blk->AddInst(std::make_unique<JumpInst>(cond_label));  // 加回边
  }
}
```

给 `%w8` 添加回边 `jump %w7`。

---

### 步骤 4：`StmtAST::GenIR` — `return pow`

```cpp
auto exp_val = exp->GenIR(bb, builder, symtab);  // LValAST("pow")
bb->AddInst(std::make_unique<RetInst>(std::move(exp_val)));
```

`LValAST("pow")`:

```
%9 = load %1        ← 加载 pow 的值
```

生成 IR:
```
%w9:
  %9 = load %1
  ret %9
```

---

## 四、最终 IR

```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 0, %0
  %1 = alloc i32
  store 1, %1
  jump %w7
%w7:
  %3 = load %0
  %4 = lt %3, 7
  br %4, %w8, %w9
%w8:
  %5 = load %1
  %6 = mul %5, 2
  store %6, %1
  %7 = load %0
  %8 = add %7, 1
  store %8, %0
  jump %w7
%w9:
  %9 = load %1
  ret %9
}
```

---

## 五、控制流图

```
%entry ──jump──> %w7(条件)
                  │
            ┌─────┴─────┐
          true          false
            │             │
      %w8(循环体)      %w9(返回)
            │             │
      jump ─┘           ret
            ↑
          %w7
```

---

## 六、符号表的变化

| 阶段 | 作用域 | 变量 |
|------|--------|------|
| 进入 `main` | `symtab0` (根) | 空 |
| 声明 `i, pow` 后 | `symtab0` | `i→addr0`, `pow→addr1` |
| 进入 while 循环体 | `symtab1` (parent=`symtab0`) | 继承，无新变量 |
| 查找 `pow` | `symtab1` 无 → 沿链到 `symtab0` | 找到 `pow→addr1` |

循环体没有自己的变量声明，所以 `symtab1` 只是用来在嵌套更深时（如 while 内再嵌套 block）扩展作用域链的。