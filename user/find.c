#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 在该dir目录下找file文件
// 遇到目录则递归检索，遇到文件则检查是否匹配
void find(char *dir, char *file)
{
    char new_path[512]; // 文件路径
    int fd;
    struct dirent de; // 目录项，包含inum与name
    struct stat st; // 文件的FCB

    if ((fd = open(dir, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", dir);
        return;
    }

    // fstat从文件描述符fd中获取FCB
    if ((fstat(fd, &st) < 0) || st.type != T_DIR)
    {
        fprintf(2, "find: cannot stat %s\n", dir);
        close(fd);
        return;
    }

    // 判断当前路径拼接下一级路径后是否会过长
    if (strlen(dir) + 1 + DIRSIZ + 1 > sizeof(new_path))
    {
        printf("find:path too long\n");
        return;
    }

    // 遍历该目录下的所有文件
    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
        if (de.inum == 0)
            continue;

        // 跳过当前目录与上一级目录，防止死循环
        if (!strcmp(de.name, ".") || !strcmp(de.name, ".."))
            continue;

        // 拼接新的路径
        strcpy(new_path, dir);
        char *ptr = new_path + strlen(dir);
        *ptr++ = '/';
        memmove(ptr, de.name, DIRSIZ);
        *(ptr + DIRSIZ) = 0; // 字符串以整型0结尾表示结束

        // stat从文件名中FCB，用以判断文件格式
        if (stat(new_path, &st) < 0)
            continue;

        switch (st.type)
        {
        // 如果是目录则继续递归遍历
        case T_DIR:
            find(new_path, file);
            break;

        // 如果是文件则判断是否是目标，注意找到目标后不要返回，以为目标可能不止一个
        case T_FILE:
            if (!strcmp(de.name, file))
            {
                printf("%s\n", new_path);
                break;
            }
        }
    }

    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(2, "usage: find [directory] [target filename]\n"); // 输出到标准错误
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}