# RISC-V 汇编生成器原理

## 概述

RISC-V 汇编生成器的作用是将 Koopa IR（中间表示）转换为 RISC-V 汇编代码。整个过程可以理解为：**遍历 IR 数据结构，输出对应的汇编字符串**。

## 核心类：RiscVGenerator

定义在 [riscv.h](../src/riscv.h) 中：

```cpp
class RiscVGenerator {
 public:
  void Generate(const koopa_raw_program_t &program, std::ostream &os);
  
 private:
  std::ostream *os_;  // 输出流，用于写入汇编代码
  
  void Visit(const koopa_raw_slice_t &slice);
  void Visit(const koopa_raw_function_t &func);
  void Visit(const koopa_raw_basic_block_t &bb);
  void Visit(const koopa_raw_value_t &value);
  void Visit(const koopa_raw_return_t &ret);
  void Visit(const koopa_raw_integer_t &integer);
  void Visit(const koopa_raw_binary_t &binary, const koopa_raw_value_t &value);
};
```

## 生成流程

### 1. 入口函数：Generate

```cpp
void RiscVGenerator::Generate(const koopa_raw_program_t &program, std::ostream &os)
```

这是整个生成过程的入口，接收 Koopa IR 程序和输出流。

**工作步骤：**
1. 初始化全局状态（清空栈映射、引用集合等）
2. 调用 `CollectReferences` 收集哪些值会被后续引用
3. 访问程序中的所有函数

### 2. 引用收集：CollectReferences

```cpp
static void CollectReferences(const koopa_raw_program_t &program)
```

**为什么需要这一步？**

考虑表达式 `(1 + 2) * 3`，对应的 IR：
```
%0 = add 1, 2
%1 = mul %0, 3
ret %1
```

`%0` 的结果在计算 `%1` 时需要用到，所以 `%0` 是"被引用的值"，需要保存到栈上。而 `%1` 直接返回，不需要保存。

**收集规则：**
- 如果一个值作为二元运算的操作数 → 需要保存
- 如果一个值作为返回值 → 需要保存
- 整数常量不需要保存（可以直接加载）

### 3. 函数处理：Visit(const koopa_raw_function_t &func)

```cpp
void RiscVGenerator::Visit(const koopa_raw_function_t &func)
```

**输出函数头部：**
```asm
  .text         # 代码段
  .globl main   # 声明 main 为全局符号
main:           # 函数入口标签
```

然后访问函数中的所有基本块。

### 4. 值处理：Visit(const koopa_raw_value_t &value)

```cpp
void RiscVGenerator::Visit(const koopa_raw_value_t &value)
```

这是核心调度函数，根据值的类型调用对应的处理函数：

| 值类型 | 处理函数 | 说明 |
|--------|----------|------|
| KOOPA_RVT_RETURN | Visit(ret) | 处理返回指令 |
| KOOPA_RVT_INTEGER | Visit(integer) | 处理整数常量 |
| KOOPA_RVT_BINARY | Visit(binary, value) | 处理二元运算 |

**优化策略：**

1. **结果缓存**：如果当前值就是上一次计算的结果（`last_result`），直接返回，不重复计算

2. **栈查找**：如果值已经在栈上，直接加载：
   ```asm
   lw a0, 0(sp)  # 从栈加载到 a0
   ```

### 5. 二元运算处理：Visit(const koopa_raw_binary_t &binary, const koopa_raw_value_t &value)

```cpp
void RiscVGenerator::Visit(const koopa_raw_binary_t &binary, const koopa_raw_value_t &value)
```

这是最复杂的部分，处理加减乘除等运算。

**处理逻辑：**

