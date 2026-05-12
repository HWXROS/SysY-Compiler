# ir.h — 中间表示（IR）数据结构定义

## 文件作用

**ir.h** 定义了编译器的**中间表示（Intermediate Representation，IR）**数据结构。IR 是介于源代码和目标机器码之间的低层次表示，具有以下特点：
- **SSA 形式**：每个值有唯一名字（如 `%0`、`%1`）
- **基于指令**：由一条条指令组成，如 `alloc`、`store`、`load`、`add` 等
- **有基本块**：代码被划分成基本块（BasicBlock），每块内部顺序执行，块间通过跳转

这个文件定义了所有 IR 相关的类（指令、值、基本块、函数等），是 AST → IR 转换的目标结构。

---

## 核心概念图

```
Program (程序)
 └── Function (函数)
      └── BasicBlock (基本块)
           └── Instruction (指令)
                ├── AllocInst    (分配内存)
                ├── StoreInst    (存值到内存)
                ├── LoadInst     (从内存取值)
                ├── BinaryOpInst (二元运算)
                ├── UnaryOpInst  (一元运算)
                ├── BranchInst   (条件分支)
                ├── JumpInst     (无条件跳转)
                └── RetInst      (返回)
```

---

## 重要 Class

### IRBuilder — ID 生成器

```cpp
class IRBuilder {
 public:
  int next_id = 0;
  int NewId() { return next_id++; }  // 生成唯一的 %0, %1, %2 ...
};
```

所有临时值（`%0`、`%1` 等）通过 `IRBuilder::NewId()` 生成。

---

### KoopaValue — IR 值的基类

```cpp
class KoopaValue {
 public:
  virtual ~KoopaValue() = default;
  virtual void Dump(std::ostream &os) const = 0;
  virtual bool IsConst() const { return false; }      // 是否为常量
  virtual int GetConstValue() const { return 0; }    // 获取常量值
};
```

所有 IR 中的值（常量、临时值引用）都继承自它。

---

### IntConst — 整型常量

```cpp
class IntConst : public KoopaValue {
  int value;
 public:
  IntConst(int v) : value(v) {}
  bool IsConst() const override { return true; }
  int GetConstValue() const override { return value; }
  void Dump(std::ostream &os) const override { os << value; }
};
```

表示字面量整数，如 `0`、`7`、`233`。直接内联到指令中，不占用临时寄存器。

---

### ValueRef — 临时值引用

```cpp
class ValueRef : public KoopaValue {
  int id;
 public:
  ValueRef(int i) : id(i) {}
  int GetId() const { return id; }
  void Dump(std::ostream &os) const override { os << "%" << id; }
};
```

表示对之前指令产生的临时值的引用，如 `%3`、`%5`。

---

## 指令类（Instruction 及其子类）

所有指令都继承自 `Instruction`，每个指令有 `Dump()` 方法输出 Koopa IR 文本。

### AllocInst — 内存分配

```cpp
class AllocInst : public Instruction {
  int result_id;  // 分配得到的地址的 ID
 public:
  AllocInst(int id) : result_id(id) {}
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = alloc i32\n";
  }
};
```

**示例：** `int a;` → `%0 = alloc i32`

---

### StoreInst — 存储指令

```cpp
class StoreInst : public Instruction {
  std::unique_ptr<KoopaValue> value;  // 要存储的值
  int addr_id;                        // 目标地址 ID
 public:
  StoreInst(std::unique_ptr<KoopaValue> v, int a)
      : value(std::move(v)), addr_id(a) {}
  void Dump(std::ostream &os) const override {
    os << "  store ";
    value->Dump(os);
    os << ", %" << addr_id << "\n";
  }
};
```

**示例：** `store 1, %0`（把常量 1 存到地址 %0）

---

### LoadInst — 加载指令

```cpp
class LoadInst : public Instruction {
  int result_id;
  int addr_id;
 public:
  LoadInst(int id, int a) : result_id(id), addr_id(a) {}
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = load %" << addr_id << "\n";
  }
};
```

**示例：** `%3 = load %0`（从地址 %0 加载值到 %3）

---

### BinaryOpInst — 二元运算指令

```cpp
class BinaryOpInst : public Instruction {
  int result_id;
  char op;                                     // 运算符
  std::unique_ptr<KoopaValue> lhs, rhs;       // 左右操作数
 public:
  BinaryOpInst(int id, char o, unique_ptr<KoopaValue> l, unique_ptr<KoopaValue> r)
      : result_id(id), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};
```

