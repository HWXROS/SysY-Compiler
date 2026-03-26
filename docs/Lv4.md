# Lv4 常量与变量

本章实现了常量/变量定义和赋值语句的编译功能。

## 语法规范

### 新增语法规则

```
Decl          ::= ConstDecl | VarDecl;
ConstDecl     ::= "const" BType ConstDef {"," ConstDef} ";";
BType         ::= "int";
ConstDef      ::= IDENT "=" ConstInitVal;
ConstInitVal  ::= ConstExp;
VarDecl       ::= BType VarDef {"," VarDef} ";";
VarDef        ::= IDENT | IDENT "=" InitVal;
InitVal       ::= Exp;
Block         ::= "{" {BlockItem} "}";
BlockItem     ::= Decl | Stmt;
Stmt          ::= LVal "=" Exp ";"
                | "return" Exp ";";
LVal          ::= IDENT;
ConstExp      ::= Exp;
```

## 实现细节

### 1. 词法分析器修改 (sysy.l)

添加 `const` 关键字识别：

```
"const"         { return CONST; }
```

### 2. 语法分析器修改 (sysy.y)

#### 新增 token
```
%token CONST
```

#### 新增非终结符类型
```
%type <ast_val> Decl ConstDecl VarDecl ConstDef VarDef ConstInitVal InitVal LVal ConstExp BType
%type <ast_list> BlockItem ConstDefList VarDefList
```

#### Block 规则修改
原来的 Block 只包含单个 Stmt，现在需要支持多个 BlockItem：

```
Block
  : '{' '}' {
    auto ast = new BlockAST();
    $$ = ast;
  }
  | '{' BlockItem '}' {
    auto ast = new BlockAST();
    for (auto item : *$2) {
      ast->items.push_back(unique_ptr<BaseAST>(item));
    }
    delete $2;
    $$ = ast;
  }
  ;
```

### 3. AST 节点 (ast.h)

#### 符号表类
```cpp
class SymbolTable {
 public:
  std::map<std::string, int> const_values;  // 常量值表
  std::map<std::string, int> var_addrs;     // 变量地址表
  
  bool IsConst(const std::string &name) const;
  bool IsVar(const std::string &name) const;
  int GetConstValue(const std::string &name) const;
  int GetVarAddr(const std::string &name) const;
};
```

#### 新增 AST 节点类
- `ConstDeclAST` - 常量声明，包含多个 `ConstDefAST`
- `ConstDefAST` - 常量定义，包含标识符和初始值
- `VarDeclAST` - 变量声明，包含多个 `VarDefAST`
- `VarDefAST` - 变量定义，包含标识符和可选初始值
- `LValAST` - 左值引用，用于读取变量/常量值
- `BTypeAST` - 基本类型（目前只有 int）

#### StmtAST 修改
```cpp
class StmtAST : public BaseAST {
 public:
  StmtType type;                    // RETURN 或 ASSIGN
  std::unique_ptr<BaseAST> exp;     // 表达式
  std::unique_ptr<BaseAST> lval;    // 左值（仅用于赋值语句）
};
```

### 4. IR 生成 (ir.h, ast.cpp)

#### 新增 IR 指令
- `AllocInst` - 内存分配指令：`%x = alloc i32`
- `StoreInst` - 存储指令：`store value, %addr`
- `LoadInst` - 加载指令：`%x = load %addr`

#### 常量折叠优化
在编译时计算常量表达式的值：

```cpp
// ConstDefAST::GenIR
auto val = init_val->GenIR(bb, builder, symtab);
int const_val = 0;
if (val->IsConst()) {
  const_val = val->GetConstValue();
}
symtab.const_values[ident] = const_val;
```

对于二元表达式，如果两个操作数都是常量，直接计算结果：

```cpp
if (lhs->IsConst() && rhs->IsConst()) {
  int l = lhs->GetConstValue();
  int r = rhs->GetConstValue();
  int result = 0;
  switch (op) {
    case '+': result = l + r; break;
    // ... 其他运算符
  }
  return std::make_unique<IntConst>(result);
}
```

### 5. RISC-V 代码生成 (riscv.cpp)

#### 新增指令处理
- `KOOPA_RVT_ALLOC` - 在栈上分配 4 字节空间
- `KOOPA_RVT_STORE` - 将值存储到栈上
- `KOOPA_RVT_LOAD` - 从栈上加载值

#### 栈帧管理
使用 `value_stack` 映射来跟踪每个值在栈上的位置：

```cpp
static std::map<const koopa_raw_value_data*, int> value_stack;
static int stack_size = 0;

void RiscVGenerator::VisitAlloc(const koopa_raw_value_t &value) {
  int addr = stack_size;
  stack_size -= 4;
  value_stack[value] = addr;
}
```

## 测试用例

### 测试1：常量定义与常量折叠
```c
int main() {
  const int x = 233 * 4;  // 编译时计算为 932
  int y = 10;
  y = y + x / 2;          // y = 10 + 466 = 476
  return y;
}
```

生成的 Koopa IR：
```
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

### 测试2：变量赋值
```c
int main() {
  int a = 5;
  int b = 10;
  a = a + b;
  return a;
}
```

生成的 Koopa IR：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 5, %0
  %1 = alloc i32
  store 10, %1
  %2 = load %0
  %3 = load %1
  %4 = add %2, %3
  store %4, %0
  %5 = load %0
  ret %5
}
```

### 测试3：多变量操作
```c
int main() {
  int x = 1;
  int y = 2;
  int z = 3;
  x = y + z;
  y = x * 2;
  z = y - x;
  return z;
}
```

## 语义规范说明

1. **常量定义**：`ConstDef` 中的 `ConstExp` 必须在编译时被计算出来
2. **变量定义**：不含初始值时，运行时实际初值未定义
3. **作用域**：Block 内定义的变量/常量在定义处到该语句块尾的范围内有效
4. **赋值语句**：赋值号左边的 `LVal` 必须是变量，不能是常量
5. **常量使用**：`ConstExp` 内使用的 `IDENT` 必须是常量
