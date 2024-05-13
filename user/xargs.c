#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char command[12][50];
    char buf[512];
    int line = 0, num = 0;

    // 读入标准输入中的参数，分行存储
    int n = read(0, buf, sizeof buf);
    for (int i = 0; i < n; i++)
    {
        if (buf[i] == '\n')
        {
            command[line][num] = 0; // char类型中0表示字符串结束
            line++;
            num = 0;
            continue;
        }
        command[line][num++] = buf[i];
    }

    char *arguments[64];
    int num_of_arg = 0;
    for (int i = 1; i < argc; i++)
        arguments[num_of_arg++] = argv[i];

    for (int i = 0; i < line; i++)
    {
        arguments[num_of_arg] = command[i];
        if (fork() == 0)
        {
            exec(argv[1], arguments);
            exit(0);
        }
        else
            wait(0); // 注意wait，确保子程序接受再进行下一个循环，运行新的子程序
    }

    exit(0);
}