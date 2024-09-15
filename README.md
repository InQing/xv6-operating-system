
本实验要求编写五个用户态的函数，分别为：sleep, pingpong,  primes, find, xargs



## （一）sleep
### (1)要求：
编写一个**用户程序**。接受一个参数t，然后调用系统调用sleep，休眠t个单位时间
### (2)思路：
本实验不需要实现“如何休眠t个单位时间”，这是sleep系统调用该考虑的事（可以在kernel中查看sleep系统调用的具体实现），关于使用系统调用后会发生什么，lab2中会再详解。
我们只需要接受参数，然后将之传递给系统调用即可。
### (3)程序如何接受参数？
c语言中，传递给main函数的参数一般有两种
——void
——int argc, char* argv[]
其中argc是参数数量，argv是参数列表
***注意：***argv[0]始终表示程序自身的名字（或路径），所以用户真正传递给main的参数其实是从argv[1]开始的。
### (4)参数转换

传入的参数是字符串，需要用atoi函数转换成整数

了解以上几点，程序就很容易实现了。

```c
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
```
实现完程序后，还有几个点需要考虑：
### (5)在makefile中加入sleep
关于makefile是什么：Makefile 可以简单的认为是一个**工程文件的编译规则**，描述了整个工程的编译和链接等规则。其中包含了那些文件需要编译，那些文件不需要编译，那些文件需要先编译，那些文件需要后编译，那些文件需要重建等等。

扩展一下，我们使用vc编写c++程序都遇过一个叫cmake的东西，而cmake其实就是一个**跨平台**的编译工具，可以在读入所有源文件后，自动生成makefile。
同时，工程的原作者可以提供一份CMakeLists.txt文档。框架的使用者们只需要在下载源码的同时下载作者提供的CMakeLists.txt，就可以利用CMake，在”原作者的帮助下“进行工程的搭建。

### (6)make qemu
qemu是一个模拟计算机硬件的软件，或者说，是一台**虚拟机**，我们要运行xv6操作系统，就需要在它之上

## （二）pingpong
### (1)要求：
编写一个pingpong程序，父进程中fork出子进程，在父子进程中利用管道实现双向通信
### (2)如何创建进程，实现多进程？
fork系统调用创建子进程
在子进程中返回0，父进程中返回子进程的pid，故可用if(pid==0)来判断是在子进程还是父进程中
创建子进程后，子进程拷贝父进程的所有信息（包括PCB），但是，两者的地址空间实际上是不同的

***注意：*** 由操作系统基础知识的调度规则，我们可知父子进程谁先上处理机是不一定的，可能父进程先运行1/3，然后子进程运行，父进程再运行1/3。所以如果在父子进程中printf，会出现乱序的情况。
但是不必担心这会影响我们程序的执行，因为所使用的大部分系统调用都是有自旋锁的。

### (3)如何使用管道进行通信？
1.管道概念的介绍：
管道是一种最基本的IPC机制，作用于有血缘关系的进程之间，完成数据传递。调用pipe系统函数即可创建一个管道。有如下特质：
1. 其本质是一个伪文件(实为**内核缓冲区**)
2. 由两个文件描述符引用，**一个表示读端，一个表示写端**。
3. 规定数据从管道的写端流入管道，从读端流出。

2.管道的原理: 管道实为内核使用环形队列机制，借助内核缓冲区(4k)实现。

3.管道的局限性：
① 数据自己读不能自己写。
② 数据一旦被读走，便不在管道中存在，不可反复读取。
③ 由于管道采用**半双工通信方式**。因此，数据只能在一个方向上流动。
④ 只能在有公共祖先的进程间使用管道。

4.管道的实现：
int pipe(int fd[2]); 
成功：0；失败：-1，设置error
函数调用成功返回r/w两个文件描述符。**无需open，但需手动close**。规定：**fd[0] → r； fd[1] → w**，就像0对应标准输入，1对应标准输出一样。向管道文件读写数据其实是在读写内核缓冲区。

**注意**：pipe读写时会上锁

5.父向子传递消息的方式：
1. 父进程调用pipe函数创建管道，得到两个文件描述符fd[0]、fd[1]指向管道的读端和写端。
2. 父进程调用fork创建子进程，那么子进程也有两个文件描述符指向同一管道。
3. 父进程关闭管道读端，子进程关闭管道写端。父进程可以向管道中写入数据，子进程将管道中的数据读出。

