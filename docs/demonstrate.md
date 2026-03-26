# 编译器流程演示：从源代码到 IR

本文档通过一个具体例子，详细讲解从 SysY 源代码到 AST 再到 Koopa IR 的完整编译流程。

## 示例源代码

```c
int main() {
  const int x = 233 * 4;
  int y = 10;
  y = y + x / 2;
  return y;
}
```

---

## 第一阶段：词法分析（Lexical Analysis）

### 输入：源代码字符流

```
int   main ( ) { 
  const   int   x   =   233   *   4 ;
  int   y   =   1 0 ;
  y   =   y   +   x   /   2 ;
  return   y ;
}
```

### 输出：Token 序列

```
Token 1:  INT        → "int"
Token 2:  IDENT      → "main"
Token 3:  '('
Token 4:  ')'
Token 5:  '{'
Token 6:  CONST      → "const"
Token 7:  INT        → "int"
Token 8:  IDENT      → "x"
Token 9:  '='
Token 10: INT_CONST  → 233
Token 11: '*'
Token 12: INT_CONST  → 4
Token 13: ';'
Token 14: INT        → "int"
Token 15: IDENT      → "y"
Token 16: '='
Token 17: INT_CONST  → 10
Token 18: ';'
Token 19: IDENT      → "y"
Token 20: '='
Token 21: IDENT      → "y"
Token 22: '+'
Token 23: IDENT      → "x"
Token 24: '/'
Token 25: INT_CONST  → 2
Token 26: ';'
Token 27: RETURN     → "return"
Token 28: IDENT      → "y"
Token 29: ';'
Token 30: '}'
```

---

## 第二阶段：语法分析（Syntax Analysis）

### 输入：Token 序列

### 输出：AST（抽象语法树）

语法分析器使用 Bison 根据语法规则自底向上归约，构建 AST：

### 详细归约过程

#### 步骤 1：解析常量定义 `const int x = 233 * 4;`

```
词法单元：
CONST INT IDENT '=' INT_CONST '*' INT_CONST ';'

归约过程：

1.1 归约 Number
    INT_CONST(233) → NumberAST(233)
    
1.2 归约 PrimaryExp
    NumberAST(233) → PrimaryExp → UnaryExp → MulExp → AddExp → RelExp → EqExp → LAndExp → LOrExp → Exp
    
1.3 归约 ConstExp
    Exp → ConstExp
    
1.4 归约 ConstInitVal
    ConstExp → ConstInitVal
    
1.5 归约 Number（第二个数字）
    INT_CONST(4) → NumberAST(4)
    ...（同上，成为 Exp）
    
1.6 归约 MulExp（乘法表达式）
    Exp '*' Exp → BinaryExprAST(op='*', left=233, right=4)
    ↓
    MulExp
    
1.7 继续向上归约
    MulExp → AddExp → RelExp → EqExp → LAndExp → LOrExp → Exp → ConstExp → ConstInitVal
    
1.8 归约 ConstDef
    IDENT '=' ConstInitVal → ConstDefAST(ident="x", init_val=BinaryExprAST(*))
    
1.9 归约 ConstDefList
    ConstDef → ConstDefList(vector<[ConstDefAST]>)
    
1.10 归约 BType
     INT → BTypeAST()
     
1.11 归约 ConstDecl
     CONST BType ConstDefList ';' → ConstDeclAST(const_defs=[ConstDefAST])
```

#### 步骤 2：解析变量定义 `int y = 10;`

```
词法单元：
INT IDENT '=' INT_CONST ';'

归约过程：

2.1 归约 InitVal
    INT_CONST(10) → NumberAST(10) → ... → Exp → InitVal
    
2.2 归约 VarDef
    IDENT '=' InitVal → VarDefAST(ident="y", init_val=NumberAST(10), has_init=true)
    
2.3 归约 VarDefList
    VarDef → VarDefList(vector<[VarDefAST]>)
    
2.4 归约 BType
    INT → BTypeAST()
    
2.5 归约 VarDecl
    BType VarDefList ';' → VarDeclAST(var_defs=[VarDefAST])
```

#### 步骤 3：解析赋值语句 `y = y + x / 2;`

