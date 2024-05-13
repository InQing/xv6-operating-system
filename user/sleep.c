#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Error num of arguments!");
        exit(-1); // exit也是一个系统调用，非0表示异常退出
    }

    int num_of_ticks = atoi(argv[1]);
    if (sleep(num_of_ticks) == -1) // sleep系统调用
    {
        printf("Error! Can't sleep!");
        exit(-1);
    }

    exit(0);
}