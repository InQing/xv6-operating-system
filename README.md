## （一）前置知识：mmap
### （1）VMA
VMA（Virtual Memory Area） 代表虚拟内存区域，它描述了一个进程的虚拟地址空间中的一个连续区间，并与物理内存或磁盘上的存储区域相关联。
具体来说，每个进程都有一个虚拟地址空间，其中可以包含多个 VMA。每个 VMA 定义了虚拟地址空间中的一个区间，并包含以下信息：
-  起始地址和结束地址：定义了该 VMA 在虚拟地址空间中的范围。
- 访问权限：每个 VMA 具有特定的访问权限，例如只读、读写、执行等。定义了进程如何访问该区域的内存。
-  映射的对象：VMA 可以映射到物理内存中的某些页，也可以映射到磁盘上的文件或设备。
-  属性标志：VMA 还可能包含一些标志或属性，例如是否共享、是否为匿名映射（不与文件关联）等。

简单来说，VMA是组成虚拟地址空间的子集，代码段、数据段、堆、栈等都是VMA。
本实验中的内存映射文件区域也是一段VMA

### （2）mmap
mmap 是 UNIX 和类 UNIX 系统中的一种内存映射技术，允许将文件或设备映射到进程的内存空间。这种技术提供了一种高效的文件 I/O 方式，使得进程可以像访问内存一样访问文件内容，而不需要显式的 I/O 操作（如 read() 和 write()）。

**1. 基本概念**:
mmap（memory map）允许将一个文件或设备中的一部分内容映射到进程的虚拟地址空间。当文件映射成功后，进程可以直接通过指针访问文件内容，而无需再调用系统的 I/O 函数。这种方式提高了文件访问的效率，尤其是在处理大文件时。
**2. mmap 的工作原理**:
- 当调用 mmap 时，内核将文件内容或设备内存的某一部分映射到进程的虚拟内存地址空间中。
- 这段内存区域与文件的物理内容保持同步，进程对这段内存的任何读写操作都会反映在文件中，反之亦然（如果映射是可写的）。
- mmap 可以被用于进程间通信（IPC），当多个进程映射同一个文件时，它们可以通过共享的内存区域进行通信。

**3. mmap 系统调用**:
mmap本函数原型如下：

```c
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
```
-  addr: 指定映射的起始地址，通常为 NULL，让内核自动选择合适的地址。
- length: 要映射的字节数。
-  prot: 映射区域的保护模式。常见的值有：
	- PROT_READ：页面可读。
	- PROT_WRITE：页面可写。
	- PROT_EXEC：页面可执行。
	- PROT_NONE：页面不可访问。
- flags: 控制映射对象的类型及映射区域的共享性。常见的值有：
	- MAP_SHARED：映射区可共享，即对映射区的修改对其他映射到该文件的进程可见。
	- MAP_PRIVATE：私有映射区，对映射区的修改对其他进程不可见，且对原文件没有影响。
	- MAP_ANONYMOUS：匿名映射区，与文件无关，通常用于进程间通信。
-  fd: 文件描述符，指向要映射的文件。如果使用匿名映射，则 fd 为 -1。
- offset: 映射的文件起始偏移量，必须为**页面大小的整数倍**。
- 返回值：成功时，mmap 返回映射区的指针；失败时，返回 MAP_FAILED。

**4. mmap 使用示例**:
将文件映射到内存中，并通过内存访问文件内容：

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
int main() {
    int fd = open("example.txt", O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }
struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("fstat");
        return 1;
    }
// 将文件映射到内存
    char *mapped = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
// 直接通过指针访问文件内容
    printf("File contents:\n%.*s\n", (int)sb.st_size, mapped);
// 解除映射
    if (munmap(mapped, sb.st_size) == -1) {
        perror("munmap");
        return 1;
    }
