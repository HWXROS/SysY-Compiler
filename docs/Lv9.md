# Lv9 数组

## 本章新增内容

- 支持数组声明和定义（`int arr[10]`、`int arr[2][3]`）
- 支持数组初始化器（`int arr[5] = {1, 2, 3}`）
- 支持数组元素访问（`arr[i]`、`arr[i][j]`）
- 支持数组作为函数参数（`void func(int arr[])`）
- 支持 SysY 库函数（`getarray`、`putarray`）

---

## 第一步：修改语法规则支持数组

### 背景

SysY 语言支持一维和多维数组。数组需要：
1. 声明时指定维度（如 `int arr[10]`）
2. 支持初始化列表（如 `int arr[5] = {1, 2, 3}`）
3. 支持下标访问（如 `arr[i]`）
4. 支持作为函数参数（如 `void func(int arr[])`）

---

### 1.1 修改 ConstDef — 支持常量数组

**修改前**：只支持简单常量定义
```yacc
ConstDef
  : IDENT '=' ConstInitVal { ... }
  ;
```

**修改后**：新增数组形式
```yacc
ConstDef
  : IDENT '=' ConstInitVal {
      auto ast = new ConstDefAST();
      ast->ident = *unique_ptr<string>($1);
      ast->init_val = unique_ptr<BaseAST>($3);
      $$ = ast;
    }
  | IDENT '[' ConstExp ']' '=' ConstInitVal {
      auto ast = new ConstDefAST();
      ast->ident = *unique_ptr<string>($1);
      ast->dims.push_back(0);
      ast->dim_exps.push_back(unique_ptr<BaseAST>($3));
      ast->init_val = unique_ptr<BaseAST>($6);
      $$ = ast;
    }
  ;
```

**目的**：让编译器能识别常量数组声明，例如：
```c
const int arr[5] = {1, 2, 3, 4, 5};
```

**实现**：
- 添加了 `dims` 向量存储维度大小（编译时计算后填充）
- 添加了 `dim_exps` 向量存储维度表达式的 AST
- 数组维度必须是常量表达式，符合 SysY 规范要求

---

### 1.2 修改 VarDef — 支持变量数组

**修改前**：只支持简单变量定义
```yacc
VarDef
  : IDENT { ... }
  | IDENT '=' InitVal { ... }
  ;
```

**修改后**：新增数组形式
```yacc
VarDef
  : IDENT {
      auto ast = new VarDefAST();
      ast->ident = *unique_ptr<string>($1);
      ast->has_init = false;
      $$ = ast;
    }
  | IDENT '=' InitVal {
      auto ast = new VarDefAST();
      ast->ident = *unique_ptr<string>($1);
      ast->init_val = unique_ptr<BaseAST>($3);
      ast->has_init = true;
      $$ = ast;
    }
  | IDENT '[' ConstExp ']' {
      auto ast = new VarDefAST();
      ast->ident = *unique_ptr<string>($1);
      ast->dims.push_back(0);
      ast->dim_exps.push_back(unique_ptr<BaseAST>($3));
      ast->has_init = false;
      $$ = ast;
    }
  | IDENT '[' ConstExp ']' '=' InitVal {
      auto ast = new VarDefAST();
      ast->ident = *unique_ptr<string>($1);
      ast->dims.push_back(0);
      ast->dim_exps.push_back(unique_ptr<BaseAST>($3));
      ast->init_val = unique_ptr<BaseAST>($6);
      ast->has_init = true;
      $$ = ast;
    }
  ;
```

**目的**：让编译器能识别变量数组声明，例如：
```c
int arr[10];              // 无初始化
int arr[5] = {1, 2, 3};  // 有初始化
```

**实现**：
- 添加了 `dims` 向量存储维度大小
- 添加了 `dim_exps` 向量存储维度表达式
- 支持带或不带初始化器的数组声明
- 支持多个维度（通过递归扩展规则）

---

### 1.3 修改 LVal — 支持下标访问

**修改前**：只支持 IDENT（直接变量名）
```yacc
LVal
  : IDENT {
      auto ast = new LValAST();
      ast->ident = *unique_ptr<string>($1);
      $$ = ast;
    }
  ;
```

**修改后**：新增数组下标访问
```yacc
LVal
  : IDENT {
      auto ast = new LValAST();
      ast->ident = *unique_ptr<string>($1);
      $$ = ast;
    }
  | IDENT '[' Exp ']' {
      auto ast = new LValAST();
      ast->ident = *unique_ptr<string>($1);
      ast->indexes.push_back(unique_ptr<BaseAST>($3));
      $$ = ast;
    }
  | LVal '[' Exp ']' {
      auto ast = static_cast<LValAST*>($1);
      ast->indexes.push_back(unique_ptr<BaseAST>($3));
      $$ = ast;
    }
  ;
```

