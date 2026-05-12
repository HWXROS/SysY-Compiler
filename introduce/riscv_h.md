# riscv.h — RISC-V 生成器头文件

## 文件作用

**riscv.h** 是 RISC-V 汇编生成器的头文件，定义了 `RiscVGenerator` 类的接口。这是一个**访问者（Visitor）模式**的实现，通过对 Koopa 库提供的 raw IR（平面化后的 Koopa IR）进行深度优先遍历，将每一种 IR 元素转换为对应的 RISC-V 汇编指令。

---

## RiscVGenerator 类

```cpp
class RiscVGenerator {
 public:
  void Generate(const koopa_raw_program_t &program, std::ostream &os);  // 主入口

 private:
  std::ostream *os_;  // 输出流指针

  // 遍历访问方法（重载）
  void Visit(const koopa_raw_slice_t &slice);
  void Visit(const koopa_raw_function_t &func);
  void Visit(const koopa_raw_basic_block_t &bb);
  void Visit(const koopa_raw_value_t &value);
  void Visit(const koopa_raw_return_t &ret);
  void Visit(const koopa_raw_integer_t &integer);
  void Visit(const koopa_raw_binary_t &binary, const koopa_raw_value_t &value);
  void Visit(const koopa_raw_store_t &store);
  void Visit(const koopa_raw_load_t &load, const koopa_raw_value_t &value);
  void VisitAlloc(const koopa_raw_value_t &value);
  void Visit(const koopa_raw_branch_t &branch);
  void Visit(const koopa_raw_jump_t &jump);
};
```

---

## Koopa Raw IR 常见类型

Koopa 库将 Koopa IR 解析后提供一个**平面化的结构**（raw IR），其中包含：

| 类型 | 说明 |
|------|------|
| `koopa_raw_program_t` | 整个程序，包含全局值和函数列表 |
| `koopa_raw_function_t` | 函数定义 |
| `koopa_raw_basic_block_t` | 基本块 |
| `koopa_raw_value_t` | 指令/值 |
| `koopa_raw_slice_t` | 切片（用于表示列表，如函数的基本块列表） |

每个 `koopa_raw_value_t` 有一个 `kind` 字段，标识它的具体类型（如 `KOOPA_RVT_ALLOC`、`KOOPA_RVT_BINARY` 等），通过 `kind.tag` 判断。

---

## 访问者模式

RISC-V 生成器采用**访问者模式**：

1. 从 `program` 开始，深度优先遍历所有函数 → 基本块 → 指令
2. 每个 Visit 重载负责生成特定指令的 RISC-V 汇编
3. 通过 `koopa_raw_slice_t` 的 `kind` 字段区分元素类型
4. `koopa_raw_value_t` 的 `kind.tag` 区分指令类型

---

## Generate 调用流程

```
main.cpp
  └── RiscVGenerator::Generate(raw_program, out_stream)
        ├── CollectReferences()         // 预扫描：收集被多次引用的值
        ├── Visit(program.values)        // 访问全局值
        └── Visit(program.funcs)        // 访问所有函数
              └── Visit(func)
                    └── Visit(func->bbs)  // 访问所有基本块
                          └── Visit(bb->insts)  // 访问所有指令
                                └── Visit(value)  // 分发到具体指令处理
```