close(fd);
    return 0;
}
```

## （二）Lab：mmap
### （1）前置工作
**1.添加`mmap`与`munmap`系统调用**
在kernel/syscall.h, kernel/syscall.c, user/usys.pl 和 user/user.h 中添加系统调用的声明，在makefile中将mmaptest加入编译。
可回顾lab2，具体不再赘述

**2.定义VMA结构**
定义VMA，用以记录文件映射内存区域的**信息**，如映射地址、长度、文件指针、偏移量、映射区域的权限等。

```c
struct mmap_vma
{
  uint64 addr;
  uint length, offest;
  int port, flags;
  struct file *file;
};
```
每个**进程**的虚拟地址空间中包含多个文件映射区域的VMA。所以，可知VMA是**进程**的一个**私有属性**
故该VMA结构应定义在`proc.h`中
同时，在进程的PCB（`struct proc`）中增加VMA字段

```c
// Per-process state
struct proc {
  // ...
  char name[16];               // Process name (debugging)

  struct mmap_vma vma[NVMA];   // virtual memory areas for mmap-ed file
};
```
宏`NVMA`表示VMA区域的最大个数，定义在`param.h`中，值为16

### （2）实现sys_mmap()
**1.`sys_mmap()`的功能**
1. 寻找空闲的VMA
2. 在进程的虚拟地址空间中，找到一段大小合适的区域，用于映射文件
3. VMA记录该区域的起始地址addr，并记录mmap传入的其它参数
4. 增加映射文件的引用

由于采用了**懒分配**，所以映射时，不需要实际分配物理内存，等到page fault时再分配

**2.核心问题**
此处有两个核心问题：将地址空间中的哪一部分作为内存映射文件的区域？划定区域后，如何寻找合适大小的区域分配给文件？

**问题一，映射区域**：**Linux**中，栈位于堆的上面，栈向下生长，堆向上生长，而中间共享区的某一部分，便用作文件映射的区域，且基址和大小是**动态分配**的
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/8e24214ee0a84063ac46444f19affb88.png#pic_center)
但xv6中的地址空间有所不同，栈固定为一个PGSIZE的大小，堆位于栈的上面，向上增长。中间没有空闲的区域。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/735aeefebe4a4cfb8624bdcf3a669462.png#pic_center)

所以很显然，我们不能参考linux中的区域划分。
个人此处的做法是：在堆区最高处，也就是trapframe页面的下面，划分一块**固定大小**的区域用于映射文件。
区域划分在堆的最高处，为的是尽量避免冲突。可以在`sbrk`中增加判断，当堆生长进入这个区域后，发生panic。或设置guard page，发生page fault。笔者此处并未添加判断（懒）。

`memlayout.h`中，设置表示该mmap区域基址的宏

```c
#define MMAPBASE (TRAPFRAME - 16 * PGSIZE)
```

**问题二：寻找区域**
此处要在MMAPBASE往上的16个页面中，寻找合适大小的空间，用于映射文件。
连续内存分配的最佳算法，想必大家都知道——**首次适应法**。
故从MMAPBASE开始向上寻找，如果该部分能够容纳映射的length，并且不与其它VMA中分配的区域冲突，那么就选定其为映射该文件的区域。

**3.一些小细节**
- 传入的参数是文件描述符fd，但要求存入VMA中的是file类型的指针。PCB中维护了进程的文件打开表`struct file *ofile[NOFILE]`，可以由fd索引到file*。但其实还有更简单的方法，从寄存器获取参数时使用`argfd`函数，可以直接将file*的结构传出
- mmaptest中有readonly检测，注意不可将不可写文件映射为MAP_SHARE
- 查看linux手册（man mmap）可知，offest必须为PGSIZE的整数倍
- 使用`filedup`增加文件引用数，避免文件被释放。

**4.`sys_ mmap`代码**
```c
uint64
sys_mmap(void)
{
  uint64 addr;
  int length, port, flags, offest;
  struct mmap_vma *vma = 0;
  struct file *file;
  struct proc *p;
  int i, is_find = 0;

  // argfd可获取fd结构与file结构
  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &port) < 0 || argint(3, &flags) < 0 || argfd(4, 0, &file) < 0 || argint(5, &offest) < 0)
  {
    return -1;
  }

  // 参数为负不需要检查，因为函数定义中uint类型已经保证了其为正
  // 不可将不可写文件映射为MAP_SHARE
  if(file->writable == 0 && (flags & MAP_SHARED) && (port & PROT_WRITE))
    return -1;
  // offest必须是PGSIZE的整数倍
  if (offest % PGSIZE)
    return -1;

  p = myproc();
  for (i = 0; i < NVMA; ++i)
  {
    if (p->vma[i].addr == 0)
    {
      vma = &p->vma[i];
      break;
    }
  }
  if (!vma)
    return -1;

  if (addr != 0)
  {
    is_find = 1;
  }
  // 若addr为NUll，则内核采用首次适应法分配空间
  else
  {
    addr = MMAPBASE;
    while (addr + length < TRAPFRAME)
    {
      is_find = 1;
      for (i = 0; i < NVMA; ++i)
      { 
        // 有重叠部分，则不可行
        if (addr < p->vma[i].addr + p->vma[i].length && p->vma[i].addr < addr + length)
        {
          is_find = 0;
          break;
        }
      }

      if (is_find)
        break;
      else
        addr += PGSIZE;
    }
  }

  if (is_find)
  {
    vma->addr = addr;
    vma->length = length;
    vma->port = port;
    vma->flags = flags;
    vma->offest = offest;
    vma->file = file;

    filedup(vma->file);

    return addr;
  }
  else
  {
    return -1;
  }
}
```

### （3）实现pagefault
修改usertrap中的功能，实现page fault，和lazy alloction实验的内容相似
**1.page fault功能**
1. 根据出错的va，找到对应的VMA
2. 分配物理内存pa
3. 调用`readi()`，将va对应的那**一页**文件读取到pa
4. 将PORT标志位转换成PTE标志位
5. 调用`mappages()`将va映射到pa

**注：** 此处还添加了脏页功能，但由于过程略显复杂，稍后会单独拎出来细讲

**2.一些小细节**
- 此处直接使用va，或者让va往下对齐页面，即`PGROUNDDOWN(va)`都可以。因为我们要映射的范围刚好是一整个页面
- 映射一整个页面的内容，可能将**不需要**的文件部分映射进来，甚至可能**超出**文件最大部分。不过这都没关系，对于不需要的部分，写回文件时并不会将这个部分也写回，对于超出文件大小的部分，`readi`会帮忙截获。
- 调用`readi`需对相应的inode加锁
- 注意，PORT标志位并不等同于PTE标志位，使用时需要转换。但实际上，PORT标志位右移一位，就转换成了PTE标志位
fcntl.h中添加PORT转PTE标志的宏：

```c
#define PORT2PTE(port) (port<<1)
```

**3.usertrap()代码**

```c
void usertrap(void)
{
  // ...
  
  else if ((which_dev = devintr()) != 0)
  {
    // ok
  }
  else if (r_scause() == 12 || r_scause() == 13 || r_scause() == 15)
  {
    char *pa;
    uint64 va = PGROUNDDOWN(r_stval());
    struct inode *ip;
    struct mmap_vma *vma = 0;
    uint offest;
    int i, ret = 0, flags = PTE_U;

    // 找到页面错误地址对应的VMA
    for (i = 0; i < NVMA; ++i)
    {
      if (va >= p->vma[i].addr && va < p->vma[i].addr + p->vma[i].length)
      {
        vma = &p->vma[i];
        break;
      }
    }
    if (!vma)
    {
      p->killed = 1;
      goto end;
    }
	
	// 脏页设置
    // 1：页面未映射，读/执行指令:映射为不可写
    // 2：页面未映射，写指令：直接添加PTE_D与PTE_W标志位
    // 3：页面已映射，写指令。添加PTE_D标志位，并恢复PTE_W标志位
    if (r_scause() == 12 || r_scause() == 13)
    {
      ret = 1;
    }
    else
    {
      ret = dirty_write(vma->port, va);
      if (ret == 2)
      {
        flags |= PTE_D;
      }
      else if (ret == 3)
      {
        goto end;
      }
      else
      {
        p->killed = 1;
        goto end;
      }
    }

    // 为页面分配物理内存，并将va对应的一页文件读入该页面
    if ((pa = kalloc()) == 0)
    {
      p->killed = 1;
      goto end;
    }
    memset(pa, 0, PGSIZE);
    offest = vma->offest + (va - vma->addr);
    ip = vma->file->ip;
    ilock(ip);
    if (readi(ip, 0, (uint64)pa, offest, PGSIZE) == -1)
    {
      kfree(pa);
      iunlock(ip);
      p->killed = 1;
      goto end;
    }
    iunlock(ip);

    // 将虚拟地址映射到物理内存，并设置权限
    // 为了设置脏页，情况1要去除PTE_W标志
    flags |= PORT2PTE(vma->port);
    if(ret == 1){
      flags &= (~PTE_W);
    }
    if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, flags) != 0)
    {
      kfree(pa);
      p->killed = 1;
      goto end;
    }
  }
  else
  {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

end:
  if (p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2)
    yield();

  usertrapret();
}
```

### （4）实现sys_munmap
**1.sys_munmap功能**
1. 根据解除映射的addr与length，找到相应的VMA
2. 从addr到addr+length的范围里，**一页一页**地遍历页表
3. 判断该页是否有效，若无效，说明因为lazy alloction机制还未分配有效内存，故直接跳过
4. 判断该页是否是脏页，若是，且mmap方式为MAP_SHARE，则调用`filewrite`将该页写回磁盘
5. 调用`uvmunmap`将取消该页的映射
6. 根据取消映射的范围，相应地修改vma中的addr与length。若范围包含整个VMA，则直接清空该VMA

**2.一些小细节**
- 根据linux手册，传入的addr必须是PGSIZE整数倍
- 功能3	可以直接修改`uvmunmap`，当页面无效时直接略过，与lazy alloction中的修改方式雷同。也可以在`sys_munmap`中判断
- 写回磁盘时，若最后一部分不及整数页，不能按照整数页写回，因为这样做会使得脏数据**覆盖**原文件的内容。需截取这非整数页的部分，单独写回。
- 根据hints，`munmap`要么包含mmap区域的头部，要么包含尾部，要么解除整个区域，而不会出现在mmap区域内打洞的情况。故**不用担心**`munmap`会将一个mmap区域一分为二，从而需要多出一个VMA存储
- 若解除了整个mmap区域的映射，记得调用`fileclose`关闭文件

**3.sys_munmap代码**
**功能2~5** 解除映射与写回文件中涉及了`walk`函数，该函数是vm.c中的内部函数，不应泄露。且这部分的代码会在`exit`的修改中复用。
基于上述两个原因，将这部分代码抽出来单独作为一个`munmap_write`函数，放在vm.c中

vm.c中添加`munmap_write`：

```c
// 解除映射，写回文件
int 
munmap_write(struct mmap_vma *vma, uint64 addr, int length)
{
  uint64 a, write_size;
  pte_t *pte;
  struct proc *p = myproc();
  for (a = addr; a <= addr + length; a += PGSIZE)
  {
    if ((pte = walk(p->pagetable, a, 0)) == 0)
      return -1;
    // 若需要解除映射的页面还未映射，则略过（此处修改uvmunmap也可）
    if ((*pte & PTE_V) == 0)
      continue;
    // 若页面是脏页面，且mmap方式为共享，则写回磁盘
    if ((*pte & PTE_D) && (vma->flags & MAP_SHARED))
    {
      // 不能将非整数页部分按整数页写入，否则会覆盖原文件的数据
      write_size = ((addr + length - a >= PGSIZE) ? PGSIZE : (addr + length - a));
      if (filewrite(vma->file, a, write_size) == -1)
        return -1;
    }
    uvmunmap(p->pagetable, a, 1, 1);
  }

  return 0;
}
```

`sys_munmap`如下：

```c
uint64
sys_munmap(void)
{
  uint64 addr;
  int length;
  struct mmap_vma *vma = 0;
  struct proc *p;
  int i;

  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return -1;
  if(addr % PGSIZE)
    return -1;
  
  p = myproc();
  for (i = 0; i < NVMA; ++i)
  {
    if (addr >= p->vma[i].addr && addr < p->vma[i].addr + length)
    {
      vma = &p->vma[i];
      break;
    }
  }
  if (!vma)
    return -1;
  if (length == 0)
    return 0;

  // 取消映射，写入文件
  if(munmap_write(vma, addr, length) == -1)
    return -1;

  // 根据解除映射的范围，相应地缩小VMA
  if (addr == vma->addr && length == vma->length)
  {
    fileclose(vma->file);
    memset(vma, 0, sizeof(struct mmap_vma)); // 清空VMA
  }
  else if(addr == vma->addr){
    vma->addr += length;
    vma->length -= length;
  }
  else if(addr + length == vma->addr + length){
    vma->length -= length;
  }
  else{
    return -1;
  }

  return 0;
}
```
### （5）脏页位设置
虽然mmaptest并未测试脏页，但这个功能很有趣，还是尝试实现了一下。

**1.思路**
可以发现，xv6中并未实现脏页功能，甚至连PTE_D的宏都没有
所以我们完全是白手起家

先在riscv.h中设置一下宏

```c
#define PTE_D (1L << 7) // dirty
```

然后问题来了，我们如何知道哪些页进行了**写操作**呢，然后设置脏页呢？
很显然，写操作时用户程序的事情，作为内核我们难以知道程序在何处执行了写指令。
但是我们可以利用page fault，先将本应可写的页面变为**不可写**，这样，程序执行写操作后就会陷入page fault，我们就可以侦测出，是在哪些页面上执行**写**操作了。

有了大致思路后，我们进一步思考：
- 只有munmap在乎脏页面，故我们只需在映射mmap页面时，去除PTE_W标志即可，不需要扩展到全部页面。而映射mmap页面时发生在page fault中的，这样，我们只需要修改page fault一个地方即可。
- 如此，page fault中映射页面时，**即便页面可写**，我们也将之映射为不可写（真实的权限存放在VMA的port中）
- 这样，当发生page fault，一共有三种情况：
	- 一，页面未映射，**读/执**行指令。这种情况下，无论页面本身权限如何，都映射为不可写
	- 二，页面未映射，**写**指令。这种情况下，我们可以在映射时，直接添加PTE_D与PTE_W标志位。
	- 三，页面已映射，**写**指令。已映射的页面发生page fault，是因为写入了本应可以写，但被修改为不可写的页面。此时，添加PTE_D标志位，并恢复PTE_W标志位，这样该页面以后就不会page fault了。
	- 

**2.代码**
脏页设置的部分用到了`walk`函数，故同样集成在vm.c中，向外提供`dirty_write`函数作为接口

vm.c中的`dirty_write`：

```c
int 
dirty_write(int port, uint64 va){
  struct proc *p = myproc();
  pte_t *pte;
  if ((pte = walk(p->pagetable, va, 0)) == 0)
    return -1;
  // 第二情况，未映射，写指令
  if((*pte & PTE_V) == 0)
    return 2;
  // 第三种情况，已映射，写指令
  if(port & PROT_WRITE){
    *pte |= (PTE_D | PTE_W);
    return 3;
  }

  return -1;
}
```
`usertrap`中page fault代码：

```c