```
词法单元：
IDENT '=' IDENT '+' IDENT '/' INT_CONST ';'

归约过程：

3.1 归约 LVal（左边的 y）
    IDENT("y") → LValAST(ident="y")
    
3.2 归约 Exp（右边的表达式）
    IDENT("y") → LValAST → PrimaryExp → UnaryExp → MulExp → AddExp → RelExp → EqExp → LAndExp → LOrExp → Exp
    
3.3 归约 x / 2（除法）
    IDENT("x") → LValAST → PrimaryExp → UnaryExp → MulExp
    INT_CONST(2) → NumberAST → PrimaryExp → UnaryExp → MulExp
    MulExp '/' MulExp → BinaryExprAST(op='/', left=LValAST("x"), right=NumberAST(2))
    ↓
    MulExp
    
3.4 归约 y + (x / 2)（加法）
    AddExp '+' MulExp → BinaryExprAST(op='+', left=LValAST("y"), right=BinaryExprAST(/))
    ↓
    AddExp → RelExp → EqExp → LAndExp → LOrExp → Exp
    
3.5 归约赋值语句
    LVal '=' Exp ';' → StmtAST(type=ASSIGN, lval=LValAST("y"), exp=BinaryExprAST(+))
```

#### 步骤 4：解析返回语句 `return y;`

```
词法单元：
RETURN IDENT ';'

归约过程：

4.1 归约 Exp
    IDENT("y") → LValAST → PrimaryExp → UnaryExp → MulExp → AddExp → RelExp → EqExp → LAndExp → LOrExp → Exp
    
4.2 归约返回语句
    RETURN Exp ';' → StmtAST(type=RETURN, exp=LValAST("y"))
```

#### 步骤 5：构建 Block

```
归约 BlockItem：
  ConstDecl → BlockItem(vector<[ConstDeclAST]>)
  BlockItem VarDecl → BlockItem(vector<[ConstDeclAST, VarDeclAST]>)
  BlockItem Stmt(赋值) → BlockItem(vector<[ConstDeclAST, VarDeclAST, StmtAST(赋值)]>)
  BlockItem Stmt(return) → BlockItem(vector<[ConstDeclAST, VarDeclAST, StmtAST(赋值), StmtAST(return)]>)

归约 Block：
  '{' BlockItem '}' → BlockAST(items=[...])
```

#### 步骤 6：构建完整的 FuncDef

```
FuncType IDENT '(' ')' Block
  ↓
FuncDefAST(
  func_type=FuncTypeAST(),
  ident="main",
  block=BlockAST(...)
)
```

#### 步骤 7：构建 CompUnit

```
FuncDef → CompUnitAST(func_def=FuncDefAST)
```

### 最终的 AST 结构

```
CompUnitAST
  └── func_def: FuncDefAST
       ├── func_type: FuncTypeAST
       ├── ident: "main"
       └── block: BlockAST
            └── items: [
                 [0] ConstDeclAST
                 │    └── const_defs: [
                 │         [0] ConstDefAST
                 │              ├── ident: "x"
                 │              └── init_val: BinaryExprAST(*)
                 │                   ├── op: '*'
                 │                   ├── left: NumberAST(233)
                 │                   └── right: NumberAST(4)
                 │
                 [1] VarDeclAST
                 │    └── var_defs: [
                 │         [0] VarDefAST
                 │              ├── ident: "y"
                 │              ├── has_init: true
                 │              └── init_val: NumberAST(10)
                 │
                 [2] StmtAST (赋值)
                 │    ├── type: ASSIGN
                 │    ├── lval: LValAST("y")
                 │    └── exp: BinaryExprAST(+)
                 │         ├── op: '+'
                 │         ├── left: LValAST("y")
                 │         └── right: BinaryExprAST(/)
                 │              ├── op: '/'
                 │              ├── left: LValAST("x")
                 │              └── right: NumberAST(2)
                 │
                 [3] StmtAST (返回)
                 │    ├── type: RETURN
                 │    └── exp: LValAST("y")
            ]
```

---

## 第三阶段：IR 生成（IR Generation）

### 输入：AST

### 输出：Koopa IR

IR 生成遍历 AST，为每个节点调用 `GenIR()` 方法，同时维护符号表。

### 详细 IR 生成过程

#### 初始化