![在这里插入图片描述](https://i-blog.csdnimg.cn/blog_migrate/c1990b5ff036f1e644ec3274209078ab.png)
### (4)如何进行读写操作？
1、write()
函数定义：
ssize_t write (int fd, const void * buf, size_t count); 
函数说明：
write()会把参数buf所指的内存写入count个字节到参数fd所指的文件内。

2、read()
函数定义：
ssize_t read(int fd, void * buf, size_t count);
函数说明：
read()会把参数fd所指的文件传送count 个字节到buf 指针所指的内存中。

至此，已具备pingpong的所有前置知识

```c
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
```

## （三）primes
### (1)要求：
用多进程实现埃氏筛法
### (2)思路：
这个实验并没有新的前置知识，只需要理解思路即可。
都知道，埃氏筛法是利用质数的倍数筛去所有合数（打上标记），最后所有未打标记的数就是质数，而放在多进程里就是：
1.父进程拥有初始数组p1=[2~35],取出数组**第一个数x**，该数**一定是质数**
2.将数组中所有等于x倍数的数都剔除，得到p2
3.利用管道将p2传给子进程
4.子进程成为新的父进程，重复1~4步骤，直到p数组中只剩下最后一个质数

结合这张图更好理解
![在这里插入图片描述](https://i-blog.csdnimg.cn/blog_migrate/29aee9400a1bb501fdb257821b1d1479.png)

很显然，这是一个递归过程，可以用dfs轻易实现

代码实现

```c
int primes[35];

void dfs(int cnt)
{
    //  埃氏筛法，进程中的第一个素为质数，用其倍数筛掉其他数
    int x = primes[0];
    printf("prime %d\n", x);

    if (cnt == 1)
        return;

    // 创建管道
    int p[2];
    pipe(p);

    int pid = fork();

    if (pid == 0)
    {   // 子进程中
        int num = 0; // 父进程筛除后剩余数的个数
        close(p[1]);
        while (read(p[0], &primes[num++], sizeof(int)))
            ;

        close(p[0]);
        dfs(num - 1); // 最后read返回0，num还要加一次，所以这里减1
        exit(0); // 记得退出
    }
    else
    {   // 父进程中
        close(p[0]);
        for (int i = 1; i < cnt; i++)
        {
            if (primes[i] % x != 0)
                write(p[1], primes + i, sizeof(int));
        }
        close(p[1]); // 记得及时关闭fd，否则会占用资源
        wait(0); // 父进程必须要等待子进程的递归都结束了，再返回该层递归
    }
}

int main(void)
{
    for (int i = 0; i <= 33; i++)
        primes[i] = i + 2;

    dfs(34); // 2~35

    exit(0);
}
```

**注意**，由于我们的递归过程没有锁，所以递归栈中进程运行的顺序不确定。因此，我们必须在父进程中调用wait函数等待子进程的递归全部结束后，再返回

## （四）find
个人觉得lab1中最难的一个程序，需要理解很多文件的知识
### (1)要求：
写一个find程序，接受两个参数：**目录**与**目标文件**，要求在该目录下找到所有符合的目标文件，并打印出路径。
很显然这也是一个递归程序，我们需要dfs遍历所有目录与所有文件
### (2)前置知识：user/ls.c
阅读并理解ls函数，我们可以得到的技巧如下：

1.如何获取文件的信息？如类型（目录or文件）
正如进程有PCB，文件也有FCB（文件控制块），保存了文件的各种信息。而fstat(fd,&st)函数可以从文件描述符fd中将FCB拷贝到st中，st.type就保存了文件的类型

2.如果该文件是目录，如何遍历该目录下所有文件？
struct dirent 是一个目录项结构体
有些操作系统中FCB=目录项，但在Unix(xv6同理)中，为了简化文件搜索的操作，只用
**文件名name[DIRSIZ]**与**索引节点编号inum**表示一个目录项，而具体的FCB则由inum索引
xv6中每个目录项大小是DIRSIZ，为14B

**while (read(fd, &de, sizeof(de)) == sizeof(de))**，fd是一个目录文件描述符，循环可以遍历目录下的所有文件，将每个文件的目录项存到de中。

3.如何组合路径，如何判断是否找到目标文件？
需要用到strcmp（c语言中字符串不能直接==比较）,strcpy,memmove三个函数。拼接路径方法ls函数中已经给出，理解并照抄即可


```c
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
```

注：遇到错误可以用**fprintf**或**perror**输出到标准错误2中

## （五）xargs
### (1)要求：
用xargs运行一个程序（如fork），然后接受标准输入中的多行参数，分别将每行参数拼接到fork程序后作为参数运行

### (2)exec
exec(char* file, char** argv)
将原进程替换成新进程（但pid不变），
两个参数分别为包含可执行文件的文件名，和一个字符串参数数组
一般用法为先fork出一个子进程，然后在子进程中exec

将标准输入中的参数按行拆出来，然后一一传进exec中的第二个参数中（第一个参数是我们要运行的程序），很简单，没啥好说的，直接上程序

```c
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
```

## （六）总结
- 几个系统调用的使用如fork,wait,exec
- 管道的使用，pipe
- 多进程编程，fork
- 文件操作，FCB，目录项，文件遍历