```
┌─────────────────────────────────────────────────────────────┐
│  步骤1: 处理左操作数 (结果放入 t1 或保存到栈)                  │
├─────────────────────────────────────────────────────────────┤
│  如果左操作数是整数常量:                                      │
│    li t1, 常量值                                             │
│  如果左操作数已在栈上:                                        │
│    记录栈位置                                                 │
│  否则:                                                       │
│    递归计算左操作数 → 保存到栈 → 记录栈位置                    │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│  步骤2: 处理右操作数 (结果放入 a0)                            │
├─────────────────────────────────────────────────────────────┤
│  如果右操作数是整数常量:                                      │
│    li a0, 常量值                                             │
│  否则:                                                       │
│    递归计算右操作数 → 结果在 a0                               │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│  步骤3: 执行运算                                             │
├─────────────────────────────────────────────────────────────┤
│  如果左操作数不是整数常量:                                    │
│    lw t1, 栈位置(sp)  # 从栈加载左操作数                      │
│  执行运算指令 (add/sub/mul/...)                              │
│  结果在 a0                                                   │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│  步骤4: 保存结果 (如果需要)                                   │
├─────────────────────────────────────────────────────────────┤
│  如果这个值会被后续引用:                                      │
│    sw a0, 栈位置(sp)  # 保存到栈                              │
└─────────────────────────────────────────────────────────────┘
```

## 运算指令映射

| IR 运算 | RISC-V 指令 | 说明 |
|---------|-------------|------|
| ADD | `add a0, t1, a0` | 加法 |
| SUB | `sub a0, t1, a0` | 减法 |
| MUL | `mul a0, t1, a0` | 乘法 |
| DIV | `div a0, t1, a0` | 除法 |
| MOD | `rem a0, t1, a0` | 取余 |
| EQ | `sub + seqz` | 等于 |
| NOT_EQ | `sub + snez` | 不等于 |
| GT | `sgt a0, t1, a0` | 大于 |
| LT | `slt a0, t1, a0` | 小于 |
| GE | `slt + seqz` | 大于等于 |
| LE | `sgt + seqz` | 小于等于 |
| AND | `and a0, t1, a0` | 按位与 |
| OR | `or a0, t1, a0` | 按位或 |

## 示例分析

### 示例1：简单返回

**源代码：**
```c
int main() { return 1; }
```

**IR：**
```
fun @main(): i32 {
%entry:
  ret 1
}
```

**汇编：**
```asm
  .text
  .globl main
main:
  li a0, 1    # 加载返回值到 a0
  ret         # 返回
```

### 示例2：二元运算

**源代码：**
```c
int main() { return 1 + 2; }
```

**IR：**
```
%0 = add 1, 2
ret %0
```

**汇编：**
```asm
  .text
  .globl main
main:
  li t1, 1      # 左操作数 → t1
  li a0, 2      # 右操作数 → a0
  add a0, t1, a0  # 相加，结果 → a0
  ret
```

### 示例3：嵌套运算

**源代码：**
```c
int main() { return (1 + 2) * 3; }
```

**IR：**
```
%0 = add 1, 2
%1 = mul %0, 3
ret %1
```

**汇编：**
```asm
  .text
  .globl main
main:
  li t1, 1        # 计算 1 + 2
  li a0, 2
  add a0, t1, a0
  sw a0, 0(sp)    # 保存 %0 到栈（因为后续需要）
  li a0, 3        # 右操作数 3
  lw t1, 0(sp)    # 加载 %0 到 t1
  mul a0, t1, a0  # 计算 %0 * 3
  ret
```

## 全局状态说明

```cpp
static std::map<const koopa_raw_value_data*, int> value_stack;  // 值 → 栈位置
static std::set<const koopa_raw_value_data*> referenced_values; // 被引用的值
static int stack_size = 0;               // 当前栈大小（负数增长）
static const koopa_raw_value_data* last_result = nullptr;  // 上一次计算结果
```

- **value_stack**：记录每个值保存在栈的哪个位置
- **referenced_values**：预先收集的需要保存的值
- **stack_size**：栈指针偏移，每次保存减 4
- **last_result**：避免重复计算刚产生的结果

## 总结

RISC-V 汇编生成的核心思想：

1. **遍历 IR 树**：通过访问者模式遍历 Koopa IR 的各个节点
2. **按需存储**：只保存后续会用到的中间结果
3. **寄存器分配**：使用 `t1` 存放左操作数，`a0` 存放右操作数和结果
4. **栈管理**：用栈保存需要复用的值，避免重复计算

这种设计简单高效，适合入门级编译器的代码生成阶段。
