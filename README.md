本次实验做起来相对轻松，因为内容简单，而且经过前两次实验的洗礼，已经对xv6中虚拟内存有了更深的理解。

## （一）前置知识：Page faults
### （1）page fault类型
- Load Page Faults： 无法转换的虚拟地址位于一条加载（读）指令中。Scause：13；
- Store Page Faults： 无法转换的虚拟地址位于一条存储（写）指令中。Scause：15；
- Instruction Page Faults： 无法转换的虚拟地址位于一条执行指令中。Scause：12；

page fault中的重要寄存器：
- scause寄存器：缺页错误**类型码**
- stval寄存器：错误的**虚拟地址**
- sepc：引发错误的**指令**所在的地址

### （2）写时复制（COW-fork）
- **来源**：父进程fork出子进程后，常常执行exec替换内存，故子进程拷贝的父进程内存绝大部分是多余的，但是直接共享内存会导致父子进程的写干扰，这时就需要利用缺页错误来实现灵活的共享内存。
- **原理**：开始时父子进程共享内存页，但是都为只读模式 -> 子进程尝试往某页写入 ，出现page fault -> 内核复制该页并为子进程添加映射 ->对父子进程开放读写权限并更新进程页表 ->返回导致缺页的指令重新运行；
只有当子进程真正需要该页时，才触发page fault进行内存拷贝，否者保持共享。
- **注**：父进程往往有多个子进程，每个物理页面会维护一个ref表示共享进程数。每有一个进程需要释放，则`--ref`，直到ref为0才真正释放

### （3）懒惰分配（Lazy Allocation）
- **来源**：程序往往申请比所需内存数更大的内存，比如矩阵（int a[N][N]）
- **原理**：用户调用sbrk申请内存 -> sz=sz+n，增长虚拟地址，但不实际分配物理内存，而是设置这些页无效-> 访问未分配的无效页，page fault -> 分配物理内存并更新页表。

### （4）按需调页（Demand Paging）
- **原理**：进程在分配内存时，只完成虚拟地址的分配，只有在触发页面错误后，才进行物理内存的分配，加载并重新映射pte；在页面错误时，把页面从文件读取到内存中，重新映射并运行原始内存；本质是对于内存的节省。
- **页面置换**：进程需要装载页进入内存，但是内存不足时，内核逐出evict物理页到磁盘并标记无效 -> 其他进程读取该页时，触发缺页异常，产生缺页错误 -> 再从磁盘读入到内存, 改写PTE为有效后更新页表 -> 重新执行读写指令。
- **页面驱逐策略**：LRU（Least Recently Used），优先驱逐非脏页。

### （5）按需补零（Zero Fill on Demand）
- **原理**：对于全局变量而言，由于初始化全为0，因此只需要在单一页物理地址补0（只读），并将所需要的BSS中全部映射到这一页。在调用这些全局变量时，利用缺页错误进行重新申请分配。其本质是推迟花费；
- **注**：按需补零，只是将一些内存分配操作推迟到了处理 page fault 时, 而由于会触发 trap 进入内核, 因此会有额外的存储开销和性能开销

### （6）内存映射文件（Mmap）
- **概述**：将完整或部分文件加载到内存中, 通过对内存相关地址的读写来操作文件.
- **原理**：一般操作系统会提供 mmap 系统调用. 该系统调用会接受虚拟地址(va), 长度(len), protection, 一些标志位(flags), 打开的文件描述符(fd)和偏移量(offset). 从 fd 对应的文件的 offset 位置开始, 映射长度为 len 的内容到虚拟地址 va, 同时加上一些 protection, 如只读或读写.
- **实现**：一般文件都是懒加载, 通过 Virtual Memory Area, VMA结构记录相关信息. 通过 page fault 将**实际文件内容映射到内存;** 通过 unmap 系统调用将文件映射的脏页**写回文件**.

## （二）Eliminate allocation from sbrk()
注释掉分配物理内存的growproc()函数，只将sz=sz+n即可，实际内存的分配等到page fault再处理

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p = myproc();
  
  if(argint(0, &n) < 0)
    return -1;
  addr = p->sz;
  p->sz += n; 
  // if(growproc(n) < 0)
  // return -1;
  return addr;
}

