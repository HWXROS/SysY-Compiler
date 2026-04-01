# Lv5. 语句块和作用域 - 实现笔记

## 教程要求

本章实现一个能够处理语句块和作用域的编译器，支持以下特性：

1. **嵌套的语句块**：Block 可以嵌套
2. **作用域管理**：每个 Block 创建新的作用域
3. **变量遮蔽**：内层作用域可以定义与外层同名的变量
4. **新的语句类型**：
   - 表达式语句：`Exp ";"`
   - 空语句：`";"`
   - 块语句：`Block`
   - 可选返回值的 return：`"return" [Exp] ";"`

## 示例程序

```c
int main() {
  int a = 1, b = 2;
  {
    int a = 2;
    b = b + a;
  }
  return b;
}
```

预期结果：返回 4（外层 b=2，内层 a=2，b = b + a = 2 + 2 = 4）

## 语法规范

### 新增语法规则

```
CompUnit      ::= FuncDef;

Decl          ::= ConstDecl | VarDecl;
ConstDecl     ::= "const" BType ConstDef {"," ConstDef} ";";
BType         ::= "int";
ConstDef      ::= IDENT "=" ConstInitVal;
ConstInitVal  ::= ConstExp;
VarDecl       ::= BType VarDef {"," VarDef} ";";
VarDef        ::= IDENT | IDENT "=" InitVal;
InitVal       ::= Exp;

FuncDef       ::= FuncType IDENT "(" ")" Block;
FuncType      ::= "int";

Block         ::= "{" {BlockItem} "}";
BlockItem     ::= Decl | Stmt;
Stmt          ::= LVal "=" Exp ";"
                | [Exp] ";"
                | Block
                | "return" [Exp] ";";

Exp           ::= LOrExp;
LVal          ::= IDENT;
PrimaryExp    ::= "(" Exp ")" | LVal | Number;
Number        ::= INT_CONST;
UnaryExp      ::= PrimaryExp | UnaryOp UnaryExp;
UnaryOp       ::= "+" | "-" | "!";
MulExp        ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp;
AddExp        ::= MulExp | AddExp ("+" | "-") MulExp;
RelExp        ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp;
EqExp         ::= RelExp | EqExp ("==" | "!=") RelExp;
LAndExp       ::= EqExp | LAndExp "&&" EqExp;
LOrExp        ::= LAndExp | LOrExp "||" LAndExp;
ConstExp      ::= Exp;
```

### 语义规范

1. **单个 Exp 可以作为 Stmt**：Exp 会被求值（即存在副作用），但所求的值会被丢弃
2. **Block 表示语句块**：语句块会创建作用域，语句块内声明的变量的生存期在该语句块内
3. **作用域嵌套**：因为语句块是可以嵌套的，所以作用域也是可以嵌套的
4. **变量遮蔽**：语句块内可以再次定义与语句块外同名的变量或常量，其作用域从定义处开始到该语句块尾结束，它覆盖了语句块外的同名变量或常量
5. **唯一性约束**：对于同一个标识符，在同一作用域中最多存在一次声明
6. **LVal 约束**：LVal 必须是当前作用域内，该 Exp 语句之前曾定义过的变量或常量。赋值号左边的 LVal 必须是变量

## AST 结构分析

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
                 │    │
                 │    ├── ConstDecl (常量声明)
                 │    │    │
                 │    │    ├── BType (基本类型)
                 │    │    └── ConstDef (常量定义)
                 │    │         │
                 │    │         ├── IDENT (常量名)
                 │    │         └── ConstInitVal (常量初始值)
                 │    │              │
                 │    │              └── ConstExp (常量表达式)
                 │    │                   │
                 │    │                   └── Exp (表达式)
                 │    │                        │
                 │    │                        └── LOrExp (逻辑或表达式)
                 │    │                             │
                 │    │                             └── ... (逐层向下)
                 │    │
                 │    └── VarDecl (变量声明)
                 │         │
                 │         ├── BType (基本类型)
                 │         └── VarDef (变量定义)
                 │              │
                 │              ├── IDENT (变量名)
                 │              └── InitVal (变量初始值，可选)
                 │                   │
                 │                   └── Exp (表达式)
                 │                        │
                 │                        └── ... (同上)
                 │
                 └── Stmt (语句)
                      │
                      ├── LVal "=" Exp ";" (赋值语句)
                      │    │
                      │    ├── LVal (左值，引用变量/常量)
                      │    └── Exp (表达式)
                      │         │
                      │         └── ... (同上)
                      │
                      ├── Exp ";" (表达式语句)
                      │    │
                      │    └── Exp (表达式，结果被丢弃)
                      │         │
                      │         └── ... (同上)
                      │
                      ├── ";" (空语句)
                      │
                      ├── Block (嵌套语句块)
                      │    │
                      │    └── BlockItem (块内项目)
                      │         │
                      │         └── ... (递归结构)
                      │
                      └── "return" [Exp] ";" (返回语句)
                           │
                           └── Exp (返回值，可选)
                                │
                                └── ... (同上)
