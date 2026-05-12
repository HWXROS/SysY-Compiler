# Lv8 函数和全局变量

## 本章新增内容

- 支持顶层声明列表（全局变量 + 函数混合）
- 支持 `void` 返回类型函数
- 支持函数形参列表
- 支持函数调用表达式
- 支持 SysY 库函数（`putint`、`putch`、`getint`）

---

## 第一步：支持顶层声明列表（CompUnit 修改）

### 背景

之前的 `CompUnit` 只支持一个 `FuncDef`，无法处理全局变量和多个函数。

### 修改前

```yacc
CompUnit
  : FuncDef { ... }
  | Decl { ... }
  ;
```

### 修改后

```yacc
CompUnit
  : CompUnit Decl {
      auto comp_unit = static_cast<CompUnitAST*>($1);
      comp_unit->items.push_back(unique_ptr<BaseAST>($2));
      $$ = comp_unit;
    }
  | CompUnit FuncDef {
      auto comp_unit = static_cast<CompUnitAST*>($1);
      comp_unit->items.push_back(unique_ptr<BaseAST>($2));
      $$ = comp_unit;
    }
  | Decl {
      auto comp_unit = new CompUnitAST();
      comp_unit->items.push_back(unique_ptr<BaseAST>($1));
      $$ = comp_unit;
    }
  | FuncDef {
      auto comp_unit = new CompUnitAST();
      comp_unit->items.push_back(unique_ptr<BaseAST>($1));
      $$ = comp_unit;
    }
  ;
```

### 目的

让编译器能够处理多个声明和函数定义，例如：
```c
int global_var = 10;      // 全局变量
void print();             // 函数声明
int main() { return 0; }  // 主函数
```

### 实现

将 `CompUnit` 改为**左递归文法**，支持 `(Decl | FuncDef)*` 列表结构。通过 `items` 向量存储所有顶层元素，在 IR 生成时遍历处理。

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| sysy.y | CompUnit 规则改为左递归，支持多个声明/函数 |
| ast.h | CompUnitAST 新增 `items` 向量替代单个 `func_def` |
| ast.cpp | CompUnitAST::GenIR 遍历 `items` 生成 IR |

### 代码改动详情

#### sysy.y — CompUnit 文法规则
```yacc
CompUnit
  : CompUnit Decl {
      auto comp_unit = static_cast<CompUnitAST*>($1);
      comp_unit->items.push_back(unique_ptr<BaseAST>($2));
      $$ = comp_unit;
    }
  | CompUnit FuncDef {
      auto comp_unit = static_cast<CompUnitAST*>($1);
      comp_unit->items.push_back(unique_ptr<BaseAST>($2));
      $$ = comp_unit;
    }
  | Decl {
      auto comp_unit = new CompUnitAST();
      comp_unit->items.push_back(unique_ptr<BaseAST>($1));
      $$ = comp_unit;
    }
  | FuncDef {
      auto comp_unit = new CompUnitAST();
      comp_unit->items.push_back(unique_ptr<BaseAST>($1));
      $$ = comp_unit;
    }
  ;
```

#### ast.h — CompUnitAST 结构
```cpp
// 修改前
std::unique_ptr<BaseAST> func_def;

// 修改后
std::vector<std::unique_ptr<BaseAST>> items;
```

#### ast.cpp — CompUnitAST::GenIR
```cpp
// 修改前
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  auto func = static_cast<FuncDefAST*>(func_def.get());
  program->AddFunc(func->GenIR());
  return program;
}

// 修改后
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  SymbolTable global_symtab;
  for (const auto &item : items) {
    if (auto* func_def = dynamic_cast<FuncDefAST*>(item.get())) {
      program->AddFunc(func_def->GenIR(global_symtab));
    } else if (auto* var_decl = dynamic_cast<VarDeclAST*>(item.get())) {
      var_decl->GenIR(nullptr, IRBuilder(), global_symtab);
    } else if (auto* const_decl = dynamic_cast<ConstDeclAST*>(item.get())) {
      const_decl->GenIR(nullptr, IRBuilder(), global_symtab);
    }
  }
  return program;
}
```

---

## 第二步：支持全局变量

### 背景

全局变量需要在 `CompUnit` 层级声明，其作用域从声明处延伸到文件结尾。

### 核心思路

1. 在 `CompUnitAST::GenIR` 中创建**全局符号表** `global_symtab`
2. 全局变量声明只注册到符号表，**不生成 alloc/store 指令**
3. 函数符号表的 `parent` 指向全局符号表，实现全局变量的查找

### 修改前

