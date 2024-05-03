#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 在该dir目录下找file文件
// 遇到目录则递归检索，遇到文件则检查是否匹配
void find(char *dir, char *file)
{
    char new_path[512];
    int fd;
    struct dirent de;
    struct stat st; // 文件的索引节点

    if ((fd = open(dir, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", dir);
        return;
    }

    // fstat从文件描述符fd中获取索引节点
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
        // de.inum==0表示这是一块已经初始化并且可以用来创建文件或者文件夹的位置，所以在读取的过程中应当无视这一块空间
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
        *(ptr + DIRSIZ) = 0;

        // stat从文件名中获取索引节点，用以判断文件格式
        if (stat(new_path, &st) < 0)
            continue;

        switch (st.type)
        {
        case T_DIR:
            find(new_path, file);
            break;

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