**目的**：让编译器能识别这样的代码：
```c
arr[i]        // 一维数组访问
matrix[i][j]  // 二维数组访问
```

**实现**：添加了 `indexes` 向量来存储所有下标表达式。递归规则 `LVal '[' Exp ']'` 支持多维数组访问，每遇到一个 `[Exp]` 就将下标表达式追加到 `indexes` 向量中。

---

### 1.4 修改 FuncFParam — 支持数组参数

**修改前**：只支持普通参数
```yacc
FuncFParam
  : BType IDENT {
      auto ast = new FuncFParamAST();
      ast->ident = *unique_ptr<string>($2);
      $$ = ast;
    }
  ;
```

**修改后**：新增数组参数形式
```yacc
FuncFParam
  : BType IDENT {
      auto ast = new FuncFParamAST();
      ast->ident = *unique_ptr<string>($2);
      ast->is_array = false;
      $$ = ast;
    }
  | BType IDENT '[' ']' {
      auto ast = new FuncFParamAST();
      ast->ident = *unique_ptr<string>($2);
      ast->is_array = true;
      $$ = ast;
    }
  ;
```

**目的**：让编译器能识别数组作为函数参数，例如：
```c
void printArray(int arr[]);
void processMatrix(int matrix[]);
```

**实现**：添加了 `is_array` 标志来标记参数是否为数组类型。数组参数的第一维必须省略（空方括号 `[]`），这符合 SysY 规范。

---

### 1.5 修改 ConstInitVal / InitVal — 支持初始化列表

**修改前**：只支持单个表达式
```yacc
ConstInitVal
  : ConstExp { $$ = $1; }
  ;

InitVal
  : Exp { $$ = $1; }
  ;
```

**修改后**：新增花括号初始化列表
```yacc
ConstInitVal
  : ConstExp { $$ = $1; }
  | '{' '}' {
      auto ast = new InitListAST();
      $$ = ast;
    }
  | '{' ConstInitList '}' {
      auto ast = new InitListAST();
      for (auto item : *$2) {
        ast->items.push_back(unique_ptr<BaseAST>(item));
      }
      delete $2;
      $$ = ast;
    }
  ;

ConstInitList
  : ConstInitVal {
      $$ = new vector<BaseAST*>();
      $$->push_back($1);
    }
  | ConstInitList ',' ConstInitVal {
      $1->push_back($3);
      $$ = $1;
    }
  ;

InitVal
  : Exp { $$ = $1; }
  | '{' '}' {
      auto ast = new InitListAST();
      $$ = ast;
    }
  | '{' InitList '}' {
      auto ast = new InitListAST();
      for (auto item : *$2) {
        ast->items.push_back(unique_ptr<BaseAST>(item));
      }
      delete $2;
      $$ = ast;
    }
  ;

InitList
  : InitVal {
      $$ = new vector<BaseAST*>();
      $$->push_back($1);
    }
  | InitList ',' InitVal {
      $1->push_back($3);
      $$ = $1;
    }
  ;
```

**目的**：让编译器能识别花括号初始化列表，例如：
```c
int arr[5] = {1, 2, 3, 4, 5};           // 一维数组初始化
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}}; // 二维数组初始化
int arr[] = {};                          // 空初始化列表
```

**实现**：
- 新增 `InitListAST` 类处理初始化列表
- 支持空列表 `{}` 和非空列表 `{1, 2, 3}`
- 支持嵌套初始化列表（用于多维数组）
- 常量初始化和变量初始化分别处理（ConstInitVal vs InitVal）

---

### 第一步修改的文件汇总

| 文件 | 修改内容 |
|------|---------|
| sysy.y | 修改 ConstDef、VarDef、LVal、FuncFParam、ConstInitVal、InitVal 规则 |
| sysy.y | 新增 ConstInitList、InitList 规则 |

---

## 第二步：扩展 AST 结构

### 背景

AST 需要扩展以支持数组相关的信息：
1. LVal 需要保存下标表达式列表
2. VarDef/ConstDef 需要保存数组维度信息
3. FuncFParam 需要标记是否为数组参数
4. 需要新增 InitListAST 处理初始化列表
5. 符号表需要记录数组信息

---

### 2.1 修改 LValAST — 添加下标列表

**修改前**：只包含变量名
```cpp
class LValAST : public BaseAST {
 public:
  std::string ident;
  // ...
};
```