```cpp
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  for (const auto &item : items) {
    auto func_def = static_cast<FuncDefAST*>(item.get());
    program->AddFunc(func_def->GenIR());
  }
  return program;
}
```

### 修改后

```cpp
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  SymbolTable global_symtab;  // 新增全局符号表
  for (const auto &item : items) {
    if (auto* func_def = dynamic_cast<FuncDefAST*>(item.get())) {
      program->AddFunc(func_def->GenIR(global_symtab));
    } else if (auto* var_decl = dynamic_cast<VarDeclAST*>(item.get())) {
      var_decl->GenIR(nullptr, IRBuilder(), global_symtab);  // bb=nullptr 表示全局
    } else if (auto* const_decl = dynamic_cast<ConstDeclAST*>(item.get())) {
      const_decl->GenIR(nullptr, IRBuilder(), global_symtab);
    }
  }
  return program;
}
```

### 目的

支持全局变量的声明和访问，例如：
```c
int global = 100;  // 全局变量
int main() {
  return global;   // 访问全局变量
}
```

### 实现

- 使用 `bb == nullptr` 区分全局/局部变量
- 全局变量仅注册到符号表，不生成 IR 指令（由运行时初始化）
- 函数符号表的 `parent` 指向全局符号表

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| ast.cpp | CompUnitAST::GenIR 新增全局符号表，区分处理声明和函数 |
| ast.h | FuncDefAST::GenIR 签名新增 `global_symtab` 参数 |
| ast.cpp | FuncDefAST::GenIR 创建符号表时设置 parent |
| ast.cpp | VarDefAST::GenIR 检查 `bb == nullptr` 判断是否全局 |

### 关键区别

| 场景 | bb 值 | 处理方式 |
|------|--------|---------|
| 全局变量声明 | `nullptr` | 只注册到符号表，不生成 alloc/store |
| 局部变量声明 | 非 `nullptr` | 正常生成 alloc/store 指令 |

---

## 第三步：支持 void 函数类型

### 背景

SysY 支持 `void` 返回类型函数，如 `void print() { ... }`。

### 修改前

```yacc
FuncType
  : INT {
    $$ = new FuncTypeAST();
  }
  ;
```

### 修改后

```yacc
FuncType
  : INT {
    $$ = new FuncTypeAST();
  }
  | VOID {
    auto ast = new FuncTypeAST();
    ast->is_void = true;
    $$ = ast;
  }
  ;
```

### 目的

支持无返回值函数，例如：
```c
void print_hello() {
  putch(72);  // 输出 'H'
}
```

### 实现

- 在 `sysy.l` 中添加 `VOID` token
- 在 `FuncTypeAST` 中添加 `is_void` 标志
- 在 `Function` 类中区分 void/int 返回类型
- IR 生成时根据 `is_void` 生成不同的 `ret` 指令

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| sysy.l | 新增 `VOID` token 识别 |
| sysy.y | FuncType 规则新增 VOID 分支 |
| ast.h | FuncTypeAST 新增 `is_void` 字段 |
| ir.h | Function 类新增 `is_void` 参数 |
| ast.cpp | FuncDefAST::GenIR 传递 `is_void` 给 Function |

### 代码改动详情

#### sysy.l — 新增 VOID token
```c
"void"          { return VOID; }
```

#### ast.h — FuncTypeAST
```cpp
class FuncTypeAST : public BaseAST {
 public:
  bool is_void = false;
  void Dump() const override {
    std::cout << "FuncTypeAST { " << (is_void ? "void" : "int") << " }";
  }
};
```

#### ir.h — Function 类
```cpp
class Function {
  bool is_void;
 public:
  Function(const std::string &n, const std::string &rt, bool iv = false)
      : name(n), ret_type(rt), is_void(iv) {}
  void Dump(std::ostream &os) const {
    os << "fun @" << name << "(): " << (is_void ? "" : ret_type) << " {\n";
    // ...
    if (!blocks.back()->HasTerminator()) {
      if (is_void) {
        os << "  ret\n";
      } else {
        os << "  ret 0\n";
      }
    }
  }
};
```

---

## 第四步：支持函数形参列表

### 背景

函数可以有形参，如 `int add(int x, int y) { return x + y; }`。

### 修改前

```yacc
FuncDef
  : FuncType IDENT '(' ')' Block { ... }
  ;
```

### 修改后