```
从usertrap()可以看到，原本xv6对其它异常如pgae fault的处理只是简单的打印错误信息，然后杀死异常进程
scause保存错误码
sepc保存发生错误的指令地址
stval保存引发错误的虚拟地址

```c
void
usertrap(void){
	// ...
	else
  {
    // other faults
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
	// ...
}
```

## （三）Lazy allocation
### （1）实验要求
实现**Lazy allocation**的具体功能，在usertrap()中处理page fault的异常，分配物理地址，并映射到出错的虚拟地址上
### （2）unsertrap()中分配物理内存
添加异常的判断条件，当scause==13或15时，处理page fault异常，即为出错的虚拟地址分配物理内存

查阅growproc()中uvmalloc()代码可以得到思路，有几点要注意：
- 虚拟地址va要使用`PGROUNDDOWN(va)`向下对齐到页边界
- 由于sepc未+4，所以处理page fault会继续回去执行出错的指令，符合要求
- 参考了几个大佬的代码，都没有像growproc()一样直接调用uvmalloc()，而是直接重写了一个类uvmalloc()函数，调用kalloc()和mappages()分配物理内存。
但个人实测，**直接调用uvmalloc()也是可以通过测试**的。不直接使用uvmalloc()可能是lab hints中明确指出了要仿写uvmalloc

这里根据自己的理解，给出两种形式的代码，都能通过测试
1. 直接调用uvmalloc

```c
void
usertrap(void)
{
  // ...
  }else if((which_dev = devintr()) != 0){
    // interruput
  }else if(r_scause() == 13 || r_scause() == 15){
    // page fault
    uint64 va = r_stval();
    va = PGROUNDDOWN(va); // 记得对齐！
    // 直接调用uvmalloc分配一页的内存
    if (uvmalloc(p->pagetable, va, va + PGSIZE) == 0)
      p->killed = 1;
      
    // sepc未+4，结束后会继续执行原指令
  }else
  {
    // other faults
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  // ...
}
```
2. 仿写uvmalloc()

```c
void
usertrap(void)
{
  // ...
  }else if((which_dev = devintr()) != 0){
    // interruput
  }else if(r_scause() == 13 || r_scause() == 15){
    // page fault
    uint64 va = r_stval();
    char* pa = 0;
    // printf("page fault, va=%p\n", va);
    // 分配物理内存
    if((pa = kalloc()) != 0)
      memset(pa, 0, PGSIZE);
    else p->killed = 1;
  
    // 添加映射 
    if (!p->killed && mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, PTE_W | PTE_R | PTE_U) != 0)
    {
      kfree(pa);
      p->killed = 1;
    }

    // sepc未+4，结束后会继续执行原指令
  }else{
    // other faults
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  // ...
}
```

### （3）修复uvmunmap()
uvmunmap()会出发panic，导致内核奔溃。
触发panic的原因是释放pte时，pte不存在或者无效。但在我们的lazy alloction中，由于物理内存只在用到时才分配，所以释放页表时，也可能包含很多还没有分配到物理内存的pte，这完全是意料之中的事情，故不应该panic，直接continue即可。

对于uvmunmap()中出错的页表有两种情况，都需要continue
1. pte==0
`pte = walk(pagetable, a, 0))` ，walk的作用是遍历三级页表，返回最后一级，也就是L0的pte。
出现pte为0的情况，是因为根本**不存在**L0级的页表，也就更不会有页表项。
而L0级页表不存在的原因，是L1级的pte无效或页表不存在（L2只有一张，一定是存在的），所以无法得到最后一级页表的基址，L0页表也就不存在了
L1级pte无效或页表不存在的原因，是因为sbrk()分配了很大的内存,va很高，因此从L2跳转到了新的L1上，但该L1级页表因为lazy alloction还没有分配物理内存
2. (*pte & PTE_V) == 0
这表示L0级的pte无效，说明页表基址存在了吗，但该pte还没有指向有效的物理内存

总结一下：pte==0表示L0级的页表不存在，说明L1级的pte无效或L1级页表不存在，(*pte & PTE_V) == 0说明L0级的页表存在，但pte无效
这两种情况都要continue

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    // L1级页表项未映射或页表不存在,根本不存在第三级页表
    if((pte = walk(pagetable, a, 0)) == 0)
      continue;
    // 最后一级pte未映射
    if((*pte & PTE_V) == 0)
      continue;
    if (PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}
```