```c
void usertrap(void)
{
  // ...
  
  else if ((which_dev = devintr()) != 0)
  {
    // ok
  }
  else if (r_scause() == 12 || r_scause() == 13 || r_scause() == 15)
  {
    char *pa;
    uint64 va = PGROUNDDOWN(r_stval());
    struct inode *ip;
    struct mmap_vma *vma = 0;
    uint offest;
    int i, ret = 0, flags = PTE_U;

    // 找到页面错误地址对应的VMA
    for (i = 0; i < NVMA; ++i)
    {
      if (va >= p->vma[i].addr && va < p->vma[i].addr + p->vma[i].length)
      {
        vma = &p->vma[i];
        break;
      }
    }
    if (!vma)
    {
      p->killed = 1;
      goto end;
    }
	
	// 脏页设置
    // 1：页面未映射，读/执行指令:映射为不可写
    // 2：页面未映射，写指令：直接添加PTE_D与PTE_W标志位
    // 3：页面已映射，写指令。添加PTE_D标志位，并恢复PTE_W标志位
    if (r_scause() == 12 || r_scause() == 13)
    {
      ret = 1;
    }
    else
    {
      ret = dirty_write(vma->port, va);
      if (ret == 2)
      {
        flags |= PTE_D;
      }
      else if (ret == 3)
      {
        goto end;
      }
      else
      {
        p->killed = 1;
        goto end;
      }
    }

    // 为页面分配物理内存，并将va对应的一页文件读入该页面
    if ((pa = kalloc()) == 0)
    {
      p->killed = 1;
      goto end;
    }
    memset(pa, 0, PGSIZE);
    offest = vma->offest + (va - vma->addr);
    ip = vma->file->ip;
    ilock(ip);
    if (readi(ip, 0, (uint64)pa, offest, PGSIZE) == -1)
    {
      kfree(pa);
      iunlock(ip);
      p->killed = 1;
      goto end;
    }
    iunlock(ip);

    // 将虚拟地址映射到物理内存，并设置权限
    // 为了设置脏页，情况1要去除PTE_W标志
    flags |= PORT2PTE(vma->port);
    if(ret == 1){
      flags &= (~PTE_W);
    }
    if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, flags) != 0)
    {
      kfree(pa);
      p->killed = 1;
      goto end;
    }
  }
  else
  {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

end:
  if (p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2)
    yield();

  usertrapret();
}
```

