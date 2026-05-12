# riscv.cpp — RISC-V 汇编生成实现

## 文件作用

**riscv.cpp** 实现了**从 Koopa IR 到 RISC-V 汇编**的转换。这是编译器后端的第一步，将平台无关的中间表示转换为特定架构（ RISC-V ）的指令。RISC-V 是目前流行的开源指令集架构，广泛应用于教育和嵌入式场景。

---

## 核心思想

RISC-V 是**三地址寄存器架构**（大多数指令有两个源寄存器，一个目标寄存器），与 Koopa IR 的临时值概念类似。转换的关键挑战在于：

1. **寄存器分配**：Koopa IR 的临时值是无限的，而 RISC-V 寄存器只有 32 个（`a0-a7`、`t0-t6`、`s0-s11` 等）
2. **内存布局**：局部变量存放在栈上（通过 `alloc` 分配），需要管理栈帧
3. **指令选择**：每种 Koopa IR 指令对应哪种 RISC-V 指令

---

## 重要概念

### 栈布局策略

本编译器采用**递减栈**（sp 向下增长），所有局部变量和临时值都存在栈上：

```
栈内存布局（简化）：
  higher address
    ┌─────────────┐
    │ 变量 addr=0  │  ← sp + 0（或 sp - 0)
    ├─────────────┤
    │ 变量 addr=-4 │  ← sp - 4
    ├─────────────┤
    │ 临时值       │  ← sp - 8
    │   ...        │
    └─────────────┘  ← sp 指向当前栈顶
  lower address
```

- `stack_size` 从 0 开始，每分配一个值减 4
- 变量的 `value_stack[value]` 记录它在栈上的偏移量

### referenced_values — 引用值集合

在 `CollectReferences()` 中预先扫描整个程序，记录那些**被多次引用**的值。这些值需要在计算后立即存回栈，以便后续复用：

```cpp
if (referenced_values.find(value) != referenced_values.end()) {
  // 计算后将 a0 的值存回栈
  stack_size -= 4;
  *os_ << "  sw a0, " << result_stack << "(sp)\n";
  value_stack[value] = result_stack;
}
```

### last_result — 避免重复加载

如果上一次计算的结果还在 `a0` 中（且仍然是当前值引用的），就直接使用，避免生成冗余的 `lw a0, offset(sp)`：

```cpp
if (value == last_result) {
  return;  // 已经在 a0 中，不需要重新加载
}
```

---

## 重要函数

### CollectReferences — 预扫描收集引用

```cpp
static void CollectReferences(const koopa_raw_program_t &program)
```

遍历整个 Koopa IR 程序，识别所有**被多次引用**的值，存入 `referenced_values`：

| 指令类型 | 规则 |
|----------|------|
| 二元运算 `binary` | lhs 和 rhs（非立即数）被引用 |
| 返回 `return` | 返回值（非立即数）被引用 |
| 存储 `store` | 要存储的值（非立即数）被引用 |
| 加载 `load` | load 指令本身被引用 |
| 分支 `branch` | 条件值（非立即数）被引用 |

---

### RiscVGenerator::Generate — 入口函数

```cpp
void RiscVGenerator::Generate(const koopa_raw_program_t &program, std::ostream &os) {
  os_ = &os;
  value_stack.clear();
  referenced_values.clear();
  stack_size = 0;
  last_result = nullptr;

  CollectReferences(program);          // 预扫描
  Visit(program.values);               // 访问全局值
  Visit(program.funcs);                // 访问函数
}
```

---

### Visit(const koopa_raw_slice_t &slice) — 分发访问

```cpp
void RiscVGenerator::Visit(const koopa_raw_slice_t &slice) {
  for (size_t i = 0; i < slice.len; ++i) {
    auto ptr = slice.buffer[i];
    switch (slice.kind) {
      case KOOPA_RSIK_FUNCTION:
        Visit(reinterpret_cast<koopa_raw_function_t>(ptr)); break;
      case KOOPA_RSIK_BASIC_BLOCK:
        Visit(reinterpret_cast<koopa_raw_basic_block_t>(ptr)); break;
      case KOOPA_RSIK_VALUE:
        Visit(reinterpret_cast<koopa_raw_value_t>(ptr)); break;
    }
  }
}
```

根据 slice 中元素的类型（函数、基本块、值），分发到对应的 Visit 重载。

---

### Visit(const koopa_raw_function_t &func) — 函数生成

```cpp
void RiscVGenerator::Visit(const koopa_raw_function_t &func) {
  if (func->bbs.len == 0) return;  // 跳过空函数（如库函数声明）

  value_stack.clear();
  stack_size = 0;
  last_result = nullptr;

  *os_ << "  .text\n";
  *os_ << "  .globl " << func->name + 1 << "\n";
  *os_ << func->name + 1 << ":\n";

  Visit(func->bbs);  // 访问所有基本块
}
```

每个函数生成一段 `.text` 段代码，从函数名标签开始。

---

### Visit(const koopa_raw_basic_block_t &bb) — 基本块生成

```cpp
void RiscVGenerator::Visit(const koopa_raw_basic_block_t &bb) {
  *os_ << bb->name + 1 << ":\n";   // 块标签
  Visit(bb->insts);                  // 块内所有指令
}
```

基本块标签直接作为 RISC-V 标号使用（如 `w7:`）。

---

### Visit(const koopa_raw_value_t &value) — 值的生成

这是最核心的分发函数，根据值的类型调用不同的 Visit 重载：