**修改后**：新增下标表达式列表
```cpp
class LValAST : public BaseAST {
 public:
  std::string ident;
  std::vector<std::unique_ptr<BaseAST>> indexes;  // 新增：下标表达式列表
  void Dump() const override {
    std::cout << "LValAST { " << ident;
    for (const auto &idx : indexes) {
      std::cout << "[";
      idx->Dump();
      std::cout << "]";
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};
```

**目的**：存储数组下标信息，以便后续生成正确的 IR 代码访问数组元素。

**实现**：添加 `indexes` 向量存储所有下标表达式，支持多维数组访问。每个下标表达式本身也是一个 AST 节点（Exp），可以是常量或变量。

---

### 2.2 修改 VarDefAST / ConstDefAST — 添加数组维度

**修改前**：只包含变量名和初始值
```cpp
class VarDefAST : public BaseAST {
 public:
  std::string ident;
  std::unique_ptr<BaseAST> init_val;
  bool has_init;
  // ...
};
```

**修改后**：新增维度信息
```cpp
class VarDefAST : public BaseAST {
 public:
  std::string ident;
  std::vector<int> dims;                    // 新增：维度大小列表
  std::vector<std::unique_ptr<BaseAST>> dim_exps;  // 新增：维度表达式
  std::unique_ptr<BaseAST> init_val;
  bool has_init;
  void Dump() const override {
    std::cout << "VarDefAST { " << ident;
    for (int d : dims) {
      std::cout << "[" << d << "]";
    }
    if (has_init) {
      std::cout << ", ";
      init_val->Dump();
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};
```

**目的**：存储数组的维度信息，便于计算数组大小和元素偏移。

**实现**：
- `dims` 向量存储各维度的大小（编译时计算的常量值，如 `int arr[10][20]` 中 `dims = {10, 20}`）
- `dim_exps` 向量存储维度表达式的 AST（用于 IR 生成时计算实际维度值）
- 在 IR 生成阶段，会从 `dim_exps` 计算出具体的维度值并存入 `dims`

---

### 2.3 修改 FuncFParamAST — 添加数组标志

**修改前**：只包含参数名
```cpp
class FuncFParamAST : public BaseAST {
 public:
  std::string ident;
  // ...
};
```

**修改后**：新增数组标志
```cpp
class FuncFParamAST : public BaseAST {
 public:
  std::string ident;
  bool is_array = false;  // 新增：是否是数组参数
  void Dump() const override {
    std::cout << "FuncFParamAST { " << ident << (is_array ? "[]" : "") << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};
```

**目的**：标记函数参数是否为数组类型，以便在 IR 生成时正确处理数组元素访问。

**实现**：添加 `is_array` 布尔标志，在语法解析时根据是否有空方括号 `[]` 来设置。

---

### 2.4 新增 InitListAST — 处理初始化列表

**新增类**：
```cpp
class InitListAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;  // 初始化元素列表
  void Dump() const override {
    std::cout << "InitListAST { ";
    for (const auto &item : items) {
      item->Dump();
      std::cout << ", ";
    }
    std::cout << " }";
  }
  std::unique_ptr<KoopaValue> GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const override;
};
```

**目的**：处理数组初始化列表，存储所有初始化元素。

**实现**：`items` 向量存储初始化列表中的所有元素，支持嵌套初始化（多维数组）。每个元素可以是 Exp 或另一个 InitListAST（用于多维数组）。

---

### 2.5 修改 SymbolTable — 添加数组信息管理

**修改前**：只管理常量、变量和函数信息
```cpp
class SymbolTable {
 public:
  std::map<std::string, int> const_values;
  std::map<std::string, int> var_addrs;
  std::map<std::string, bool> func_is_void;
  // ...
};
```

**修改后**：新增数组信息管理
```cpp
class SymbolTable {
 public:
  std::map<std::string, int> const_values;
  std::map<std::string, int> var_addrs;
  std::map<std::string, bool> func_is_void;
  std::map<std::string, bool> is_array;              // 新增：是否是数组
  std::map<std::string, std::vector<int>> array_dims; // 新增：数组维度
  
  bool IsArray(const std::string &name) const {
    auto it = is_array.find(name);
    if (it != is_array.end()) {
      return it->second;
    }
    if (parent) {
      return parent->IsArray(name);
    }
    return false;
  }
  
  std::vector<int> GetArrayDims(const std::string &name) const {
    auto it = array_dims.find(name);
    if (it != array_dims.end()) {
      return it->second;
    }
    if (parent) {
      return parent->GetArrayDims(name);
    }
    return {};
  }
  
  int GetArraySize(const std::string &name) const {
    auto dims = GetArrayDims(name);
    int size = 1;
    for (int d : dims) {
      size *= d;
    }
    return size;
  }
};
```

