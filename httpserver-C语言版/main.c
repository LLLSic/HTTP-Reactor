#include "server.h"
#include <stdio.h>
#include <stdlib.h>  // atoi()
#include <unistd.h>  // chdir()

int main(int argc, char* argv[]) {
     //第一个参数是port 第二个参数是地址
    printf("start main...\n");
    if (argc < 3) //没有带参数
    {
        printf("need port and path\n");
        return -1;
    }
    unsigned short port = atoi(argv[1]);

    //切换当前进程的工作目录
    chdir(argv[2]);

    // 初始化用于监听的套接字
    int lfd = initListenFd(port);

    // 启动服务器程序
    epollRun(lfd);

    return 0;
}