```cpp
SymbolTable symtab;  // 空的符号表
Program program;     // 空的 IR 程序
```

#### 步骤 1：生成函数定义

```cpp
// FuncDefAST::GenIR()
auto func = std::make_unique<Function>();
func->name = "main";
func->ret_type = IntTy();

BasicBlock *entry = new BasicBlock();
func->blocks.push_back(unique_ptr<BasicBlock>(entry));

IRBuilder builder(entry);
```

#### 步骤 2：处理 Block 中的第一个项目 - ConstDecl

```cpp
// ConstDeclAST::GenIR()
// 遍历所有 ConstDef

// ConstDefAST::GenIR() for "x = 233 * 4"
auto val = init_val->GenIR(bb, builder, symtab);

// BinaryExprAST::GenIR() for "233 * 4"
auto lhs = left->GenIR(bb, builder, symtab);   // NumberAST(233)
auto rhs = right->GenIR(bb, builder, symtab);  // NumberAST(4)

// NumberAST::GenIR()
return std::make_unique<IntConst>(233);
return std::make_unique<IntConst>(4);

// 常量折叠优化！
if (lhs->IsConst() && rhs->IsConst()) {
  int result = 233 * 4;  // = 932
  return std::make_unique<IntConst>(932);
}

// 回到 ConstDefAST::GenIR()
int const_val = 932;
symtab.const_values["x"] = 932;  // 记录到符号表
// 注意：常量不生成 IR 指令！
```

**当前状态：**
- 符号表：`{ "x": 932 }`
- IR 指令：无（常量被折叠了）

#### 步骤 3：处理第二个项目 - VarDecl

```cpp
// VarDeclAST::GenIR()
// 遍历所有 VarDef

// VarDefAST::GenIR() for "y = 10"
if (has_init) {
  // 生成 alloc 指令分配栈空间
  auto alloc = std::make_unique<AllocInst>(IntTy());
  int addr_id = builder.GetNextValueId();
  bb->AddInst(std::move(alloc));
  
  // 生成初始值的 IR
  auto init_val_ir = init_val->GenIR(bb, builder, symtab);
  // NumberAST(10)::GenIR() → IntConst(10)
  
  // 生成 store 指令
  auto store = std::make_unique<StoreInst>(std::move(init_val_ir), addr_id);
  bb->AddInst(std::move(store));
  
  // 记录变量地址到符号表
  symtab.var_addrs["y"] = addr_id;
}
```

**当前状态：**
- 符号表：`{ "x": 932, "y": 0 }`
- IR 指令：
  ```
  %0 = alloc i32
  store 10, %0
  ```

#### 步骤 4：处理第三个项目 - 赋值语句 `y = y + x / 2`

```cpp
// StmtAST::GenIR() for ASSIGN
auto lval_ptr = static_cast<LValAST*>(lval.get());
auto exp_val = exp->GenIR(bb, builder, symtab);

// BinaryExprAST::GenIR() for "y + x / 2"
auto lhs = left->GenIR(bb, builder, symtab);   // LValAST("y")
auto rhs = right->GenIR(bb, builder, symtab);  // BinaryExprAST(/)

// LValAST::GenIR() for "y"
if (symtab.IsConst("y")) {
  return std::make_unique<IntConst>(symtab.GetConstValue("y"));
} else if (symtab.IsVar("y")) {
  int addr = symtab.GetVarAddr("y");  // = 0
  auto load = std::make_unique<LoadInst>(IntTy(), addr);
  bb->AddInst(std::move(load));
  return load;  // 返回 load 指令的结果
}
// 生成：%1 = load %0

// BinaryExprAST::GenIR() for "x / 2"
auto lhs_x = left->GenIR(bb, builder, symtab);   // LValAST("x")
auto rhs_2 = right->GenIR(bb, builder, symtab);  // NumberAST(2)

// LValAST::GenIR() for "x"
if (symtab.IsConst("x")) {
  int val = symtab.GetConstValue("x");  // = 932
  return std::make_unique<IntConst>(932);  // 常量直接返回值！
}

// NumberAST::GenIR() for 2
return std::make_unique<IntConst>(2);

// 常量折叠！932 / 2 = 466
if (lhs_x->IsConst() && rhs_2->IsConst()) {
  int result = 932 / 2;  // = 466
  return std::make_unique<IntConst>(466);
}

// 回到加法表达式
// lhs = %1 (load y), rhs = IntConst(466)
// 生成 add 指令
auto add = std::make_unique<BinaryInst>(BinaryOp::ADD, std::move(lhs), std::move(rhs), IntTy());
bb->AddInst(std::move(add));
// 生成：%2 = add %1, 466

// 回到赋值语句
// exp_val = %2
int addr = symtab.GetVarAddr("y");  // = 0
auto store = std::make_unique<StoreInst>(std::move(exp_val), addr);
bb->AddInst(std::move(store));
// 生成：store %2, %0
```