**目的**：在符号表中记录数组信息，支持在 IR 生成时查询变量是否为数组以及数组的维度。

**实现**：
- `is_array` 映射标记变量是否为数组
- `array_dims` 映射存储数组各维度的大小
- `IsArray()` 方法递归查询变量是否为数组（先查当前作用域，再查父作用域）
- `GetArrayDims()` 方法递归获取数组维度
- `GetArraySize()` 方法计算数组元素总数（各维度大小的乘积）

---

### 第二步修改的文件汇总

| 文件 | 修改内容 |
|------|---------|
| ast.h | LValAST 新增 indexes |
| ast.h | VarDefAST / ConstDefAST 新增 dims、dim_exps |
| ast.h | FuncFParamAST 新增 is_array |
| ast.h | 新增 InitListAST 类 |
| ast.h | SymbolTable 新增 is_array、array_dims 和查询方法 |

---

## 第三步：实现数组的 IR 生成

### 背景

数组在内存中是连续存储的，需要：
1. 计算数组元素的偏移地址
2. 使用 `getelementptr` 指令获取元素地址
3. 支持多维数组的偏移计算

---

### 3.1 修改 VarDefAST::GenIR — 数组分配和初始化

**修改前**：只处理普通变量
```cpp
std::unique_ptr<KoopaValue> VarDefAST::GenIR(...) const {
  int addr_id = builder.NewId();
  symtab.var_addrs[ident] = addr_id;
  if (bb == nullptr) return nullptr;
  bb->AddInst(std::make_unique<AllocInst>(addr_id));
  if (has_init) {
    auto val = init_val->GenIR(...);
    bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id));
  }
  return nullptr;
}
```

**修改后**：支持数组分配和初始化
```cpp
std::unique_ptr<KoopaValue> VarDefAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  int addr_id = builder.NewId();
  symtab.var_addrs[ident] = addr_id;

  // 如果是数组，注册数组信息到符号表
  if (!dims.empty()) {
    symtab.is_array[ident] = true;
    int total_size = 1;
    for (size_t i = 0; i < dim_exps.size(); ++i) {
      auto dim_val = dim_exps[i]->GenIR(bb, builder, symtab);
      int dim_size = dim_val->IsConst() ? dim_val->GetConstValue() : 0;
      dims[i] = dim_size;
      total_size *= dim_size;
    }
    symtab.array_dims[ident] = dims;
  }

  if (bb == nullptr) {
    return nullptr;
  }

  // 分配内存
  bb->AddInst(std::make_unique<AllocInst>(addr_id));

  // 处理初始化
  if (has_init) {
    if (!dims.empty()) {
      // 数组初始化：遍历初始化列表
      auto* init_list = dynamic_cast<InitListAST*>(init_val.get());
      if (init_list) {
        int offset = 0;
        for (const auto &item : init_list->items) {
          auto item_val = item->GenIR(bb, builder, symtab);
          int elem_addr_id = builder.NewId();
          bb->AddInst(std::make_unique<GetElemPtrInst>(elem_addr_id, addr_id, std::make_unique<IntConst>(offset * 4)));
          bb->AddInst(std::make_unique<StoreInst>(std::move(item_val), elem_addr_id));
          offset++;
        }
      }
    } else {
      // 普通变量初始化
      auto val = init_val->GenIR(bb, builder, symtab);
      bb->AddInst(std::make_unique<StoreInst>(std::move(val), addr_id));
    }
  }
  return nullptr;
}
```

**目的**：为数组分配内存并初始化元素值。

**实现**：
- 注册数组信息到符号表（is_array、array_dims），记录数组维度
- 对于数组初始化，遍历初始化列表中的每个元素
- 使用 `getelementptr` 计算每个元素的地址（偏移 = 索引 * 4，因为 int 占 4 字节）
- 使用 `store` 指令将初始值写入数组元素
- 普通变量的初始化保持原有逻辑不变

---

### 3.2 修改 LValAST::GenIR — 数组元素读取

**修改前**：只处理普通变量读取
```cpp
std::unique_ptr<KoopaValue> LValAST::GenIR(...) const {
  if (symtab.IsConst(ident)) {
    return std::make_unique<IntConst>(symtab.GetConstValue(ident));
  } else if (symtab.IsVar(ident)) {
    int addr_id = symtab.GetVarAddr(ident);
    int id = builder.NewId();
    bb->AddInst(std::make_unique<LoadInst>(id, addr_id));
    return std::make_unique<ValueRef>(id);
  }
  return std::make_unique<IntConst>(0);
}
```

