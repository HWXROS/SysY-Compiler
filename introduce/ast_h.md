# ast.h — 抽象语法树（AST）定义

## 文件作用

**ast.h** 定义了编译器的**抽象语法树（Abstract Syntax Tree，AST）**结构。AST 是源代码的树形表示，每个节点对应一种语法结构（如函数、变量声明、表达式、语句等）。这是编译器前端的输出，也是 IR 生成阶段的输入。

---

## 重要 Class

### SymbolTable — 符号表

管理变量和常量的作用域信息。

```cpp
class SymbolTable {
 public:
  std::map<std::string, int> const_values;   // 常量名 → 常量值
  std::map<std::string, int> var_addrs;       // 变量名 → 内存地址ID
  SymbolTable *parent;                        // 父作用域指针
  SymbolTable(SymbolTable *p = nullptr) : parent(p) {}
};
```

**常用方法：**

| 方法 | 作用 |
|------|------|
| `Exists(name)` | 检查变量/常量是否存在于当前或父作用域 |
| `ExistsLocal(name)` | 只检查当前作用域（用于检测重复定义） |
| `IsConst(name)` | 判断是否为常量 |
| `IsVar(name)` | 判断是否为变量 |
| `GetConstValue(name)` | 获取常量值，沿作用域链向上查找 |
| `GetVarAddr(name)` | 获取变量地址，沿作用域链向上查找 |

**作用域链原理：** 每个内层块创建一个新的 `SymbolTable`，其 `parent` 指向外层的 `SymbolTable`。查找变量时先查当前层，找不到则递归查父层，实现嵌套作用域的变量遮蔽。

---

### BaseAST — 所有 AST 节点的基类

```cpp
class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump() const = 0;                                           // 打印 AST（用于调试）
  virtual std::unique_ptr<KoopaValue> GenIR(...) const = 0;  // 生成 IR
};
```

所有具体 AST 类（如 `FuncDefAST`、`VarDeclAST` 等）都继承自 `BaseAST`。

---

### CompUnitAST — 编译单元（整个程序）

```cpp
class CompUnitAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_def;  // 唯一函数定义
};
```

代表整个程序，是 AST 的根节点。

---

### FuncDefAST — 函数定义

```cpp
class FuncDefAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_type;  // 返回类型（int）
  std::string ident;                     // 函数名
  std::unique_ptr<BaseAST> block;       // 函数体（BlockAST）
};
```

代表 `int main() { ... }` 这样的函数定义。

---

### BlockAST — 语句块

```cpp
class BlockAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;  // 块内的语句/声明列表
};
```

代表 `{}` 包围的代码块。**有两个 `GenIR` 重载：**
- `GenIR(IRBuilder&, SymbolTable&)` — 为函数体调用，创建新 BasicBlock
- `GenIR(BasicBlock*, IRBuilder&, SymbolTable&)` — 为嵌套块调用，复用当前 BasicBlock

---

### ConstDeclAST / VarDeclAST — 常量/变量声明

```cpp
class ConstDeclAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> const_defs;  // 常量定义列表
};

class VarDeclAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> var_defs;    // 变量定义列表
};
```

---

### StmtAST — 语句

```cpp
class StmtAST : public BaseAST {
 public:
  StmtType type;              // 语句类型
  std::unique_ptr<BaseAST> exp;   // 表达式（用于 return、赋值右侧、表达式语句）
  std::unique_ptr<BaseAST> lval;  // 左值（用于赋值语句）
};
```

`StmtType` 枚举定义了所有语句类型：
- `RETURN` — 返回语句
- `ASSIGN` — 赋值语句
- `EXPR` — 表达式语句（计算但丢弃结果）
- `EMPTY` — 空语句
- `BLOCK` — 块语句
- `BREAK` — break 语句
- `CONTINUE` — continue 语句

---

### IfStmtAST / WhileStmtAST — 控制流

```cpp
class IfStmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> cond;       // 条件
  std::unique_ptr<BaseAST> then_stmt;  // then 分支
  std::unique_ptr<BaseAST> else_stmt;  // else 分支（可为空）
};

class WhileStmtAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> cond;  // 条件
  std::unique_ptr<BaseAST> body;  // 循环体
};
```

---

### LValAST / NumberAST / UnaryExprAST / BinaryExprAST — 表达式

```cpp
class LValAST : public BaseAST {
 public:
  std::string ident;  // 变量名
};

class NumberAST : public BaseAST {
 public:
  int value;  // 整数字面量
};

class BinaryExprAST : public BaseAST {
 public:
  char op;                          // 运算符
  std::unique_ptr<BaseAST> left;    // 左操作数
  std::unique_ptr<BaseAST> right;   // 右操作数
};
```

---

## 全局线程局部变量

编译器在生成 IR 时使用全局状态来管理控制流：

```cpp
extern thread_local Function* g_current_func;          // 当前函数
extern thread_local BasicBlock* g_current_bb;         // 当前基本块
extern thread_local std::vector<BasicBlock*> g_end_block_stack;   // if-end 块栈
extern thread_local std::vector<LoopContext> g_loop_stack;         // 循环上下文栈
extern thread_local int g_loop_depth;                  // 当前循环嵌套深度
extern thread_local std::set<BasicBlock*> g_protected_blocks;      // 受保护块集合
```
