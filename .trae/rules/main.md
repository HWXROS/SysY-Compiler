https://pku-minic.github.io/online-doc/#/lv0-env-config/docker

项目编译使用官方提供的docker镜像，挂在共享目录执行
C:\HWXWS\code\sysy-make-template>docker ps -a
CONTAINER ID   IMAGE                  COMMAND   CREATED        STATUS             PORTS              NAMES
d9462d2e9edc   maxxing/compiler-dev   "bash"    31 hours ago   Exited     (255) 16 seconds ago      devbox

C:\HWXWS\code\sysy-make-template>docker start devbox         
devbox
C:\HWXWS\code\sysy-make-template>docker exec -it devbox bash
root@d9462d2e9edc:~# cd compiler/
root@d9462d2e9edc:~/compiler# ls   
Makefile  README.md  build  docs  hello.c  src