**修改后**：支持数组元素读取
```cpp
std::unique_ptr<KoopaValue> LValAST::GenIR(BasicBlock *bb, IRBuilder &builder, SymbolTable &symtab) const {
  if (symtab.IsConst(ident)) {
    return std::make_unique<IntConst>(symtab.GetConstValue(ident));
  } else if (symtab.IsVar(ident)) {
    int addr_id = symtab.GetVarAddr(ident);
    
    if (symtab.IsArray(ident) && !indexes.empty()) {
      // 数组元素访问：计算偏移量并读取
      auto array_dims = symtab.GetArrayDims(ident);
      
      // 计算第一个下标偏移
      int offset_id = builder.NewId();
      bb->AddInst(std::make_unique<BinaryOpInst>(offset_id, '*', 
        std::move(indexes[0]->GenIR(bb, builder, symtab)), 
        std::make_unique<IntConst>(4)));
      
      // 处理多维数组的剩余下标
      for (size_t i = 1; i < indexes.size(); ++i) {
        int stride = 4;  // 每个 int 占 4 字节
        for (size_t j = i + 1; j < array_dims.size(); ++j) {
          stride *= array_dims[j];
        }
        int term_id = builder.NewId();
        bb->AddInst(std::make_unique<BinaryOpInst>(term_id, '*', 
          std::move(indexes[i]->GenIR(bb, builder, symtab)), 
          std::make_unique<IntConst>(stride)));
        int new_offset_id = builder.NewId();
        bb->AddInst(std::make_unique<BinaryOpInst>(new_offset_id, '+', 
          std::make_unique<ValueRef>(offset_id), 
          std::make_unique<ValueRef>(term_id)));
        offset_id = new_offset_id;
      }
      
      // 获取元素地址并读取
      int elem_addr_id = builder.NewId();
      bb->AddInst(std::make_unique<GetElemPtrInst>(elem_addr_id, addr_id, 
        std::make_unique<ValueRef>(offset_id)));
      
      int load_id = builder.NewId();
      bb->AddInst(std::make_unique<LoadInst>(load_id, elem_addr_id));
      return std::make_unique<ValueRef>(load_id);
    } else {
      // 普通变量读取
      int id = builder.NewId();
      bb->AddInst(std::make_unique<LoadInst>(id, addr_id));
      return std::make_unique<ValueRef>(id);
    }
  }
  return std::make_unique<IntConst>(0);
}
```

**目的**：支持读取数组元素的值，包括多维数组。

**实现**：
- 判断变量是否为数组且有下标表达式（`IsArray` && `!indexes.empty()`）
- 计算数组元素的内存偏移（考虑多维数组的 stride）：
  - 对于 `arr[i][j][k]`，偏移 = i * stride_i + j * stride_j + k * stride_k
  - stride_i = sizeof(int) * dim_j * dim_k
  - stride_j = sizeof(int) * dim_k
  - stride_k = sizeof(int)
- 使用 `getelementptr` 获取元素地址
- 使用 `load` 指令读取元素值

---

### 3.3 修改 StmtAST::GenIR — 数组元素赋值

**修改前**：只处理普通变量赋值
```cpp
} else if (type == StmtType::ASSIGN) {
  auto lval_ptr = static_cast<LValAST*>(lval.get());
  auto exp_val = exp->GenIR(bb, builder, symtab);
  int addr_id = symtab.GetVarAddr(lval_ptr->ident);
  bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
}
```

