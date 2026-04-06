<https://pku-minic.github.io/online-doc/#/lv0-env-config/docker>

项目编译使用官方提供的docker镜像，挂在共享目录执行 /root/compiler

```
docker start -i devbox         
```

```
docker exec -it devbox bash
```

```
root@d9462d2e9edc:~# cd compiler/
root@d9462d2e9edc:~/compiler# ls   
Makefile  README.md  build  docs  hello.c  src
```

以后修改代码确认功能正确后，使用以下命令推送。注意修改的时候保留注释
```
git add .
git commit -m "你的提交信息"
git push
```

该仓库中存放了一个基于北大 <https://pku-minic.github.io/online-doc/#/lv1-main/教程编写的> SysY 编译器项目

<br />

测试命令(在build目录下)：

```
./compiler -koopa 输入文件.c -o 输出文件.koopa
```
```
./compiler -riscv 输入文件.koopa -o 输出文件.s
```

自动测试 Koopa IR:

```
autotest -koopa -s lv6 /root/compiler
```

自动测试 RISC-V 汇编:

```
 autotest -riscv -s lv6 /root/compiler
```