```cpp
void RiscVGenerator::Visit(const koopa_raw_value_t &value) {
  if (value == last_result) return;  // 已在 a0 中，跳过

  auto it = value_stack.find(value);
  if (it != value_stack.end()) {
    *os_ << "  lw a0, " << it->second << "(sp)\n";  // 从栈加载
    last_result = nullptr;
    return;
  }

  switch (kind.tag) {
    case KOOPA_RVT_RETURN:   Visit(kind.data.ret); break;
    case KOOPA_RVT_INTEGER:   Visit(kind.data.integer); break;
    case KOOPA_RVT_BINARY:    Visit(kind.data.binary, value); break;
    case KOOPA_RVT_ALLOC:     VisitAlloc(value); break;
    case KOOPA_RVT_STORE:     Visit(kind.data.store); break;
    case KOOPA_RVT_LOAD:      Visit(kind.data.load, value); break;
    case KOOPA_RVT_BRANCH:    Visit(kind.data.branch); break;
    case KOOPA_RVT_JUMP:      Visit(kind.data.jump); break;
  }
}
```

---

### Visit(const koopa_raw_binary_t &binary, value) — 二元运算

这是最复杂的 Visit 函数，处理所有二元运算指令到 RISC-V 指令的转换。

**核心策略：**
- 左操作数放到 `t1` 寄存器
- 右操作数放到 `a0` 寄存器（如果需要计算则直接在 a0）
- 结果放在 `a0`
- 如果左操作数是立即数，用 `li t1, imm`
- 如果右操作数是立即数，用 `li a0, imm`
- 如果左操作数是之前计算的结果（不在栈上），需要先存栈再加载到 t1

**二元运算映射：**

| Koopa IR | RISC-V | 说明 |
|----------|--------|------|
| `add` | `add a0, t1, a0` | 加法 |
| `sub` | `sub a0, t1, a0` | 减法 |
| `mul` | `mul a0, t1, a0` | 乘法 |
| `div` | `div a0, t1, a0` | 除法 |
| `mod` | `rem a0, t1, a0` | 取模 |
| `lt` | `slt a0, t1, a0` | 小于（signed） |
| `gt` | `sgt a0, t1, a0` | 大于 |
| `le` | `sgt a0, t1, a0` + `seqz a0, a0` | 小于等于 |
| `ge` | `slt a0, t1, a0` + `seqz a0, a0` | 大于等于 |
| `eq` | `sub a0, t1, a0` + `seqz a0, a0` | 等于 |
| `ne` | `sub a0, t1, a0` + `snez a0, a0` | 不等于 |
| `and` | `and a0, t1, a0` | 逻辑与 |
| `or` | `or a0, t1, a0` | 逻辑或 |

---

### VisitAlloc — alloc 指令

```cpp
void RiscVGenerator::VisitAlloc(const koopa_raw_value_t &value) {
  int addr = stack_size;      // 当前栈位置作为地址
  stack_size -= 4;            // 栈向下增长
  value_stack[value] = addr;  // 记录 value → 栈偏移
  last_result = nullptr;
}
```

`alloc i32` 在栈上分配 4 字节，记录分配地址。

---

### Visit(const koopa_raw_load_t &load, value) — load 指令

```cpp
void RiscVGenerator::Visit(const koopa_raw_load_t &load, const koopa_raw_value_t &value) {
  auto addr_value = load.src;
  auto it = value_stack.find(addr_value);
  if (it != value_stack.end()) {
    *os_ << "  lw a0, " << it->second << "(sp)\n";  // 从栈加载
  }
  last_result = value;

  if (referenced_values.find(value) != referenced_values.end()) {
    // 如果这个值被多次引用，需要存回栈
    int result_stack = stack_size;
    stack_size -= 4;
    *os_ << "  sw a0, " << result_stack << "(sp)\n";
    value_stack[value] = result_stack;
  }
}
```

---

### Visit(const koopa_raw_store_t &store) — store 指令

```cpp
void RiscVGenerator::Visit(const koopa_raw_store_t &store) {
  Visit(store.value);  // 先计算要存储的值（放到 a0）
  auto addr_value = store.dest;
  auto it = value_stack.find(addr_value);
  if (it != value_stack.end()) {
    *os_ << "  sw a0, " << it->second << "(sp)\n";  // a0 存到目标栈地址
  }
  last_result = nullptr;
}
```

---

### Visit(const koopa_raw_branch_t &branch) — 条件分支

```cpp
void RiscVGenerator::Visit(const koopa_raw_branch_t &branch) {
  Visit(branch.cond);  // 计算条件，结果在 a0
  *os_ << "  bnez a0, " << branch.true_bb->name + 1 << "\n";  // a0≠0 跳
  *os_ << "  j " << branch.false_bb->name + 1 << "\n";        // 否则跳到 false
  last_result = nullptr;
}
```

---

### Visit(const koopa_raw_jump_t &jump) — 无条件跳转

```cpp
void RiscVGenerator::Visit(const koopa_raw_jump_t &jump) {
  *os_ << "  j " << jump.target->name + 1 << "\n";
  last_result = nullptr;
}
```

---

## RISC-V 常用寄存器

| 寄存器 | 用途 |
|--------|------|
| `a0` | 函数返回值，也用于计算结果 |
| `t0-t6` | 临时寄存器 |
| `s0-s11` | 保存寄存器 |
| `sp` | 栈指针 |
| `ra` | 返回地址 |

本编译器主要使用 `a0`（结果）、`t1`（左操作数）、`sp`（栈指针）。