```

**关键变化（相比 Lv4）**：
- Block 现在可以包含多个 BlockItem，形成递归结构
- Stmt 新增了 5 种类型：赋值语句、表达式语句、空语句、块语句、返回语句
- 支持嵌套的 Block，每个 Block 创建新的作用域
- 内层作用域可以遮蔽外层同名变量

## 实现细节

### 1. 语法分析器修改 (sysy.y)

#### 新增 Stmt 类型

```yacc
Stmt
  : RETURN ';' {
      // return 不带返回值
      auto ast = new StmtAST();
      ast->type = StmtType::RETURN;
      ast->exp = nullptr;
      $$ = ast;
    }
  | RETURN Exp ';' {
      // return 带返回值
      auto ast = new StmtAST();
      ast->type = StmtType::RETURN;
      ast->exp = unique_ptr<BaseAST>($2);
      $$ = ast;
    }
  | LVal '=' Exp ';' {
      // 赋值语句
      auto ast = new StmtAST();
      ast->type = StmtType::ASSIGN;
      ast->lval = unique_ptr<BaseAST>($1);
      ast->exp = unique_ptr<BaseAST>($3);
      $$ = ast;
    }
  | Exp ';' {
      // 表达式语句
      auto ast = new StmtAST();
      ast->type = StmtType::EXPR;
      ast->exp = unique_ptr<BaseAST>($1);
      $$ = ast;
    }
  | ';' {
      // 空语句
      auto ast = new StmtAST();
      ast->type = StmtType::EMPTY;
      ast->exp = nullptr;
      $$ = ast;
    }
  | Block {
      // 块语句
      $$ = $1;
    }
  ;
```

### 2. AST 修改 (ast.h)

#### 新增 StmtType 枚举

```cpp
enum class StmtType {
  RETURN,
  ASSIGN,
  EXPR,      // 表达式语句
  EMPTY,     // 空语句
  BLOCK      // 块语句（BlockAST 也用于 Stmt）
};
```

#### 作用域管理 - SymbolTable 修改

```cpp
class SymbolTable {
 public:
  std::map<std::string, int> const_values;
  std::map<std::string, int> var_addrs;
  SymbolTable *parent;  // 父作用域
  
  SymbolTable(SymbolTable *p = nullptr) : parent(p) {}
  
  // 在当前作用域检查是否存在（不查找父作用域）
  bool ExistsLocal(const std::string &name) const {
    return const_values.count(name) || var_addrs.count(name);
  }
  
  // 在当前作用域或父作用域检查是否存在
  bool Exists(const std::string &name) const {
    if (const_values.count(name) || var_addrs.count(name)) {
      return true;
    }
    if (parent) {
      return parent->Exists(name);
    }
    return false;
  }
  
  bool IsConst(const std::string &name) const {
    if (const_values.count(name)) {
      return true;
    }
    if (parent) {
      return parent->IsConst(name);
    }
    return false;
  }
  
  bool IsVar(const std::string &name) const {
    if (var_addrs.count(name)) {
      return true;
    }
    if (parent) {
      return parent->IsVar(name);
    }
    return false;
  }
  
  int GetConstValue(const std::string &name) const {
    auto it = const_values.find(name);
    if (it != const_values.end()) {
      return it->second;
    }
    if (parent) {
      return parent->GetConstValue(name);
    }
    return 0;
  }
  
  int GetVarAddr(const std::string &name) const {
    auto it = var_addrs.find(name);
    if (it != var_addrs.end()) {
      return it->second;
    }
    if (parent) {
      return parent->GetVarAddr(name);
    }
    return 0;
  }
};
```

**关键点**：
- 添加 `parent` 指针形成作用域链
- `ExistsLocal` 只检查当前作用域（用于检测重复定义）
- `Exists`、`IsConst`、`IsVar`、`GetConstValue`、`GetVarAddr` 都会沿作用域链向上查找

### 3. IR 生成修改 (ast.cpp)

#### BlockAST 的作用域处理

```cpp
std::unique_ptr<BasicBlock> BlockAST::GenIR(SymbolTable &symtab) const {
  auto bb = std::make_unique<BasicBlock>("entry");
  IRBuilder builder;
  // 创建新的作用域
  SymbolTable new_symtab(&symtab);
  for (const auto &item : items) {
    item->GenIR(bb.get(), builder, new_symtab);
  }
  return bb;
}

