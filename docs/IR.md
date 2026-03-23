┌─────────────────────────────────────────────────┐
│  AST（源代码的"形状"）                           │
│                                                 │
│    return 1 + 2;                                │
│         │                                       │
│       StmtAST                                    │
│         │                                       │
│       AddExprAST                                │
│       /        \                                │
│   NumberAST   NumberAST                         │
│     (1)        (2)                              │
│                                                 │
└─────────────────────────────────────────────────┘
                    │
                    │  IR 生成（遍历 AST）
                    ▼
┌─────────────────────────────────────────────────┐
│  IR（执行流的"指令"）                            │
│                                                 │
│    %0 = add 1, 2                                │
│    ret %0                                       │
│                                                 │
│  特点：                                         │
│    - 线性序列                                   │
│    - SSA 形式                                   │
│    - 无语法噪音                                 │
│                                                 │
└─────────────────────────────────────────────────┘

### AST（树形结构）
```
CompUnitAST
└── FuncDefAST
    ├── FuncTypeAST: int
    ├── ident: "main"
    └── BlockAST
        └── StmtAST
            └── BinaryExprAST
                ├── NumberAST: 1
                └── NumberAST: 2
```
### IR（线性结构）
```
fun @main(): i32 {
%entry:
  %0 = add 1, 2      ; 计算 1 + 2
  ret %0             ; 返回结果
}

```
## IR 的层次结构
```
Program（程序）
├── 全局变量列表
│   └── Value（全局变量）
│
└── 函数列表
    └── Function（函数）
        └── 基本块列表
            └── BasicBlock（基本块）
                └── 指令列表
                    └── Value（指令/值）
```