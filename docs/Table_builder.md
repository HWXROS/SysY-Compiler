# SymbolTable 和 IRBuilder 详解

## 目录

1. [SymbolTable（符号表）](#symboltable 符号表)
2. [IRBuilder（IR 构建器）](#irbuilder 构建器)
3. [两者协作流程](#两者协作流程)
4. [实战示例](#实战示例)

---

## SymbolTable（符号表）

### 作用

**符号表是编译器的"字典"**，用于记录和管理程序中的标识符信息。在 SysY 编译器中，它主要负责：

1. **存储常量值**：记录 `const int x = 10;` 中 `x` 的值
2. **存储变量地址**：记录 `int y = 20;` 中 `y` 的内存地址（alloc ID）
3. **作用域管理**：支持嵌套作用域和变量遮蔽
4. **名称查找**：快速查找标识符的类型、值、地址等信息

### 数据结构设计

```cpp
class SymbolTable {
 public:
  std::map<std::string, int> const_values;  // 常量值表：名字 → 值
  std::map<std::string, int> var_addrs;     // 变量地址表：名字 → alloc ID
  SymbolTable *parent;  // 父作用域指针（关键！）
  
  SymbolTable(SymbolTable *p = nullptr) : parent(p) {}
  
  // 查找方法（支持作用域链）
  bool Exists(const std::string &name) const;
  bool IsConst(const std::string &name) const;
  bool IsVar(const std::string &name) const;
  int GetConstValue(const std::string &name) const;
  int GetVarAddr(const std::string &name) const;
};
```

### 作用域链机制

**核心思想**：每个作用域是一个 `SymbolTable` 对象，通过 `parent` 指针连接成链。

```
作用域链示例：

全局作用域 (parent = nullptr)
    ↓
函数 main 作用域 (parent = 全局)
    ↓
Block1 作用域 (parent = 函数)
    ↓
Block2 作用域 (parent = Block1)
```

### 查找算法（递归向上）

```cpp
bool SymbolTable::Exists(const std::string &name) const {
  // 1. 先查当前作用域
  if (const_values.count(name) || var_addrs.count(name)) {
    return true;  // 找到了
  }
  // 2. 没找到，查父作用域（递归）
  if (parent) {
    return parent->Exists(name);
  }
  // 3. 到顶层还没找到
  return false;
}

int SymbolTable::GetVarAddr(const std::string &name) const {
  // 1. 先查当前作用域
  auto it = var_addrs.find(name);
  if (it != var_addrs.end()) {
    return it->second;  // 找到了，返回地址
  }
  // 2. 没找到，查父作用域（递归）
  if (parent) {
    return parent->GetVarAddr(name);
  }
  // 3. 到顶层还没找到
  return 0;
}
```

**查找过程示例**：
```
查找变量 x：
Block2 → Block1 → 函数作用域 → 全局作用域
  ↓        ↓         ↓           ↓
没找到    没找到    找到了！     (不会到这里)
         返回地址
```

### 变量遮蔽原理

**遮蔽（Shadowing）**：内层作用域定义与外层同名的变量时，外层变量被"遮蔽"。

```c
int main() {
  int a = 1;      // 外层 a
  {
    int a = 2;    // 内层 a，遮蔽外层
    return a;     // 返回 2（内层 a）
  }
}
```

**实现机制**：
```cpp
// VarDefAST::GenIR 实现
std::unique_ptr<KoopaValue> VarDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  int addr_id = builder.NewId();           // 1. 申请新地址
  bb->AddInst(std::make_unique<AllocInst>(addr_id));
  
  symtab.var_addrs[ident] = addr_id;       // 2. 在当前作用域登记
                                           //    如果外层有同名变量，会被"遮蔽"
                                           //    但外层变量仍然存在父作用域中
  
  if (has_init) {
    auto val = init_val->GenIR(bb, builder, symtab);
    bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id));
  }
  return nullptr;
}
```

**遮蔽的本质**：
- 查找时**从内到外**，先找到谁就用谁
- 内层变量在 `symtab.var_addrs` 中优先被找到
- 外层变量仍然存在，只是访问不到（被遮蔽）

### 作用域创建时机

**每个 Block 都会创建新的作用域**：

```cpp
// BlockAST::GenIR 实现
std::unique_ptr<KoopaValue> BlockAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  // 创建新的作用域，parent 指向当前作用域
  SymbolTable new_symtab(&symtab);
  
  // 在新作用域中处理 Block 内的所有语句
  for (const auto &item : items) {
    item->GenIR(bb, builder, new_symtab);
  }
  return nullptr;
}
```

**作用域生命周期**：
```
进入 Block → 创建 new_symtab → 处理 BlockItem → 离开 Block → new_symtab 销毁
    ↓              ↓               ↓              ↓
  父作用域      子作用域开始    子作用域中     子作用域结束
```

### 关键方法详解

#### 1. `ExistsLocal()` - 检查当前作用域

```cpp
bool ExistsLocal(const std::string &name) const {
  return const_values.count(name) || var_addrs.count(name);
}
```

**用途**：检测**重复定义**（只查当前作用域，不查父作用域）

```c
int main() {
  int a = 1;
  int a = 2;  // 错误：同一作用域重复定义
}
```

#### 2. `IsConst()` / `IsVar()` - 类型判断

```cpp
bool IsConst(const std::string &name) const {
  if (const_values.count(name)) {
    return true;  // 当前作用域找到了
  }
  if (parent) {
    return parent->IsConst(name);  // 查父作用域
  }
  return false;
}
```

**用途**：
- 判断 LVal 是常量还是变量
- 赋值语句左边必须是变量（不能给常量赋值）

#### 3. `GetConstValue()` / `GetVarAddr()` - 获取值/地址

```cpp
int GetConstValue(const std::string &name) const {
  auto it = const_values.find(name);
  if (it != const_values.end()) {
    return it->second;  // 当前作用域找到了
  }
  if (parent) {
    return parent->GetConstValue(name);  // 查父作用域
  }
  return 0;  // 没找到，返回默认值
}
```

**用途**：
- `GetConstValue()`：常量折叠优化（直接用值代替 Load）
- `GetVarAddr()`：生成 Load/Store 指令（需要地址）

---

## IRBuilder（IR 构建器）

### 作用

**IRBuilder 是 IR 指令的"工厂"**，负责：

1. **生成唯一 ID**：为每个中间变量分配唯一的编号
2. **创建指令**：生成 alloc、load、store、add 等指令
3. **管理临时变量**：跟踪已使用的 ID，避免冲突

### 数据结构设计

```cpp
class IRBuilder {
 private:
  int next_id;  // 下一个可用 ID
  
 public:
  IRBuilder() : next_id(0) {}
  
  // 生成新 ID（核心方法！）
  int NewId() {
    return next_id++;
  }
};
```

### NewId() 方法详解

```cpp
int NewId() {
  return next_id++;
}
```

**为什么需要唯一 ID？**

Koopa IR 是**静态单赋值（SSA）**形式，每个变量只能被赋值一次。因此需要为每个中间结果分配唯一编号：

```
%0 = alloc i32      ; ID 0
%1 = load %0        ; ID 1
%2 = add %1, 2      ; ID 2
%3 = mul %2, %1     ; ID 3
```

**ID 分配过程**：
```
初始：next_id = 0

调用 NewId() → 返回 0, next_id = 1  → 用于 %0
调用 NewId() → 返回 1, next_id = 2  → 用于 %1
调用 NewId() → 返回 2, next_id = 3  → 用于 %2
...
```

### IRBuilder 的使用场景

#### 1. 变量分配（Alloc）

```cpp
// VarDefAST::GenIR
std::unique_ptr<KoopaValue> VarDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  int addr_id = builder.NewId();  // 申请唯一 ID
  bb->AddInst(std::make_unique<AllocInst>(addr_id));  // 生成 alloc 指令
  
  symtab.var_addrs[ident] = addr_id;  // 登记到符号表
  ...
}
```

**生成的 IR**：
```
%0 = alloc i32  ; addr_id = 0
```

#### 2. 加载变量（Load）

```cpp
// LValAST::GenIR
std::unique_ptr<KoopaValue> LValAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (symtab.IsVar(ident)) {
    int addr_id = symtab.GetVarAddr(ident);  // 从符号表获取地址
    int id = builder.NewId();                 // 申请新 ID 用于 load 结果
    bb->AddInst(std::make_unique<LoadInst>(id, addr_id));  // 生成 load 指令
    return std::make_unique<ValueRef>(id);
  }
  ...
}
```

**生成的 IR**：
```
%1 = load %0  ; id = 1, addr_id = 0
```

#### 3. 二元运算（BinaryOp）

```cpp
// BinaryExprAST::GenIR
std::unique_ptr<KoopaValue> BinaryExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  auto left_val = left->GenIR(bb, builder, symtab);
  auto right_val = right->GenIR(bb, builder, symtab);
  
  int id = builder.NewId();  // 申请新 ID 用于运算结果
  bb->AddInst(std::make_unique<BinaryOpInst>(id, op, std::move(left_val), std::move(right_val)));
  return std::make_unique<ValueRef>(id);
}
```

**生成的 IR**：
```
%2 = add %0, %1  ; id = 2
```

---

## 两者协作流程

### 完整示例分析

```c
int main() {
  int a = 1;
  int b = a + 2;
  return b;
}
```

### 编译过程详解

#### 步骤 1：处理 `int a = 1;`

```cpp
// VarDefAST::GenIR for 'a'
int addr_id_a = builder.NewId();  // addr_id_a = 0
bb->AddInst(std::make_unique<AllocInst>(0));
symtab.var_addrs["a"] = 0;

// 处理初始值
auto init_val = std::make_unique<IntConst>(1);
bb->AddInst(std::make_unique<StoreInst>(std::move(init_val), 0));
```

**生成的 IR**：
```
%0 = alloc i32
store 1, %0
```

**符号表状态**：
```
var_addrs: { "a" → 0 }
```

#### 步骤 2：处理 `a + 2`

```cpp
// BinaryExprAST::GenIR
// 1. 处理左操作数 'a'
auto left_val = LValAST("a").GenIR(bb, builder, symtab);
  // LValAST::GenIR:
  //   addr_id = symtab.GetVarAddr("a") = 0
  //   id = builder.NewId() = 1
  //   bb->AddInst(std::make_unique<LoadInst>(1, 0))
  //   返回 ValueRef(1)

// 2. 处理右操作数 '2'
auto right_val = std::make_unique<IntConst>(2);

// 3. 生成加法指令
int result_id = builder.NewId();  // result_id = 2
bb->AddInst(std::make_unique<BinaryOpInst>(2, '+', std::move(left_val), std::move(right_val)));
```

**生成的 IR**：
```
%1 = load %0
%2 = add %1, 2
```

**符号表状态**：（不变）
```
var_addrs: { "a" → 0 }
```

#### 步骤 3：处理 `int b = ...`

```cpp
// VarDefAST::GenIR for 'b'
int addr_id_b = builder.NewId();  // addr_id_b = 3
bb->AddInst(std::make_unique<AllocInst>(3));
symtab.var_addrs["b"] = 3;

// 处理初始值（使用步骤 2 的结果）
auto init_val = std::make_unique<ValueRef>(2);
bb->AddInst(std::make_unique<StoreInst>(std::move(init_val), 3));
```

**生成的 IR**：
```
%3 = alloc i32
store %2, %3
```

**符号表状态**：
```
var_addrs: { "a" → 0, "b" → 3 }
```

#### 步骤 4：处理 `return b;`

```cpp
// StmtAST::GenIR for return
auto lval_ptr = static_cast<LValAST*>(lval.get());
auto exp_val = LValAST("b").GenIR(bb, builder, symtab);
  // LValAST::GenIR:
  //   addr_id = symtab.GetVarAddr("b") = 3
  //   id = builder.NewId() = 4
  //   bb->AddInst(std::make_unique<LoadInst>(4, 3))
  //   返回 ValueRef(4)

bb->AddInst(std::make_unique<RetInst>(std::move(exp_val)));
```

**生成的 IR**：
```
%4 = load %3
ret %4
```

### 最终 IR 汇总

```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 1, %0
  %1 = alloc i32
  %2 = load %0
  %3 = add %2, 2
  store %3, %1
  %4 = load %1
  ret %4
}
```

### ID 分配统计

| 指令 | ID | 用途 |
|------|-----|------|
| `alloc i32` | 0 | 变量 a 的地址 |
| `load %0` | 1 | 读取 a 的值 |
| `add %1, 2` | 2 | a + 2 的结果 |
| `alloc i32` | 3 | 变量 b 的地址 |
| `load %3` | 4 | 读取 b 的值 |

---

## 实战示例

### 示例 1：嵌套作用域

```c
int main() {
  int x = 1;
  {
    int y = x + 1;
    {
      int z = x + y + 1;
      return z;
    }
  }
}
```

**作用域变化**：
```
进入 main:
  symtab_0 (parent = nullptr)
  var_addrs: { "x" → 0 }

进入 Block1:
  symtab_1 (parent = &symtab_0)
  var_addrs: { "y" → 2 }

进入 Block2:
  symtab_2 (parent = &symtab_1)
  var_addrs: { "z" → 5 }

离开 Block2:
  symtab_2 销毁

离开 Block1:
  symtab_1 销毁
```

**生成的 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 1, %0
  %1 = alloc i32
  %2 = load %0
  %3 = add %2, 1
  store %3, %1
  %4 = alloc i32
  %5 = load %0
  %6 = load %1
  %7 = add %5, %6
  %8 = add %7, 1
  store %8, %4
  %9 = load %4
  ret %9
}
```

### 示例 2：变量遮蔽

```c
int main() {
  int a = 1;
  {
    int a = 2;
    return a;  // 返回 2（内层 a）
  }
}
```

**符号表变化**：
```
外层：
  symtab_0
  var_addrs: { "a" → 0 }

内层：
  symtab_1 (parent = &symtab_0)
  var_addrs: { "a" → 2 }  // 遮蔽外层 a

查找 "a"：
  symtab_1.var_addrs["a"] = 2  ✓ 找到！
  （不会查 symtab_0）
```

**生成的 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 1, %0
  %1 = alloc i32
  store 2, %1
  %2 = load %1  ; 加载内层 a（地址 1）
  ret %2
}
```

### 示例 3：常量折叠

```c
int main() {
  const int x = 2 * 3;
  int y = x + 1;
  return y;
}
```

**符号表状态**：
```
const_values: { "x" → 6 }  // 编译时计算 2*3
var_addrs:    { "y" → 0 }
```

**关键点**：
- 常量 `x` 不生成 alloc 指令
- 使用 `x` 时直接代入值 6（常量折叠）

**生成的 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  %1 = add 6, 1    ; x 直接用值 6
  store %1, %0
  %2 = load %0
  ret %2
}
```

---

## 总结

### SymbolTable 的核心作用

1. **存储管理**：记录常量和变量的信息
2. **作用域链**：通过 parent 指针实现嵌套作用域
3. **名称查找**：从内到外递归查找标识符
4. **变量遮蔽**：内层优先，外层被遮蔽但保留

### IRBuilder 的核心作用

1. **ID 分配**：为每个中间变量分配唯一编号
2. **指令创建**：生成各种 Koopa IR 指令
3. **冲突避免**：确保所有 ID 不重复

### 协作关系

```
源代码 → AST → GenIR()
              ↓
         SymbolTable（查名字）
              ↓
         IRBuilder（要 ID）
              ↓
         BasicBlock（存指令）
              ↓
         Koopa IR（输出）
```

**SymbolTable** 是"字典"，回答"**这个名字对应什么？**"  
**IRBuilder** 是"工厂"，回答"**下一个 ID 是多少？**"

两者配合，才能正确生成 Koopa IR！