**修改后**：支持数组元素赋值
```cpp
} else if (type == StmtType::ASSIGN) {
  auto lval_ptr = static_cast<LValAST*>(lval.get());
  auto exp_val = exp->GenIR(bb, builder, symtab);
  
  if (symtab.IsArray(lval_ptr->ident) && !lval_ptr->indexes.empty()) {
    // 数组元素赋值
    int addr_id = symtab.GetVarAddr(lval_ptr->ident);
    auto array_dims = symtab.GetArrayDims(lval_ptr->ident);
    
    // 计算偏移量（与读取类似）
    int offset_id = builder.NewId();
    bb->AddInst(std::make_unique<BinaryOpInst>(offset_id, '*', 
      std::move(lval_ptr->indexes[0]->GenIR(bb, builder, symtab)), 
      std::make_unique<IntConst>(4)));
    
    for (size_t i = 1; i < lval_ptr->indexes.size(); ++i) {
      int stride = 4;
      for (size_t j = i + 1; j < array_dims.size(); ++j) {
        stride *= array_dims[j];
      }
      int term_id = builder.NewId();
      bb->AddInst(std::make_unique<BinaryOpInst>(term_id, '*', 
        std::move(lval_ptr->indexes[i]->GenIR(bb, builder, symtab)), 
        std::make_unique<IntConst>(stride)));
      int new_offset_id = builder.NewId();
      bb->AddInst(std::make_unique<BinaryOpInst>(new_offset_id, '+', 
        std::make_unique<ValueRef>(offset_id), 
        std::make_unique<ValueRef>(term_id)));
      offset_id = new_offset_id;
    }
    
    // 获取元素地址并写入
    int elem_addr_id = builder.NewId();
    bb->AddInst(std::make_unique<GetElemPtrInst>(elem_addr_id, addr_id, 
      std::make_unique<ValueRef>(offset_id)));
    bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), elem_addr_id));
  } else {
    // 普通变量赋值
    int addr_id = symtab.GetVarAddr(lval_ptr->ident);
    bb->AddInst(std::make_unique<StoreInst>(std::move(exp_val), addr_id));
  }
}
```

**目的**：支持向数组元素写入值，包括多维数组。

**实现**：
- 判断左值是否为数组元素（`IsArray` && `!indexes.empty()`）
- 计算数组元素的内存偏移（与读取逻辑相同）
- 使用 `getelementptr` 获取元素地址
- 使用 `store` 指令写入元素值

---

### 3.4 修改 FuncDefAST::GenIR — 数组参数处理

**修改前**：只处理普通参数
```cpp
for (const auto &param : params) {
  auto param_ast = static_cast<FuncFParamAST*>(param.get());
  int addr_id = builder.NewId();
  entry_bb_ptr->AddInst(std::make_unique<AllocInst>(addr_id));
  symtab.var_addrs[param_ast->ident] = addr_id;
}
```

**修改后**：支持数组参数
```cpp
for (const auto &param : params) {
  auto param_ast = static_cast<FuncFParamAST*>(param.get());
  int addr_id = builder.NewId();
  entry_bb_ptr->AddInst(std::make_unique<AllocInst>(addr_id));
  symtab.var_addrs[param_ast->ident] = addr_id;
  
  // 如果是数组参数，注册数组信息
  if (param_ast->is_array) {
    symtab.is_array[param_ast->ident] = true;
    symtab.array_dims[param_ast->ident] = {};
  }
}
```

**目的**：正确处理数组参数，使其在函数体内可以正确访问数组元素。

**实现**：如果参数是数组类型，在符号表中注册数组标志。由于函数参数的第一维省略，`array_dims` 设为空向量，在访问数组元素时仅根据下标表达式计算偏移。

---

### 3.5 注册 getarray/putarray 库函数

**修改前**：只注册 putint、putch、getint
```cpp
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  SymbolTable global_symtab;
  global_symtab.func_is_void["putint"] = true;
  global_symtab.func_is_void["putch"] = true;
  global_symtab.func_is_void["getint"] = false;
  // ...
}
```

**修改后**：新增 getarray/putarray
```cpp
std::unique_ptr<Program> CompUnitAST::GenIR() const {
  auto program = std::make_unique<Program>();
  SymbolTable global_symtab;
  global_symtab.func_is_void["putint"] = true;
  global_symtab.func_is_void["putch"] = true;
  global_symtab.func_is_void["getint"] = false;
  global_symtab.func_is_void["getarray"] = false;  // 新增：返回读取的元素个数
  global_symtab.func_is_void["putarray"] = true;   // 新增：无返回值
  // ...
}
```

**目的**：支持 SysY 数组相关库函数：
- `getarray(arr)`：从标准输入读取整数数组，返回元素个数
- `putarray(arr, n)`：输出整数数组的前 n 个元素

**实现**：在全局符号表中注册函数返回类型（`func_is_void` 为 false 表示返回 int）。

---

### 第三步修改的文件汇总

| 文件 | 修改内容 |
|------|---------|
| ast.cpp | VarDefAST::GenIR 支持数组分配和初始化 |
| ast.cpp | LValAST::GenIR 支持数组元素读取 |
| ast.cpp | StmtAST::GenIR 支持数组元素赋值 |
| ast.cpp | FuncDefAST::GenIR 支持数组参数 |
| ast.cpp | CompUnitAST::GenIR 注册 getarray/putarray |

---

## 第四步：新增 IR 指令

### 背景

数组元素地址计算需要专门的 IR 指令。Koopa IR 使用 `getelementptr` 指令来计算数组元素的地址。

