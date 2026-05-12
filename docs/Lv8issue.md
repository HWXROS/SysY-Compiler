# Lv8 函数和全局变量 - 问题记录

## 1. 语法冲突问题

### 问题描述
- **现象**：编译时出现 "syntax is ambiguous" 错误，特别是对于嵌套的 if-else 语句
- **原因**：dangling else 语法歧义，编译器无法确定 else 分支应该与哪个 if 匹配

### 解决方案
- 将 `Stmt` 语法规则分为 `MatchedStmt` 和 `UnmatchedStmt`
- 使用经典的 dangling else 解决方案，确保 else 分支与最近的 if 匹配
- 启用 GLR 解析器来处理剩余的语法冲突

### 具体修改
- **文件**：`src/sysy.y`
- **修改**：添加 `MatchedStmt` 和 `UnmatchedStmt` 规则，启用 `%glr-parser`

## 2. 基本块名称格式问题

### 问题描述
- **现象**：生成的 Koopa IR 中基本块名称使用 `%name =` 格式，导致 Koopa 解析器报错
- **原因**：基本块名称格式不符合 Koopa IR 规范，应该使用 `%name:` 格式

### 解决方案
- 修改 `BasicBlock::Dump` 方法，将基本块名称格式从 `%name =` 改为 `%name:`

### 具体修改
- **文件**：`src/ir.h`
- **修改**：`BasicBlock::Dump` 方法中的输出格式

## 3. 不可达基本块问题

### 问题描述
- **现象**：生成的 Koopa IR 中包含不可达的基本块，导致 Koopa 解析器报错
- **原因**：在 `IfStmtAST::GenIR` 中，即使 then 和 else 分支都有终止符（如 return 语句），仍然创建了 end_bb

### 解决方案
- 在 `IfStmtAST::GenIR` 中添加判断，只有当 then 或 else 分支没有终止符时才创建 end_bb
- 只有当创建了 end_bb 时才将其设置为当前基本块

### 具体修改
- **文件**：`src/ast.cpp`
- **修改**：`IfStmtAST::GenIR` 方法中的逻辑

## 4. 缺少 return 语句问题

### 问题描述
- **现象**：非 void 函数如果没有显式的 return 语句，生成的 IR 会缺少 return 指令，导致 Koopa 解析器报错

### 解决方案
- 在 `FuncDefAST::GenIR` 中添加判断，对于非 void 函数，如果最后一个基本块没有终止符，自动添加 `ret 0` 指令

### 具体修改
- **文件**：`src/ast.cpp`
- **修改**：`FuncDefAST::GenIR` 方法中的逻辑

## 5. RISC-V 汇编生成问题

### 问题描述
- **现象**：生成的 RISC-V 汇编中基本块名称包含 `%` 前缀，导致汇编语法错误

### 解决方案
- 修改 `RiscVGenerator::Visit` 方法，在生成基本块标签和分支指令时去除 `%` 前缀

### 具体修改
- **文件**：`src/riscv.cpp`
- **修改**：`RiscVGenerator::Visit` 方法中的基本块名称处理

## 6. 库函数调用问题

### 问题描述
- **现象**：调用库函数时，编译器无法确定函数的返回类型，导致生成的 IR 格式不正确

### 解决方案
- 在全局符号表中预注册 SysY 库函数的返回类型（is_void）
- 在 `CallExprAST::GenIR` 中使用 `symtab.IsFuncVoid()` 来确定函数的返回类型

### 具体修改
- **文件**：`src/ast.cpp`
- **修改**：`CompUnitAST::GenIR` 方法中预注册库函数

## 7. 字符常量支持问题

### 问题描述
- **现象**：编译包含字符常量（如 `'A'`）的代码时出现语法错误
- **原因**：词法分析器没有正确处理字符常量

### 解决方案
- 在 `sysy.l` 文件中添加字符常量的词法规则
- 处理转义字符（如 `\n`、`\t` 等）

### 具体修改
- **文件**：`src/sysy.l`
- **修改**：添加字符常量的词法规则

## 8. 输入处理问题

### 问题描述
- **现象**：在 `-riscv` 模式下，编译器无法正确处理 C 源文件输入，尝试将其解析为 Koopa IR

### 解决方案
- 在 `main.cpp` 中添加输入类型检测，根据文件内容判断是 C 源文件还是 Koopa IR 文件
- 对于 C 源文件，先解析生成 IR，再生成 RISC-V 汇编
- 对于 Koopa IR 文件，直接解析并生成 RISC-V 汇编

### 具体修改
- **文件**：`src/main.cpp`
- **修改**：`main` 函数中的输入处理逻辑

## 9. 编译错误问题

### 问题描述
- **现象**：编译时出现 "koopa.h file not found" 错误
- **原因**：在 Docker 容器中，编译器无法找到 koopa.h 头文件

### 解决方案
- 修改 `main.cpp`，只在需要时包含 riscv.h 和 koopa.h
- 对于只需要 Koopa IR 生成的情况，不包含这些头文件

### 具体修改
- **文件**：`src/main.cpp`
- **修改**：头文件包含逻辑

## 总结

通过解决以上问题，我们成功实现了第八章的所有功能，包括：

1. CompUnit 支持多声明和多函数
2. 全局变量支持
3. void 函数支持
4. 函数形参支持
5. 函数调用表达式支持
6. SysY 库函数支持
7. 语法冲突修复
8. IR 生成优化
9. RISC-V 汇编生成修复

编译器现在应该能够通过第八章的所有测试用例，包括复杂的函数调用、嵌套的 if-else 语句和库函数调用。