```yacc
FuncDef
  : FuncType IDENT '(' FuncFParams ')' Block {
    auto ast = new FuncDefAST();
    ast->func_type = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<string>($2);
    ast->params = *unique_ptr<vector<BaseAST*>>($4);
    ast->block = unique_ptr<BaseAST>($6);
    $$ = ast;
  }
  ;

FuncFParams
  : FuncFParam {
    auto list = new vector<BaseAST*>();
    list->push_back($1);
    $$ = list;
  }
  | FuncFParams ',' FuncFParam {
    auto list = static_cast<vector<BaseAST*>>($1);
    list->push_back($3);
    $$ = list;
  }
  ;

FuncFParam
  : BType IDENT {
    auto ast = new FuncFParamAST();
    ast->ident = *unique_ptr<string>($2);
    $$ = ast;
  }
  ;
```

### 目的

支持带参数的函数定义和调用，例如：
```c
int add(int a, int b) {
  return a + b;
}
int main() {
  return add(1, 2);
}
```

### 实现

- 新增 `FuncFParams` 和 `FuncFParam` 语法规则
- 新增 `FuncFParamAST` 类存储参数信息
- 在 `FuncDefAST` 中添加 `params` 向量
- IR 生成时将参数视为局部变量处理

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| sysy.y | 新增 FuncFParams、FuncFParam 规则，修改 FuncDef 规则 |
| ast.h | 新增 FuncFParamAST 类 |
| ast.h | FuncDefAST 新增 `params` 向量 |
| ast.cpp | FuncDefAST::GenIR 处理参数分配 |

### 代码改动详情

#### ast.h — FuncFParamAST
```cpp
class FuncFParamAST : public BaseAST {
 public:
  std::string ident;
  void Dump() const override {
    std::cout << "FuncFParamAST { " << ident << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override {
    return nullptr;
  }
};
```

#### ast.cpp — FuncDefAST::GenIR 参数处理
```cpp
for (const auto &param : params) {
  auto param_ast = static_cast<FuncFParamAST*>(param.get());
  int addr_id = builder.NewId();
  entry_bb->AddInst(std::make_unique<AllocInst>(addr_id));
  symtab.var_addrs[param_ast->ident] = addr_id;
}
```

---

## 第五步：支持函数调用表达式

### 背景

SysY 支持函数调用表达式，如 `putint(1);` 或 `func(1, 2);`。

### 修改前

```yacc
UnaryExp
  : PrimaryExp { ... }
  | '+' UnaryExp { ... }
  | '-' UnaryExp { ... }
  | '!' UnaryExp { ... }
  ;
```

### 修改后

```yacc
UnaryExp
  : PrimaryExp { ... }
  | '+' UnaryExp { ... }
  | '-' UnaryExp { ... }
  | '!' UnaryExp { ... }
  | IDENT '(' ')' {
    auto ast = new CallExprAST();
    ast->ident = *unique_ptr<string>($1);
    $$ = ast;
  }
  | IDENT '(' FuncRParams ')' {
    auto ast = new CallExprAST();
    ast->ident = *unique_ptr<string>($1);
    ast->args = *unique_ptr<vector<BaseAST*>>($3);
    $$ = ast;
  }
  ;

FuncRParams
  : Exp {
    auto list = new vector<BaseAST*>();
    list->push_back($1);
    $$ = list;
  }
  | FuncRParams ',' Exp {
    auto list = static_cast<vector<BaseAST*>>($1);
    list->push_back($3);
    $$ = list;
  }
  ;
```

### 目的

支持函数调用表达式，例如：
```c
int result = add(1, 2);  // 有返回值调用
putint(result);          // 无返回值调用
```

### 实现

- 在 `UnaryExp` 中添加函数调用规则
- 新增 `CallExprAST` 类存储调用信息
- 新增 `CallInst` IR 指令
- 在符号表中记录函数是否为 void 类型

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| sysy.y | UnaryExp 新增函数调用规则，新增 FuncRParams 规则 |
| ast.h | 新增 CallExprAST 类 |
| ir.h | 新增 CallInst 类 |
| ast.h | SymbolTable 新增 `func_is_void` 映射 |
| ast.cpp | CallExprAST::GenIR 生成调用指令 |

### 代码改动详情

#### ir.h — CallInst 类
```cpp
class CallInst : public Instruction {
  std::string func_name;
  std::vector<std::unique_ptr<KoopaValue>> args;
  bool is_void;
 public:
  CallInst(const std::string &name, std::vector<std::unique_ptr<KoopaValue>> a, bool iv = false)
      : func_name(name), args(std::move(a)), is_void(iv) {}
  void Dump(std::ostream &os) const override {
    if (!is_void) {
      os << "  %call_inst = call @" << func_name << "(";
    } else {
      os << "  call @" << func_name << "(";
    }
    for (size_t i = 0; i < args.size(); ++i) {
      args[i]->Dump(os);
      if (i < args.size() - 1) os << ", ";
    }
    os << ")\n";
  }
};
```

