# 编译器结构概述

## 完整编译流程

```
┌─────────────────────────────────────────────────────────────┐
│  源语言 (SysY)                                              │
│                                                             │
│  int main() {                                               │
│    return 0;                                                │
│  }                                                          │
└─────────────────────────────────────────────────────────────┘
                         │
                         │  词法分析 (Flex) + 语法分析 (Bison)
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  AST（抽象语法树）                                           │
│                                                             │
│  CompUnitAST                                                │
│  └── FuncDefAST                                             │
│      ├── FuncTypeAST: int                                   │
│      ├── ident: "main"                                      │
│      └── BlockAST                                           │
│          └── StmtAST                                        │
│              └── NumberAST: 0                               │
└─────────────────────────────────────────────────────────────┘
                         │
                         │  GenIR()
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  IR 结构 (自己定义的中间表示)                                 │
│                                                             │
│  Program → Function → BasicBlock → Instruction              │
│                                                             │
│  ├── Dump()      → 输出 Koopa IR 字符串                      │
│  └── ToString()  → 返回 Koopa IR 字符串                      │
└─────────────────────────────────────────────────────────────┘
                         │
                         │  ToString()
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  Koopa IR 字符串                                            │
│                                                             │
│  fun @main(): i32 {                                         │
│  %entry:                                                    │
│    ret 0                                                    │
│  }                                                          │
└─────────────────────────────────────────────────────────────┘
                         │
                         │  koopa_parse_from_string()
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  koopa_raw_program_t (Koopa 库的数据结构)                    │
│                                                             │
│  ├── funcs[]                                                │
│  │   └── koopa_raw_function_t                               │
│  │       └── bbs[]                                          │
│  │           └── koopa_raw_basic_block_t                    │
│  │               └── insts[]                                │
│  │                   └── koopa_raw_value_t                  │
└─────────────────────────────────────────────────────────────┘
                         │
                         │  RiscVGenerator::Generate()
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  RISC-V 汇编                                                │
│                                                             │
│    .text                                                    │
│    .globl main                                              │
│  main:                                                      │
│    li a0, 0                                                 │
│    ret                                                      │
└─────────────────────────────────────────────────────────────┘
```

## 两种使用 Koopa 的方式

### 方式一：字符串形式（当前实现）

```
AST ──► GenIR() ──► 自己的 IR 结构 ──► ToString() ──► IR 字符串 ──► koopa parser ──► koopa_raw_program_t
                                              │
                                              ▼
                                       "fun @main(): i32 {
                                        %entry:
                                          ret 0
                                        }"
```

**优点**：简单直观，易于调试
**缺点**：需要解析字符串，效率略低

### 方式二：直接 API（更高效）

```
AST ──► GenIR() ──► koopa API 调用 ──► koopa_raw_program_t
                          │
                          ▼
                   koopa_new_function("main")
                   koopa_new_basic_block("entry")
                   koopa_new_return_inst(0)
```

**优点**：效率高，无需解析字符串
**缺点**：API 复杂，学习成本高

## 当前实现的文件结构

```
src/
├── ast.h / ast.cpp    # AST 节点定义和 IR 生成
├── ir.h               # 自己定义的 IR 结构
├── sysy.l             # 词法分析器 (Flex)
├── sysy.y             # 语法分析器 (Bison)
├── riscv.h / riscv.cpp # RISC-V 汇编生成器
└── main.cpp           # 主函数，串联各阶段
```

## 关键类说明

### AST 层 (ast.h)

| 类名 | 作用 |
|------|------|
| CompUnitAST | 编译单元，包含多个函数定义 |
| FuncDefAST | 函数定义 |
| BlockAST | 代码块 |
| StmtAST | 语句 |
| NumberAST | 数字字面量 |

### IR 层 (ir.h)

| 类名 | 作用 |
|------|------|
| Program | IR 程序 |
| Function | 函数 |
| BasicBlock | 基本块 |
| BinaryOpInst | 二元运算指令 |
| UnaryOpInst | 一元运算指令 |
| IntConst | 整数常量 |
| ValueRef | 值引用 (%0, %1, ...) |

### RISC-V 层 (riscv.h)

| 方法 | 作用 |
|------|------|
| Generate() | 入口函数，生成汇编 |
| Visit(func) | 处理函数 |
| Visit(bb) | 处理基本块 |
| Visit(value) | 处理值/指令 |
| Visit(binary) | 处理二元运算 |

## 示例：从源码到汇编

**源代码：**
```c
int main() { return 1 + 2; }
```

**AST：**
```
CompUnitAST
└── FuncDefAST
    └── BlockAST
        └── StmtAST
            └── BinaryExprAST (+)
                ├── NumberAST (1)
                └── NumberAST (2)
```

**IR 结构：**
```
Program
└── Function("main")
    └── BasicBlock("entry")
        ├── BinaryOpInst(%0 = add 1, 2)
        └── RetInst(%0)
```

**Koopa IR 字符串：**
```
fun @main(): i32 {
%entry:
  %0 = add 1, 2
  ret %0
}
```

**RISC-V 汇编：**
```asm
  .text
  .globl main
main:
  li t1, 1
  li a0, 2
  add a0, t1, a0
  ret
```

## 数据流向图

```
┌──────────┐    Flex/Bison    ┌──────────┐    GenIR()    ┌──────────┐
│  源代码   │ ──────────────► │    AST   │ ────────────► │  IR 结构  │
│  test.c  │                  │  ast.h   │               │   ir.h   │
└──────────┘                  └──────────┘               └──────────┘
                                                             │
                                                             │ ToString()
                                                             ▼
┌──────────┐    RiscVGenerator  ┌──────────┐    parser    ┌──────────┐
│ RISC-V   │ ◄──────────────── │ koopa    │ ◄─────────── │ IR 字符串 │
│ 汇编     │                    │ raw_prog │              │          │
│ test.s   │                    │          │              │          │
└──────────┘                    └──────────┘              └──────────┘
```