### （六）其它函数的小修改
最后完善些小工作，本实验就收尾了

**1.修改`exit`**
exit退出进程时，要解除所有mmap映射，并清空VMA
调用上文的`munmap_write`即可，这就是集成为函数的复用性。
```c
void
exit(int status)
{
  // ...
  
  // 解除所有MMAP页面，清空VMA
  for (i = 0; i < NVMA; ++i){
    vma = &p->vma[i];
    if (vma->addr)
    {
      munmap_write(vma, vma->addr, vma->length);
      fileclose(vma->file); // 关闭文件
      memset(vma, 0, sizeof(struct mmap_vma)); // 清空VMA
    }
  }
  
  // ...
}
```
**2.修改fork**
子进程复制父进程的VMA即可。
这里并未实现hints中的共享页面，因为这基本是COW实验的内容，并无新意，照搬上去会显得代码臃肿

```c
int
fork(void)
{
  // ...

  // 复制VMA
  for (i = 0; i < NVMA; ++i)
  {
    if (p->vma[i].addr)
    {
      np->vma[i] = p->vma[i];
      filedup(np->vma[i].file);
    }
  }

  // ...
}
```
## （三）感言
mmap算是pagetable后最难的一个实验了，但有了虚拟内存与文件系统的基础，做下来并没有做pagetable时那么吃力，大概也成长了许多吧。
还有一个原因是此次的mmaptest非常简单易懂，使得代码的调式也简单了不少，很容易定位到出错的地方。

此番MIT6.S081实验之旅差不多也要结束了，剩下的Network实验不算难，此次基本算是一个小收尾了。

差不多整个暑假都在all in这个课程，下学期升入大三应该不会有这么多时间啦，该是找实习的时候了。