支持的运算符：

| 字符 | 含义 |
|------|------|
| `+` | 加法 (add) |
| `-` | 减法 (sub) |
| `*` | 乘法 (mul) |
| `/` | 除法 (div) |
| `%` | 取模 (mod) |
| `<` | 小于 (lt) |
| `>` | 大于 (gt) |
| `L` | 小于等于 (le) |
| `G` | 大于等于 (ge) |
| `E` | 等于 (eq) |
| `N` | 不等于 (ne) |
| `&` | 逻辑与 (and) |
| `\|` | 逻辑或 (or) |

**示例：** `%6 = mul %5, 2`

---

### RetInst — 返回指令

```cpp
class RetInst : public Instruction {
  std::unique_ptr<KoopaValue> value;  // 返回值（可为空）
 public:
  RetInst(std::unique_ptr<KoopaValue> v) : value(std::move(v)) {}
  void Dump(std::ostream &os) const override {
    os << "  ret ";
    if (value) value->Dump(os);
    else os << "0";
    os << "\n";
  }
};
```

**示例：** `ret %9` 或 `ret 0`

---

### BranchInst — 条件分支指令

```cpp
class BranchInst : public Instruction {
  std::unique_ptr<KoopaValue> cond;       // 条件
  std::string true_label, false_label;   // 目标基本块
 public:
  BranchInst(std::unique_ptr<KoopaValue> c, const std::string &t, const std::string &f)
      : cond(std::move(c)), true_label(t), false_label(f) {}
  void Dump(std::ostream &os) const override {
    os << "  br ";
    cond->Dump(os);
    os << ", %" << true_label << ", %" << false_label << "\n";
  }
};
```

**示例：** `br %4, %w8, %w9`（如果 %4 为真，跳到 %w8；否则跳到 %w9）

---

### JumpInst — 无条件跳转指令

```cpp
class JumpInst : public Instruction {
  std::string target_label;
 public:
  JumpInst(const std::string &t) : target_label(t) {}
  const std::string& GetTarget() const { return target_label; }
  void Dump(std::ostream &os) const override {
    os << "  jump %" << target_label << "\n";
  }
};
```

**示例：** `jump %w7`

---

## 基本块与函数类

### BasicBlock — 基本块

```cpp
class BasicBlock {
  std::string name;
  std::vector<std::unique_ptr<Instruction>> insts;
  bool is_protected = false;
 public:
  BasicBlock(const std::string &n) : name(n) {}
  void AddInst(std::unique_ptr<Instruction> inst);         // 添加指令
  bool HasTerminator() const;                               // 是否有终结指令
  bool IsEmpty() const;                                     // 是否为空
  void SetProtected(bool v);                                // 设置受保护
  void Dump(std::ostream &os) const;                       // 打印块内容
};
```

**终结指令（Terminator）：** `RetInst`、`JumpInst`、`BranchInst`。一旦基本块有了终结指令，后面不能再添加指令。

**HasTerminator()** 检测方法：
```cpp
bool HasTerminator() const {
  if (insts.empty()) return false;
  auto last = insts.back().get();
  return dynamic_cast<RetInst*>(last) ||
         dynamic_cast<JumpInst*>(last) ||
         dynamic_cast<BranchInst*>(last);
}
```

---

### Function — 函数

```cpp
class Function {
  std::string name;
  std::string ret_type;
  std::vector<std::unique_ptr<BasicBlock>> blocks;
 public:
  Function(const std::string &n, const std::string &rt);
  void AddBlock(std::unique_ptr<BasicBlock> block);         // 添加基本块
  BasicBlock* CreateBlock(const std::string &name);        // 创建并添加基本块
  size_t GetBlockCount() const;                             // 获取块数量
  BasicBlock* GetBlock(size_t index);                       // 按索引获取块
  void Dump(std::ostream &os) const;                       // 打印函数
};
```

---

### Program — 程序

```cpp
class Program {
  std::vector<std::unique_ptr<Function>> funcs;
 public:
  void AddFunc(std::unique_ptr<Function> func);
  void Dump(std::ostream &os) const;                        // 打印整个程序
  std::string ToString() const;                             // 转字符串
};
```
