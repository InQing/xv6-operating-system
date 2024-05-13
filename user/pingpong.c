#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void perror(const char *str)
{
    printf("%s\n", str);
    exit(-1);
}

int main(int argc, char *argv[])
{
    if (argc != 1)
        perror("extra arguments!");

    int fd_1[2]; // 管道的文件描述符
    int fd_2[2]; // 管道半双工，要实现双向通信需要创建两个管道
    char buf[100]; // 读写缓冲区

    if (pipe(fd_1) == -1) // 创建管道
        perror("create pipe");
    if (pipe(fd_2) == -1) // 创建管道
        perror("create pipe");

    int pid = fork(); // 创建子进程

    if (pid < 0)
    {
        perror("fork child");
    }
    else if (pid == 0)
    { // 子进程中
        close(fd_1[1]);
        read(fd_1[0], buf, sizeof(buf));
        int pid_child = getpid();
        printf("%d:%s\n", pid_child, buf);

        close(fd_2[0]);
        write(fd_2[1], "received pong", 14);
    }
    else
    { // 父进程中
        close(fd_1[0]);
        write(fd_1[1], "received ping", 14);

        close(fd_2[1]);
        read(fd_2[0], buf, sizeof(buf));
        int pid_father = getpid();
        printf("%d:%s\n", pid_father, buf);
    }

    close(fd_1[0]);
    close(fd_2[1]);
    exit(0);
}