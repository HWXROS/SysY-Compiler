该仓库中存放了一个基于北大 <https://pku-minic.github.io/online-doc/#/lv1-main/教程编写的> SysY 编译器项目

<br />

测试命令：

```
compiler -koopa 输入文件.c -o 输出文件.koopa
```
```
compiler -riscv 输入文件.koopa -o 输出文件.s
```

自动测试 Koopa IR:

```
autotest -koopa -s lv6 /root/compiler
```

自动测试 RISC-V 汇编:

```
 autotest -riscv -s lv6 /root/compiler
```