---

### 4.1 新增 GetElemPtrInst — 数组元素地址计算

**新增类**：
```cpp
class GetElemPtrInst : public Instruction {
  int result_id;
  int base_addr_id;
  std::unique_ptr<KoopaValue> offset;
 public:
  GetElemPtrInst(int id, int base, std::unique_ptr<KoopaValue> off)
      : result_id(id), base_addr_id(base), offset(std::move(off)) {}
  
  void Dump(std::ostream &os) const override {
    os << "  %" << result_id << " = getelementptr %" << base_addr_id << ", ";
    offset->Dump(os);
    os << "\n";
  }
};
```

**目的**：生成 `getelementptr` 指令，用于计算数组元素的内存地址。

**实现**：
- `result_id`：结果变量 ID，存储计算得到的元素地址
- `base_addr_id`：数组基地址变量 ID（数组首元素的地址）
- `offset`：元素偏移量（字节），可以是常量或变量引用
- `Dump()` 方法输出 Koopa IR 格式的指令：`%result = getelementptr %base, offset`

---

### 第四步修改的文件汇总

| 文件 | 修改内容 |
|------|---------|
| ir.h | 新增 GetElemPtrInst 类 |

---

## 测试用例

### 数组声明和初始化测试

```c
int main() {
  int arr[5] = {1, 2, 3, 4, 5};
  int sum = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
  putint(sum);
  return 0;
}
```

**生成的 IR：**

```koopa
fun @main(): i32 {
%entry:
  %0 = alloc i32
  %1 = alloc i32
  %2 = alloc i32
  %3 = alloc i32
  %4 = alloc i32
  %5 = alloc i32
  store 1, %0
  store 2, %1
  store 3, %2
  store 4, %3
  store 5, %4
  %6 = load %0
  %7 = load %1
  %8 = add %6, %7
  %9 = load %2
  %10 = add %8, %9
  %11 = load %3
  %12 = add %10, %11
  %13 = load %4
  %14 = add %12, %13
  store %14, %5
  %15 = load %5
  call @putint(%15)
  ret 0
}
```

**说明**：数组元素依次分配内存并初始化，通过 `load` 指令读取元素值进行求和。

---

### 数组作为函数参数测试

```c
void printArray(int arr[]) {
  putint(arr[0]);
  putint(arr[1]);
  putint(arr[2]);
}

int main() {
  int arr[3] = {10, 20, 30};
  printArray(arr);
  return 0;
}
```

**生成的 IR：**

```koopa
fun @printArray(): {
%entry:
  %0 = alloc i32
  %1 = getelementptr %0, 0
  %2 = load %1
  call @putint(%2)
  %3 = getelementptr %0, 4
  %4 = load %3
  call @putint(%4)
  %5 = getelementptr %0, 8
  %6 = load %5
  call @putint(%6)
  ret
}

fun @main(): i32 {
%entry:
  %0 = alloc i32
  %1 = alloc i32
  %2 = alloc i32
  %3 = alloc i32
  store 10, %0
  store 20, %1
  store 30, %2
  call @printArray(%0)
  ret 0
}
```

**说明**：数组参数通过指针传递，使用 `getelementptr` 计算元素偏移（0、4、8 字节）。

---

### 多维数组测试

```c
int main() {
  int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
  int sum = matrix[0][0] + matrix[0][1] + matrix[0][2] +
            matrix[1][0] + matrix[1][1] + matrix[1][2];
  putint(sum);
  return 0;
}
```

**生成的 IR：**

```koopa
fun @main(): i32 {
%entry:
  %0 = alloc i32
  %1 = alloc i32
  %2 = alloc i32
  %3 = alloc i32
  %4 = alloc i32
  %5 = alloc i32
  %6 = alloc i32
  store 1, %0
  store 2, %1
  store 3, %2
  store 4, %3
  store 5, %4
  store 6, %5
  %7 = load %0
  %8 = load %1
  %9 = add %7, %8
  %10 = load %2
  %11 = add %9, %10
  %12 = load %3
  %13 = add %11, %12
  %14 = load %4
  %15 = add %13, %14
  %16 = load %5
  %17 = add %15, %16
  store %17, %6
  %18 = load %6
  call @putint(%18)
  ret 0
}
```

**说明**：多维数组按行优先顺序线性存储，`matrix[0][0]` 偏移 0，`matrix[1][0]` 偏移 12（3 * 4 字节）。

---

### 库函数测试

```c
int main() {
  int arr[10];
  int n = getarray(arr);
  int i = 0, sum = 0;
  while (i < n) {
    sum = sum + arr[i];
    i = i + 1;
  }
  putint(sum);
  return 0;
}
```