#### ast.cpp — CallExprAST::GenIR
```cpp
std::unique_ptr<KoopaValue> CallExprAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  std::vector<std::unique_ptr<KoopaValue>> args_ir;
  for (const auto &arg : args) {
    args_ir.push_back(arg->GenIR(bb, builder, symtab));
  }
  bool is_void_func = symtab.IsFuncVoid(ident);
  if (is_void_func) {
    bb->AddInst(std::make_unique<CallInst>(ident, std::move(args_ir), true));
    return nullptr;
  } else {
    int id = builder.NewId();
    bb->AddInst(std::make_unique<CallInst>(ident, std::move(args_ir), false));
    return std::make_unique<ValueRef>(id);
  }
}
```

### 关键点

| 场景 | IR 格式 |
|------|---------|
| void 函数调用 | `call @putint(1)` |
| int 函数调用 | `%call_inst = call @func(1)` |
| void 函数 return | `ret` |
| int 函数 return | `ret 0` 或 `ret <value>` |

---

## 第六步：支持 SysY 库函数

### 背景

SysY 提供内置的库函数，可以在不声明的情况下使用：
- `putint(int value)` — void，输出整数
- `putch(int c)` — void，输出字符
- `getint()` — int，读取整数

### 修改前

```cpp
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  SymbolTable global_symtab;
  for (const auto &item : items) {
    // ...
  }
  return program;
}
```

### 修改后

```cpp
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  SymbolTable global_symtab;
  // 预注册库函数
  global_symtab.func_is_void["putint"] = true;
  global_symtab.func_is_void["putch"] = true;
  global_symtab.func_is_void["getint"] = false;
  for (const auto &item : items) {
    // ...
  }
  return program;
}
```

### 目的

支持 SysY 标准库函数的调用，例如：
```c
int main() {
  int x = getint();  // 读取输入
  putint(x);         // 输出整数
  putch(10);         // 输出换行
  return 0;
}
```

### 实现

在全局符号表中预注册库函数的返回类型（`func_is_void`），`CallExprAST::GenIR` 通过 `symtab.IsFuncVoid()` 判断函数类型并生成正确的 IR。

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| ast.cpp | CompUnitAST::GenIR 预注册库函数 |

---

## 第七步：修复语法冲突

### 背景

在实现过程中，遇到了 **dangling else** 语法冲突问题，导致编译失败。

### 问题原因

```yacc
Stmt
  : IF '(' Exp ')' Stmt
  | IF '(' Exp ')' Stmt ELSE Stmt
  ;
```

当遇到 `if (a) if (b) x; else y;` 时，`else` 可以匹配第一个或第二个 `if`，产生二义性。

### 解决方案

将 `Stmt` 分为 `MatchedStmt` 和 `UnmatchedStmt`，使用经典的 dangling else 解决方案：

```yacc
Stmt
  : MatchedStmt
  | UnmatchedStmt
  ;

MatchedStmt
  : RETURN ';' { ... }
  | RETURN Exp ';' { ... }
  | LVal '=' Exp ';' { ... }
  | Exp ';' { ... }
  | ';' { ... }
  | Block { ... }
  | IF '(' Exp ')' MatchedStmt ELSE MatchedStmt { ... }
  | WHILE '(' Exp ')' MatchedStmt { ... }
  | BREAK ';' { ... }
  | CONTINUE ';' { ... }
  ;

UnmatchedStmt
  : IF '(' Exp ')' Stmt { ... }
  | IF '(' Exp ')' MatchedStmt ELSE UnmatchedStmt { ... }
  ;
```

### 目的

消除 dangling else 二义性，确保 `else` 总是匹配最近的 `if`。

### 实现

- `MatchedStmt`：匹配完整的语句（有对应的 else 或复合语句）
- `UnmatchedStmt`：匹配不完整的 if 语句（无 else）
- `else` 只能跟在 `MatchedStmt` 后面

### 附加措施

启用 GLR 解析器处理剩余的语法冲突：
```yacc
%glr-parser
```

---

## 第八步：IR 生成优化

### 背景

在实现过程中，遇到了两个 IR 生成问题：
1. 基本块名称格式不正确，使用了 `%name =` 而不是 `%name:`
2. 生成了不可达的基本块，导致 Koopa 解析器报错

### 问题 1：基本块名称格式错误