## （四）Lazytests and Usertests
### （1）实验要求
修复lazy alloction中存在的一些bug，比如内存不够，va超出堆的范围等等
### （2）实验步骤
**1.** sbrk()的负参数问题
释放相应内存即可，和原growproc()中做得事一样。
释放无效内存的问题已经在uvmunmap中解决了。

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p = myproc();

  if(argint(0, &n) < 0)
    return -1;
  addr = p->sz;
  // 处理n为负的情况，释放内存
  if(n < 0){
    uvmdealloc(p->pagetable, p->sz, p->sz + n);
  }
  p->sz += n;

  return addr;
}
```
**2.** 处理虚拟地址越界问题
一共有两点：高于最大分配内存p->sz的，和低于栈顶p->trapframe->sp的。我们的动态内存时分配在**堆**上的，所以va必须要在这个范围内

```c
if(va >= p->sz || va < p->trapframe->sp)
      p->killed = 1;
```

**3.** 处理fork()调用uvmcopy()导致panic的问题
同uvmunmap一样，pte==0和pte无效的两种情况要跳过

```c
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if ((*pte & PTE_V) == 0)
      continue;
```
**4.** 处理内存不足,kalloc()失败的问题

```c
    // 或杀死分配物理地址失败的进程,分配成功则置零
    if(!p->killed && (pa = kalloc()) != 0)
      memset(pa, 0, PGSIZE);
    else p->killed = 1;
```
如果是直接调用uvmalloc()，则不存在这个问题

综上所述，usertrap()对page faults的完整的处理代码如下，同样给出两个版本
1. 直接调用uvmalloc()

```c
else if(r_scause() == 13 || r_scause() == 15){
    // page fault
    uint64 va = r_stval();
    // printf("page fault, va=%p\n", va);
    
    // 杀死va高于分配内存，或杀死va低于用户栈的进程
    if(va >= p->sz || va < p->trapframe->sp)
      p->killed = 1;
    // 或杀死分配物理地址失败的进程,分配成功则置零
    va = PGROUNDDOWN(va);
    if (!p->killed && uvmalloc(p->pagetable, va, va + PGSIZE) == 0)
      p->killed = 1;

    // sepc未+4，结束后会继续执行原指令
  }
```

2. 仿写uvmalloc()
```c
else if(r_scause() == 13 || r_scause() == 15){
    // page fault
    uint64 va = r_stval();
    char* pa = 0;
    // printf("page fault, va=%p\n", va);
    
    // 杀死va高于分配内存，或杀死va低于用户栈的进程
    if(va >= p->sz || va < p->trapframe->sp)
      p->killed = 1;
    // 或杀死分配物理地址失败的进程,分配成功则置零
    if(!p->killed && (pa = kalloc()) != 0)
      memset(pa, 0, PGSIZE);
    else p->killed = 1;
 
    // 添加映射 
    if (!p->killed && mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, PTE_W | PTE_R | PTE_U) != 0)
    {
      kfree(pa);
      p->killed = 1;
    }

    // sepc未+4，结束后会继续执行原指令
}
```
这里有多个if语句，注意用p->killed==1判断此时进程是否已被杀死，杀死了就不能进入if中判断了。
比如说在`va >= p->sz`的情况下，忘记加if(!p->killed)而直接进行if((pa = kalloc()) != 0)，就会给错误的虚拟地址va分配内存，最终panic



**5.** 解决系统调用read()/write()访问未分配的物理内存问题
系统调用时处于**内核态**，而page fault是属于从用户空间陷入的异常，故此时访问错误页表不会产生page fault，所以要重新分配内存。
根据read()/write()向下追踪，可知最终是在copyin()/copyout()中使用walkaddr访问了错误的页表
那么修改walkaddr()，即可，修改方法可以完全照搬usertrap()中pagefault的处理方法，为错误的页分配物理内存即可。
**注意**：使用proc信息，要在vm.c中添加头文件

```c
#include "spinlock.h"
#include "proc.h"
```

```c
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_V) == 0){
    // 此处copyin/copyout调用walkaddr，位于内核态，不会发生pagefault
    // 所以要自己手动分配内存
    struct proc *p = myproc();
    // 杀死va高于分配内存，或杀死va低于用户栈的进程
    if(va >= p->sz || va < p->trapframe->sp)
      return 0;
    // 或杀死分配物理地址失败的进程,分配成功则置零
    if((pa = (uint64)kalloc()) == 0)
      return 0;
    memset((void*)pa, 0, PGSIZE);
    // 添加映射 
    if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, pa, PTE_W | PTE_R | PTE_U) != 0)
    {
      kfree((void*)pa);
      return 0;
    }
  }else if((*pte & PTE_U) == 0){
    return 0;
  }else{
    pa = PTE2PA(*pte);
  }

  return pa;
}

```
## （六）总结
Page faults的应用：
- 写时复制
- **懒惰分配**
- 按需调页
- 按需补零
- 内存映射文件