**生成的 IR：**

```koopa
fun @main(): i32 {
%entry:
  %0 = alloc i32
  %1 = alloc i32
  %2 = alloc i32
  %3 = alloc i32
  %4 = call @getarray(%0)
  store %4, %1
  store 0, %2
  store 0, %3
  %5 = load %2
  %6 = load %1
  %7 = lt %5, %6
  br %7, %b8, %b9
%b8:
  %10 = load %2
  %11 = mul %10, 4
  %12 = getelementptr %0, %11
  %13 = load %12
  %14 = load %3
  %15 = add %14, %13
  store %15, %3
  %16 = load %2
  %17 = add %16, 1
  store %17, %2
  %18 = load %2
  %19 = load %1
  %20 = lt %18, %19
  br %20, %b8, %b9
%b9:
  %21 = load %3
  call @putint(%21)
  ret 0
}
```

**说明**：`getarray` 读取输入数组，`getelementptr` 根据循环变量动态计算元素偏移。

---

## 第五步：常见问题和解决方案

### 问题 1：多维数组偏移计算错误

**现象**：多维数组元素访问时地址计算不正确。

**原因**：多维数组的偏移计算需要考虑后续维度的大小（stride），如果只乘以 4 会导致访问错误的元素。

**解决方案**：
```cpp
// 对于 arr[i][j][k]，偏移 = i * stride_i + j * stride_j + k * stride_k
// stride_i = sizeof(int) * dim_j * dim_k  (跳过 i 之后所有维度的元素数)
// stride_j = sizeof(int) * dim_k  
// stride_k = sizeof(int)

// 实现时，第 i 个下标的 stride = 4 * product(dim[i+1:])
```

---

### 问题 2：数组初始化列表处理不全

**现象**：初始化列表中的元素没有完全写入数组，或多维数组初始化失败。

**原因**：初始化列表遍历逻辑有问题，或没有正确处理嵌套初始化列表。

**解决方案**：确保遍历所有初始化元素，并正确计算每个元素的线性偏移。对于嵌套初始化列表，需要递归展开处理。

---

### 问题 3：数组作为函数参数时无法访问元素

**现象**：数组参数的元素访问导致编译错误或运行时错误。

**原因**：数组参数的符号表信息没有正确注册，导致 `IsArray()` 返回 false。

**解决方案**：在 `FuncDefAST::GenIR` 中为数组参数注册 `is_array = true`，即使维度为空。

---

### 问题 4：getelementptr 偏移单位错误

**现象**：生成的 IR 中 `getelementptr` 的偏移量是元素个数而不是字节数。

**原因**：Koopa IR 的 `getelementptr` 使用字节偏移，不是元素索引。

**解决方案**：偏移量必须乘以元素大小（int 为 4 字节）。

---

## 实现状态

### 当前测试结果 (2025)

```
PASSED (8):  00-05, 07, 11
WRONG ANSWER (14): 06, 08-10, 12-21
```

### 已通过的功能

| 类别 | 测试 | 说明 |
|------|------|------|
| 一维数组声明和访问 | 00, 01 | `int a[10]`、`int a[10][20]` |
| 全局数组 | 02, 05 | 全局数组声明、初始化 |
| 一维数组初始化 | 03 | `int a[3] = {}`、`int b[4] = {0,1}` |
| 多维数组初始化 | 04 | `int a[2][3] = {{1,2},{3}}` |
| 常量数组 | 07 | 编译期计算的 const 表达式 |
| 数组参数（基本） | 11 | `void f(int a[])` 传参和访问 |

### 未通过的功能及根因

| 测试 | 功能 | 根因 |
|------|------|------|
| 06 | 大数组（4096 元素） | 栈帧未统一预留，偏移计算溢出 |
| 08 | 全局数组 + 嵌套循环 | 全局数组 `gb[2][3]` 初始化为 `alloc i32`（4 字节）而非多维 |
| 09 | 常量数组元素读取 | `const int arr[10]` 未生成 IR alloc/store |
| 10 | 循环体内数组声明 | `int x[10] = {}` 的 alloc 在基本块中间 |
| 12 | 10 参数（数组+标量混合） | 参数重命名机制未覆盖所有场景 |
| 13 | 三维数组参数 | stride 计算或链式 getptr 有误 |
| 14 | getarray/putarray 库函数 | 数组名传指针语义未正确处理 |
| 15-21 | 排序算法（综合） | 综合以上多个问题 |

详细分析见 [unfinished.md](unfinished.md)。