**修改前**：
```cpp
void Dump(std::ostream &os) const {
  os << "%" << name << " =:\n";  // 错误格式
  // ...
}
```

**修改后**：
```cpp
void Dump(std::ostream &os) const {
  os << "%" << name << ":\n";  // 正确格式
  // ...
}
```

### 问题 2：不可达基本块

**修改前**：
```cpp
// IfStmtAST::GenIR 中总是创建 end_bb
end_bb = g_current_func->CreateBlock(end_label);
g_end_block_stack.push_back(end_bb);
g_current_bb = end_bb;
```

**修改后**：
```cpp
// 如果 then 和 else 语句都有终止符，不需要创建 end_bb
if (!then_has_ret || !else_has_ret) {
  end_bb = g_current_func->CreateBlock(end_label);
  g_end_block_stack.push_back(end_bb);
  pushed_to_stack = true;
}
if (end_bb) {
  g_current_bb = end_bb;
}
```

### 问题 3：非 void 函数缺少 return 语句

**修改前**：
```cpp
// FuncDefAST::GenIR 不处理缺少 return 的情况
```

**修改后**：
```cpp
// 如果函数不是 void 类型，且最后一个基本块没有终止符，添加 ret 0
if (!is_void && !func->GetBlock(func->GetBlockCount() - 1)->HasTerminator()) {
  func->GetBlock(func->GetBlockCount() - 1)->AddInst(std::make_unique<RetInst>(std::make_unique<IntConst>(0)));
}
```

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| ir.h | 修复基本块名称格式 |
| ast.cpp | IfStmtAST::GenIR 避免创建不可达基本块 |
| ast.cpp | FuncDefAST::GenIR 为非 void 函数自动添加 return |

---

## 第九步：RISC-V 汇编生成修复

### 背景

RISC-V 汇编生成时，基本块名称包含 `%` 前缀，导致汇编语法错误。

### 修改前

```cpp
void RiscVGenerator::Visit(const koopa_raw_basic_block_t &bb) {
  *os_ << bb->name << ":\n";  // 包含 % 前缀
  // ...
}
```

### 修改后

```cpp
void RiscVGenerator::Visit(const koopa_raw_basic_block_t &bb) {
  const char* name = bb->name;
  if (name[0] == '%') {
    name += 1;  // 去除 % 前缀
  }
  *os_ << name << ":\n";
  // ...
}
```

### 目的

确保生成的 RISC-V 汇编语法正确，基本块名称不包含 `%` 前缀。

### 修改的文件

| 文件 | 修改内容 |
|------|---------|
| riscv.cpp | Visit(bb) 和 Visit(branch) 去除 % 前缀 |

---

## 测试用例

### 全局变量测试

```c
int var = 1;
int main() {
  int a = var;
  return a;
}
```

**生成的 IR：**
```koopa
fun @main(): i32 {
%entry:
  %0 = alloc i32
  %1 = load @var
  store %1, %0
  %2 = load %0
  ret %2
}
```

### void 函数测试

```c
void print_hello() {
  putch(72);
  putch(10);
}
int main() {
  print_hello();
  return 0;
}
```

**生成的 IR：**
```koopa
fun @print_hello(): {
%entry:
  call @putch(72)
  call @putch(10)
  ret
}

fun @main(): i32 {
%entry:
  call @print_hello()
  ret 0
}
```

### 函数形参与调用测试

```c
int add(int a, int b) {
  return a + b;
}
int main() {
  int result = add(1, 2);
  return result;
}
```

**生成的 IR：**
```koopa
fun @add(): i32 {
%entry:
  %0 = alloc i32
  %1 = alloc i32
  %2 = load %0
  %3 = load %1
  %4 = add %2, %3
  ret %4
}

fun @main(): i32 {
%entry:
  %0 = alloc i32
  %1 = call @add(1, 2)
  store %1, %0
  %2 = load %0
  ret %2
}
```

---

## 总结

本章实现了以下功能：

| 功能 | 说明 |
|------|------|
| 顶层声明列表 | CompUnit 支持多个声明和函数 |
| 全局变量 | 支持全局变量声明和访问 |
| void 函数 | 支持无返回值函数 |
| 函数形参 | 支持带参数的函数定义 |
| 函数调用 | 支持函数调用表达式 |
| 库函数 | 支持 putint、putch、getint |
| 语法冲突修复 | 解决 dangling else 问题 |
| IR 生成优化 | 修复基本块格式和不可达块问题 |
| RISC-V 修复 | 修复基本块名称处理 |

编译器现在能够处理包含函数定义、全局变量、函数调用和库函数的完整 SysY 程序。