std::unique_ptr<KoopaValue> BlockAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  // 创建新的作用域
  SymbolTable new_symtab(&symtab);
  for (const auto &item : items) {
    item->GenIR(bb, builder, new_symtab);
  }
  return nullptr;
}
```

**关键点**：
- 每个 Block 都会创建新的 `SymbolTable`，父指针指向外层作用域
- 内层作用域的变量定义会遮蔽外层同名变量

#### StmtAST 的新类型处理

```cpp
std::unique_ptr<KoopaValue> StmtAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (type == StmtType::RETURN) {
    if (exp) {
      auto exp_val = exp->GenIR(bb, builder, symtab);
      bb->AddInst(std::make_unique<RetInst>(std::move(exp_val)));
    } else {
      bb->AddInst(std::make_unique<RetInst>(nullptr));
    }
  } else if (type == StmtType::ASSIGN) {
    auto lval_ptr = static_cast<LValAST*>(lval.get());
    auto exp_val = exp->GenIR(bb, builder, symtab);
    int addr_id = symtab.GetVarAddr(lval_ptr->ident);
    bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
  } else if (type == StmtType::EXPR) {
    // 表达式语句：计算表达式但丢弃结果
    if (exp) {
      exp->GenIR(bb, builder, symtab);
    }
  } else if (type == StmtType::EMPTY) {
    // 空语句：什么都不做
  }
  return nullptr;
}
```

### 4. IR 修改 (ir.h)

#### RetInst 处理 nullptr

```cpp
class RetInst : public Instruction {
  std::unique_ptr<KoopaValue> value;
 public:
  RetInst(std::unique_ptr<KoopaValue> v) : value(std::move(v)) {}
  void Dump(std::ostream &os) const override {
    os << "  ret ";
    if (value) {
      value->Dump(os);
    } else {
      os << "0";
    }
    os << "\n";
  }
};
```

## 测试用例

### 测试 1：基本作用域

```c
int main() {
  int a = 1, b = 2;
  {
    int a = 2;
    b = b + a;
  }
  return b;
}
```

**预期 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 1, %0
  %1 = alloc i32
  store 2, %1
  %2 = alloc i32
  store 2, %2
  %3 = load %1
  %4 = load %2
  %5 = add %3, %4
  store %5, %1
  %6 = load %1
  ret %6
}
```

**验证**：返回 4，内层 `a` 遮蔽外层 `a`

### 测试 2：嵌套作用域

```c
int main() {
  int x = 10;
  {
    int y = 20;
    x = x + y;
    {
      int z = 30;
      x = x + y + z;
    }
  }
  return x;
}
```

**预期 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 10, %0
  %1 = alloc i32
  store 20, %1
  %2 = load %0
  %3 = load %1
  %4 = add %2, %3
  store %4, %0
  %5 = alloc i32
  store 30, %5
  %6 = load %0
  %7 = load %1
  %8 = add %6, %7
  %9 = load %5
  %10 = add %8, %9
  store %10, %0
  %11 = load %0
  ret %11
}
```

**验证**：返回 80（10 + 20 = 30, 30 + 20 + 30 = 80）

### 测试 3：表达式语句

```c
int main() {
  int a = 1;
  a + 2;
  return a;
}
```

**预期 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 1, %0
  %1 = load %0
  %2 = add %1, 2
  %3 = load %0
  ret %3
}
```

**验证**：`a + 2` 被计算但结果被丢弃，返回 1

### 测试 4：空语句

```c
int main() {
  int a = 1;
  ;
  return a;
}
```

**预期 IR**：
```
fun @main(): i32 {
%entry:
  %0 = alloc i32
  store 1, %0
  %1 = load %0
  ret %1
}
```

**验证**：空语句不产生任何 IR

### 测试 5：return 不带参数

```c
int main() {
  return;
}
```

**预期 IR**：
```
fun @main(): i32 {
%entry:
  ret 0
}
```

**验证**：返回 0

## 作用域实现原理

### 作用域链

```
全局作用域 (parent = nullptr)
    ↓
函数作用域 (parent = 全局)
    ↓
Block1 作用域 (parent = 函数)
    ↓
Block2 作用域 (parent = Block1)
```

### 变量查找过程

当查找变量 `x` 时：
1. 在当前作用域的 `var_addrs` 和 `const_values` 中查找
2. 如果找到，返回结果
3. 如果没找到且有父作用域，在父作用域中递归查找
4. 如果没找到且没有父作用域，返回默认值（0）

### 变量遮蔽原理

内层作用域定义同名变量时：
1. 在 `new_symtab`（内层作用域）的 `var_addrs` 中添加新条目
2. 查找时会先找到内层的条目（短路）
3. 外层同名变量被"遮蔽"，但仍存在于外层作用域中

## 总结

本章实现了：
1. ✅ 嵌套语句块支持
2. ✅ 作用域管理机制（符号表链）
3. ✅ 变量遮蔽
4. ✅ 表达式语句
5. ✅ 空语句
6. ✅ 可选返回值的 return 语句

关键创新点是 **SymbolTable 的 parent 指针**，通过作用域链实现了：
- 变量的嵌套作用域
- 内层对外层变量的遮蔽
- 跨作用域的变量查找