**当前状态：**
- 符号表：`{ "x": 932, "y": 0 }`
- IR 指令：
  ```
  %0 = alloc i32
  store 10, %0
  %1 = load %0
  %2 = add %1, 466
  store %2, %0
  ```

#### 步骤 5：处理第四个项目 - 返回语句 `return y`

```cpp
// StmtAST::GenIR() for RETURN
auto exp_val = exp->GenIR(bb, builder, symtab);

// LValAST::GenIR() for "y"
if (symtab.IsConst("y")) {
  // 不是常量
} else if (symtab.IsVar("y")) {
  int addr = symtab.GetVarAddr("y");  // = 0
  auto load = std::make_unique<LoadInst>(IntTy(), addr);
  bb->AddInst(std::move(load));
  return load;
}
// 生成：%3 = load %0

// 生成 ret 指令
auto ret = std::make_unique<RetInst>(std::move(exp_val));
bb->AddInst(std::move(ret));
// 生成：ret %3
```

**最终状态：**
- 符号表：`{ "x": 932, "y": 0 }`
- IR 指令：
  ```
  %0 = alloc i32
  store 10, %0
  %1 = load %0
  %2 = add %1, 466
  store %2, %0
  %3 = load %0
  ret %3
  ```

#### 步骤 6：组装完整的 IR 程序

```cpp
// CompUnitAST::GenIR()
auto program = std::make_unique<Program>();
program->functions.push_back(std::move(func));
```

### 最终的 Koopa IR

```koopa
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 10, %0
  %1 = load %0
  %2 = add %1, 466
  store %2, %0
  %3 = load %0
  ret %3
}
```

---

## 关键优化：常量折叠

注意观察编译器如何优化常量表达式：

### 源代码
```c
const int x = 233 * 4;  // 编译时计算：932
y = y + x / 2;          // x 是常量 932，x / 2 = 466
```

### 优化后的 IR
```koopa
%2 = add %1, 466  // 直接使用 466，而不是生成 load 和 div 指令
```

### 如果没有常量折叠
```koopa
; 会生成更多指令（效率低）
%a = load %x_ptr
%b = div %a, 2
%c = add %1, %b
```

**常量折叠的好处：**
1. 减少运行时计算
2. 减少 IR 指令数量
3. 提高生成的代码质量

---

## 完整流程图

```
源代码
  ↓
[词法分析器 Flex]
  ↓
Token 序列
  ↓
[语法分析器 Bison]
  ↓
AST
  ↓
[IR 生成器 GenIR()]
  ├→ 符号表管理
  ├→ 常量折叠优化
  └→ IR 指令生成
  ↓
Koopa IR
  ↓
[Koopa 库处理]
  ↓
RISC-V 汇编
```

---

## 总结

### AST 生成的关键点
1. **自底向上归约**：从 Token 逐步归约为更大的语法结构
2. **语义动作**：每个产生式都有对应的 C++ 代码创建 AST 节点
3. **类型传递**：通过 `$$` 和 `$1, $2, ...` 传递 AST 指针

### IR 生成的关键点
1. **遍历 AST**：从根节点开始递归调用 `GenIR()`
2. **符号表管理**：跟踪常量和变量的信息
3. **常量折叠**：编译时计算常量表达式
4. **指令生成**：为变量分配、加载、存储生成相应 IR

### 优化的体现
- `233 * 4` → `932`（编译时计算）
- `x / 2`（x=932）→ `466`（编译时计算）
- 最终只生成一次加法指令，而不是乘法和除法

这就是编译器如何将高级语言转换为低级 IR 的完整过程！
