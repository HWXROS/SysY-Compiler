# main.cpp — 编译器主程序

## 文件作用

**main.cpp** 是整个编译器的**入口文件**，负责串联各个编译阶段、处理命令行参数、管理文件输入输出，以及调用 lexer/parser 生成 AST。

---

## 整体流程

```
源代码 (.c 文件)
  │
  ▼
yyparse()  ← 调用 Bison/Flex 生成的 parser，生成 AST
  │
  ▼
CompUnitAST::GenIR()  ← AST → 自定义 Koopa IR
  │
  ▼
  ┌─ mode == "-koopa" ──────────────────────┐
  │  program->Dump(out_stream)               │  → 输出 .koopa 文件（Koopa IR 文本）
  │
  └─ mode == "-riscv" ───────────────────────┐
     │  program->ToString()                  │  → IR 转字符串
     │  koopa_parse_from_string()            │  → Koopa 库解析 IR 字符串
     │  koopa_build_raw_program()            │  → 构建 Raw IR
     │  RiscVGenerator::Generate()           │  → 生成 RISC-V 汇编
     ▼                                        │
     out_file                                 │  → 输出 .s 文件（RISC-V 汇编）
```

---

## 重要代码解析

### 命令行参数解析

```cpp
int main(int argc, const char *argv[]) {
  assert(argc == 5);
  auto mode = argv[1];      // "-koopa" 或 "-riscv"
  auto input = argv[2];     // 输入文件路径
  auto output = argv[4];    // 输出文件路径
```

调用方式：
```bash
./compiler -koopa input.c -o output.koopa
./compiler -riscv input.koopa -o output.s
```

### 词法/语法分析

```cpp
yyin = fopen(input, "r");    // 打开输入文件作为 yyparse 的输入
assert(yyin);

unique_ptr<BaseAST> ast;
auto ret = yyparse(ast);      // 调用 Bison/Flex 生成的 parser
assert(!ret);                 // 解析成功则 ret == 0
```

`yyparse()` 是 Flex/Bison 生成的词法+语法分析函数，它会调用 `yylex()` 逐个读取 token，并按 grammar 规约构建 AST。

### AST → IR 生成

```cpp
auto comp_unit = static_cast<CompUnitAST*>(ast.get());
auto program = comp_unit->GenIR();  // 调用 AST 的 GenIR 方法
```

这里开始真正的代码生成。从根节点 `CompUnitAST` 出发，递归调用每个子节点的 `GenIR()`，最终生成完整的 IR 表示。

### Koopa 模式（输出 IR 文本）

```cpp
if (string(mode) == "-koopa") {
  program->Dump(out_stream);   // Dump 方法将 IR 结构序列化为文本
}
```

输出即 `out.koopa` 中的 Koopa IR 文本。

### RISC-V 模式（输出汇编）

```cpp
else if (string(mode) == "-riscv") {
  std::string ir_str = program->ToString();  // IR → 字符串

  koopa_program_t koopa_prog;
  koopa_error_code_t err = koopa_parse_from_string(ir_str.c_str(), &koopa_prog);
  // 解析 Koopa IR 字符串，得到 koopa_program_t

  koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
  koopa_raw_program_t raw = koopa_build_raw_program(builder, koopa_prog);
  // 将解析结果构建为 Raw IR（平面化结构）

  RiscVGenerator generator;
  generator.Generate(raw, out_stream);
  // 生成 RISC-V 汇编

  koopa_delete_raw_program_builder(builder);
  koopa_delete_program(koopa_prog);
}
```

**为什么要用 Koopa 库？**
- 自定义的 `program->Dump()` 输出的只是**文本格式**的 Koopa IR
- `RiscVGenerator` 需要的是**结构化的 Raw IR**（由 Koopa 库提供）
- 因此需要先 `koopa_parse_from_string()` 将文本解析回 Koopa 库的结构
- 再 `koopa_build_raw_program()` 转换为便于后端遍历的 Raw IR

### 文件输出

```cpp
FILE *out_file = fopen(output, "w");
assert(out_file);
std::ostringstream out_stream;
// ... 填充 out_stream ...
fprintf(out_file, "%s", out_stream.str().c_str());
fclose(out_file);
```

所有输出先写入 `ostringstream`，最后一次性写入文件。

---

## 编译器命令行接口

| 模式 | 命令 | 输出 |
|------|------|------|
| AST 打印 | `./compiler input.c` | 打印 AST 到 stdout |
| Koopa IR | `./compiler -koopa input.c -o out.koopa` | 输出 Koopa IR 文本 |
| RISC-V | `./compiler -riscv input.koopa -o out.s` | 输出 RISC-V 汇编 |
