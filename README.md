本实验将深入内核，编写两个系统调用：System call tracing, Sysinfo


## （一）System call tracing
### (1)实验要求
添加一个系统调用trace，用于追踪指定的系统调用

 1. 参数一：mask（掩码），通过掩码来确定追踪哪些系统调用，每个系统调用都有一个系统调用编号（/kernel/syscall.h中可以看到），如果掩码的第n位为1，则表示追踪编号为n的系统调用
 2. 参数二：追踪的指令，即在该指令运行下，追踪相应的系统调用
 3. 输出系统调用的进程ID、名称及返回值
 
 ### (2)系统调用流程
图片来源于博客园YuanZiming大佬的博客
https://www.cnblogs.com/YuanZiming/p/14218997.html
 ![](https://i-blog.csdnimg.cn/direct/c39152955b78491aabcebf9c0df35d33.png#pic_center)
### (3)trace执行流程
根据上诉流程图与xv6 book中4.3，4.4的介绍，先梳理一下trace的执行流程
 1. 命令行输入trace mask command
 2. **用户态**下，执行trace.c，执行trace(mask)与exec(command)两个系统调用
 3. 执行usys.S中的汇编指令。系统调用的**参数**传入了a0寄存器中，系统调用编号传入了a7寄存器中，调用ecall，转入**内核态**
 4. 执行trampoline.S中的汇编代码（jr t0），跳转到内核指定的中断判断函数usertrap()
 5. trap.c中的usertrap()判断中断类型，如果是系统调用，则执行**syscall()**
 6. syscall.c中的syscall()，取出a7中的系统调用号num，然后根据num执行相应的系统调用。
 7. 由于先执行的是trace(mask)，所以先来到sysproc.c中的sys_trace()，实现系统调用的具体功能。trace的功能是：从a0中取出系统调用的参数（即mask），放入进程的PCB中，用以让当前进程获取到mask
 8. sys_trace()系统调用结束后，回到syscall中，返回值保存在a0中。
 9. 之后exec(command)执行，假设command是fork()，则从syscall()跳转到sys_fork()
 10. sys_fork()系统调用结束后，回到syscall中，返回值保存在a0中。
 11. trace系统调用已经将mask放入当前进程的PCB中，所以syscall中可以直接获取到mask。若k是fork的系统调用编号，而mask中第k位为1，则打印fork调用的信息
 12. 对于exec(command)连锁执行的一系列系统调用，皆按照fork()的相似步骤执行。
 
 
 **要点总结：**
- 系统调用的执行流程：用户态函数fork()，ecall跳入内核态，**syscall()**，sys_fork()。任何系统调用都要经过syscall，并在syscall中转入对应的系统调用
- a7存储系统调用的编号，用于索引对应的系统调用函数
- a0既存储系统调用的传入参数，也存储系统调用的返回值

### (4)实验步骤
根据官方文档的hints一步步走
**1.** 让新添加的trace参与到项目编译中
Add ***$U/_trace*** to UPROGS in Makefile
**2.** 添加关于trace的声明、接口与系统调用号
分为三步：
 - add a ***prototype*** for the system call to user/user.h
声明系统调用trace的函数原型
 传入的参数即为int型掩码，返回值int（0成功，-1出错）
```c
int trace(int);
```
- a stub to user/usys.pl
添加用户态调用trace的接口

```c
entry("trace");
```
- a ***syscall number*** to kernel/syscall.h
为新的系统调用trace添加系统调用号

```c
#define SYS_trace  22
```

**3.** 实现内核态系统调用sys_trace()的具体功能

> Add a sys_trace() function in kernel/sysproc.c that implements the new
> system call by remembering its argument in a new variable in the proc
> structure.

sys_trace()的功能很简单，就是将a0寄存器中的参数，即mask写到PCB里即可，代码如下：

```c
uint64
sys_trace(void){
  int mask; // 掩码，第k位为1即代表追踪编号2^k的系统调用
  //argint获取a0寄存器中存放的系统调用参数，
  if(argint(0, &mask) < 0)
    return -1;
  //在进程控制块中加入mask变量，这样fork子进程时复制父进程信息，就能保留mask的值
  //从而完成参数在进程之间的传递
  myproc()->mask = mask;
  return 0;
}
```
**4.** 修改fork
Modify fork() (see kernel/proc.c) to copy the trace mask from the parent to the child process.
父进程fork出子进程时，子进程会拷贝父进程的PCB信息。由于mask是我们新加入PCB中的，所以原fork中没有继承，这里加上去即可

```c
 //copy mask to child
  np->mask = p->mask;
```
**5.** 修改syscall
Modify the syscall() function in kernel/syscall.c to print the trace output. You will need to add an array of syscall names to index into.
需要修改syscall，根据mask的值判断当前系统调用是否需要打印。同时要增加系统调用编号到系统调用函数、名称的索引
- 修改syscall()
```c
void syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7; // 系统调用编号
  if (num > 0 && num < NELEM(syscalls) && syscalls[num])
  {
    p->trapframe->a0 = syscalls[num]();

    // num为系统调用编号，mask的第num位为1即说明追踪该系统调用
    // a0寄存器存储系统调用的返回值
    if (((p->mask) >> num) & 1)
      printf("%d: syscall %s -> %d\n", p->pid, syscall_names[num], p->trapframe->a0);
  }
  else
  {
    printf("%d %s: unknown sys call %d\n",
           p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```
- 系统调用号到函数的索引
```c
static uint64 (*syscalls[])(void) = {
  ...
  [SYS_trace]   sys_trace,
};

```
- 系统调用号到名称的索引
```c
static char *syscall_names[] = {
    ...
    [SYS_trace] "trace",

```
- 声明
```c
extern uint64 sys_trace(void);
```

## （二）Sysinfo
### (1)实验要求
编写一个系统调用sysinfo，参数为struct sysinfo类型的指针addr。sysinfo获取**空闲内存的字节数**和**进程数量**，保存在addr指向的地址中，返回给用户空间

 - 关于sysinfo的声明、系统调用接口等就不再赘述，在trace有详细说明。这里我们关注三个主体部分：系统调用的具体实现**sys_sysinfo**，获取空闲内存字节数的函数**kamount**，获取进程数量的函数**procammount**

### (2)sys_sysinfo()
- sys_sysinfo()的功能是获取空闲内存的字节数与进程数量，并返回给用户空间，前者我们会另外编写函数，稍后再考虑，这里我们要解决的是**如何将着两个信息返回给用户空间，即保存入传入的指针addr中**
- 根据hints，我们先查看sys_fstat() (kernel/sysfile.c) 和 filestat() (kernel/file.c)找到copyout函数的用法，再细看copyout函数的具体定义
- copyout定义
```c
// Copy len bytes from src to virtual address dstva in a given page table.
// 把在内核地址src开始的len大小的数据拷贝到用户进程pagetable的虚地址dstva处
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
```
- filestat函数中关于copyout函数的用法

```c
if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
```
据此，我们很容易写出sys_sysinfo()

```c
uint64
sys_sysinfo(void){
  struct sysinfo info;
  uint64 addr; // 将结果保存入addr指向的空间中
  info.freemem = kamount();
  info.nproc = procammount();

  //sysinfo系统调用的传入参数addr保存在寄存器a0中
  if(argaddr(0, &addr) < 0)
    return -1;

  // copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
  // 把在内核地址src开始的len大小的数据拷贝到用户进程pagetable的虚地址dstva处
  if(copyout(myproc()->pagetable, addr, (char*)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}
```
### (2)空闲内存的字节数
- xv6中的内存采用分页存储管理，每个页面的大小为PGSIZE，即4KB。
- 查看kalloc.c中的几个函数（如kalloc(),kfree()），可以发现空闲的页面是用一个链表连接起来的,freelist指针指向空闲链表的头结点。
- 细看kfree会发现，当free一页内存时，该块内存会以头插法插入到空闲链表的头表头

```c
 r->next = kmem.freelist;
  kmem.freelist = r;
```
- 访问内存时，要上锁

```c
acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
```

根据上诉信息，我们很容易就能得出代码的思路：遍历空闲链表得到空闲块的块数ammount，ammount即为空闲内存的字节数
代码如下：

```c
uint64
kamount(void){
  uint64 amount = 0; // 空闲内存块的数量

  acquire(&kmem.lock); // 上锁，不允许被打断
  struct run *r = kmem.freelist; // 遍历空闲链表的指针
  while(r){
    amount++;
    r = r->next;
  }
  release(&kmem.lock);

  return amount * PGSIZE;
}

```

### (3)进程数量
- 查看proc.c中的函数如kill()可知，进程是用一片连续的地址空间，即数组proc存放的，proc中每个元素为进程的PCB（标识进程存在的唯一信息）

所以，思路显而易见：遍历proc，根据PCB找出状态非UNUSED的进程并统计。同样要记得上锁
代码如下：

```c
uint64
procammount(void){
  uint64 amount = 0; // 进程的数量
  struct proc *p;
  //进程用一片连续的数组proc存放，最大NPROC为64，找出状态非UNUSED的进程即可
  for (p = proc; p < proc + NPROC; p++){
    acquire(&p->lock);
    if(p->state != UNUSED)
      amount++;
    release(&p->lock);
  }

  return amount;
}
```

## （三）总结
- 本章最重要的莫过于系统调用的执行流程，再回顾一遍：
  1. 用户态下执行函数fork(),触发中断。这是系统调用提供给用户的接口
  2. 通过ecall跳转入内核态下的中断处理函数
  3. trap函数判断中断类型为系统调用，转入响应函数syscall
  4. syscall从寄存器中获取系统调用的编号，索引到相应的系统调用
  5. 转入sys_fork，这是系统调用的具体实现函数

- 内存管理的知识：分页存储、空闲链表法
- 进程相关概念：PCB

 
