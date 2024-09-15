问题的引入：从上一节的系统调用我们知道，用户进程如果要获取内核的数据，必须通过copyout函数实现传递，同理，内核获取用户进程数据也需要通过copyin函数。然而，这样的方法是通过软件方面实现的，有没有更高效的方式呢？

有——那就是通过页表。

我们都知道，页表是通过硬件遍历寻址的，MMU，TLB也都是硬件支持，所以速度回更快。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/fcafe86f4a8743e895a223a40956fc6f.png#pic_center)
## （一）前置知识：页表详解
### （1）页表映射
- 虚拟地址：
RISC-V中，地址是64位的，但仅用低位39位表示虚拟地址。39位的虚拟地址中，27位为页表项（PTE）的编号，12位为offest
- PTE：
内存中，每个PTE的自身大小占4B
PTE由物理块号（PPN）和flags构成。
每个物理块大小为2^12字节（页长）=4KB。物理块号对应物理内存中高44位的地址，物理块号的44位+offset（在物理块中对于物理块起始地址的偏移量）的12位构成了一个物理地址。
- 虚拟地址到物理地址的映射关系：
虚拟地址=PTE编号+offset
PTE编号+页表基地址->物理块号
物理块号+offset=物理地址
- 寻址过程：
(1)MMU从satp寄存器中获取页表的基地址
(2)从虚拟地址中取出PTE编号与offest，根据页表的基地址，MMU在内存中找到该页表，然后遍历页表，由PTE编号找到相应的PTE
(3)从PTE中取出PNN，如果是单级页表，则PNN+offest则为最终物理地址，如果是多级页表，则PNN作为下一级页表的基地址
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/8a5d75f641fa48c195240da62779801c.png#pic_center)
### （2）多级页表
- 多级页表的优势
多级页表的好处是节约页表本身大小所占用的内存
RISC-V中，每级页表占用8bit，即每级页表维护2^8=512个页表项。
查找一个映射，从L2->L1->L0，只需要访问3*512的条目。而如果只有单级页表，一页中包含2^27个条目，需要全部放入内存中。
- 三级页表
SV39中，每个进程维护一张用户地址空间页表和一张内核地址空间页表，每个页表都是三级的。
高地址为内核空间，低地址为用户空间
- 三级页表的映射过程
1.逻辑地址划分：
|  三级     |  二级    |  一级     | 偏移量 |
|     L2     |    L1     |     L0     | Offset |
|  9bits   |  9bits    |  9bits   | 12bits |
2.从satp寄存器中取出三级页表的基地址A3
计算三级页表条目地址(PTE)：A3+L2x4 (每个PTE占4字节)
读取该PTE对应的物理块号PNN，读取结果作为二级页表的基地址A2
3.获取二级页表的基地址A2
计算二级页表条目地址(PTE)：A2+L1x4 (每个PTE占4字节)
读取该PTE对应的物理块号PNN，读取结果作为一级页表的基地址A1
4.获取一级页表的基地址A1
计算一级页表条目地址(PTE)：A1+L0x4 (每个PTE占4字节)
读取该PTE对应的物理块号PNN，读取结果作为最终物理页面的基地址A0
5.最终物理地址为：A0+offest
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/8b3269de1c604444a092d51a703f7362.png#pic_center)
哦对了，说明一下，u开头的函数代表用户进程的，k开头的函数代表内核态的，其中有vm的就是和虚拟内存相关的
## （二）Print a page table
### （1）实验要求
写一个函数vmprint(pagetable_t pagetable)，用于打印页表，包括页表深度，PTE与PA（物理地址，其实可以理解为PNN）
### （2）实验思路
按照提示，分析一下freewalk

```c
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}
```
- **pagetable**：页表基地址。**i**：PTE编号。**pte（pagetable[i]）**：PTE，由PNN与flag组成。**child**：下一级页表的基地址，如果已经是一级页表了，那就是最终物理地址。**PTE2PA**：将PNN转为物理地址，原型为#define PTE2PA(pte) (((pte) >> 10) << 12)，可以看到，实际上就是清除了PTE的标志位，然后扩展成一个56位的物理地址（PNN本身44位）
- **pte & PTE_V**：判断该PTE是否valid
- **pte & (PTE_R | PTE_W | PTE_X)**：判断PTE是否可读、写、或执行。通过这个条件可以判断是否是***最后一级页表***，因为只有最后一级的pte才对应最终物理地址，一定满足读写可执行之一的条件，而第二、第三级的pte对应的只是下一级页表的索引地址，不需要读写或可执行
- 页表的三级遍历由递归实现

###  （3）实验代码
根据上述分析，容易给出代码。
因为要打印起始页表的地址，不适合放入递归里写，所以拆成了两个函数。
注意，打印的.. .. ..表示的是页表的深度而非级数，事实上是从三级页表开始索引，但深度是一。
```c
void vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);

  printwalk(pagetable, 1); // 递归打印页表
}

void printwalk(pagetable_t pagetable, int depth)
{
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if (pte & PTE_V)
    { // PTE合法
      uint64 child = PTE2PA(pte);
      // 按format打印level，PTE与PA
      switch (depth)
      {
      case 1:
        printf("..");
        break;
      case 2:
        printf(".. ..");
        break;
      case 3:
        printf(".. .. ..");
        break;
      }
      printf("%d: pte %p pa %p\n", i, pte, child);

      // 只有在页表的最后一级，才可读写或可执行
      // 若不在最后一级，则继续递归
      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0)
        printwalk((pagetable_t)child, depth + 1);
    }
  }
}
```

最后，在exec.c里添加调用

```c
	...
  if(p->pid == 1)
    vmprint(p->pagetable);

  return argc; // this ends up in a0, the first argument to main(argc, argv)
```

## （三）A kernel page table per process
### （1）实验要求
xv6原本的设计是，每个用户进程维护各自的用户页表，但当进入内核态时，所有进程都切换到**同一张**内核页表，这张内核页表是全局共享的
该实验的目的是让每个进程**都有一张属于自己的**内核页表，这一步的作用会在下一个实验揭晓。
### （2）实验步骤 
**1.**在PCB中添加内核页表

```c
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
->pagetable_t kernelpagetable; // kernel page table for per process
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
```
**2.** 初始化内核页表函数 **proc_kvminit()**
先来看看 **kvminit()** 函数，它是初始化 **全局页表** 的

```c
void kvminit()
{
  kernel_pagetable = (pagetable_t)kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}
```
结合这个函数，再来看一下内核的地址空间
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/31500453e35d442cbaa1a2fc6aa00123.png#pic_center)
- kvminit()为内核页表创建了虚拟地址到物理地址的映射，包括一系列的恒等映射，如text段，data段，直到内核虚拟地址的上限TRAMPOLINE。
- 由于 xv6 支持多核/多进程调度，同一时间可能会有多个进程处于内核态，所以需要对所有处于内核态的进程创建其独立的内核态内的栈
- 由于这张页表是全局的，所以在一张表中为每个进程都分配了内核栈,Kstack0，Kstack1，映射到不同的物理地址。
- 各个栈之间由Guard守护，防止别的进程越界，一旦越界，会发生page fault错误

很显然，我们可以仿照kvminit()写一个为每个进程分配内核页表的函数，但有几点要注意：
- kvmmap()默认为全局页表创建映射，我们要为每个进程创建映射，则需要重写一个函数，传入进程各自的kernelpagetable
- 映射CLINT的部分需要注释掉，这点做完下一个实验就会明白

代码如下：

```c
// 为每个进程的内核页表添加映射
void proc_kvmmap(pagetable_t kernelpagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kernelpagetable, va, sz, pa, perm) != 0)
    panic("proc_kvmmap");
}
```

```c
pagetable_t proc_kvminit()
{
  pagetable_t kernelpagetable = (pagetable_t)kalloc();
  memset(kernelpagetable, 0, PGSIZE);

  // uart registers
  proc_kvmmap(kernelpagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  proc_kvmmap(kernelpagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 注释掉，腾出空间映射用户页表，避免冲突
  // CLINT
  //proc_kvmmap(kernelpagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  proc_kvmmap(kernelpagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  proc_kvmmap(kernelpagetable, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  proc_kvmmap(kernelpagetable, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  proc_kvmmap(kernelpagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  return kernelpagetable;
}

```
然后在allocproc()中调用

```c
static struct proc*
allocproc(void)
{
// An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 进程的内核页表初始化
  p->kernelpagetable = proc_kvminit();
  if(p->kernelpagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
}
```
**3.** 为进程的内核页表添加到内核栈的映射
前面初始化内核页表添加了一系列映射，但并不包括到内核栈的映射，因为内核栈的内容实际上是每个进程独有的。
查看procinit()中添加内核栈映射的代码，迁移到allocproc中并略作修改

procinit()中：

```c
// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      // Allocate a page for the process's kernel stack.
      // Map it high in memory, followed by an invalid
      // guard page.
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (p - proc));
      kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;
  }
  kvminithart();
}

```
迁移到allocproc()中：

```c
  // 申请内核栈，然后将进程的内核页表映射到内核栈(kernel stack)
  char *pa = kalloc();
  if (pa == 0)
    panic("kalloc");
  // 由于每个进程都有自己的内核栈了，所以这里可以将内核栈映射到固定的逻辑地址上
  uint64 va = KSTACK(0);
  proc_kvmmap(p->kernelpagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  p->kstack = va;


  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
```

 - kvmmap替换成proc_kvmmap
 - 前文提到，由于所有进程共享一张内核页表，所以不同进程的内核栈位于不同的虚拟地址。但此处每个进程都有属于自己的内核页表了，一张表只有一个内核栈，所以虚拟地址自然也可以固定下来
 - **特别注意**的是，添加内核栈映射的代码迁移到了allocproc中，那么procinit中相应的代码就可以删除了。

 **4.**  修改进程调度函数scheduler()
scheduler()是CPU的进程调度函数，当进程切换时，同时也要将相应内核页表加载入satp寄存器，当没有进程运行时，则加载全局页表。

**kvminithart()** 函数将全局页表加载入satp寄存器，同理可以写出**proc_kvminithart()** 用于将不同进程的内核页表加载
**sfence_vma()** 的作用是更新页表后，刷新快表

```c
void proc_kvminithart(pagetable_t kernelpagetable)
{
  w_satp(MAKE_SATP(kernelpagetable));
  sfence_vma(); // 刷新快表
}
```

修改scheduler()

```c
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    
    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // 将内核页表装载入satp寄存器
        proc_kvminithart(p->kernelpagetable);
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;

        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        // 由上两行源代码注释可知这时进程已经结束运行
        // kvminithart()为何放在这里而不放在found==0处，个人认为是这里"no process is running"的范围更广
        kvminithart(); // 装载全局内核页表

        c->proc = 0;

        found = 1;
      }
      release(&p->lock);
    }
#if !defined (LAB_FS)
    if(found == 0) {
      intr_on();
      asm volatile("wfi");
    }
#else
    ;
#endif
  }
}
```
- 这里易错点是切换回全局页表的时机，很容易误认为是当found==0时切换。但实际，当swtch(&c->context, &p->context);调用后，该进程的运行就已经结束了，此时没有任何进程运行，所以应该在这里切换回全局页表

**5.** 释放内核栈与内核页表
需要在进程回收函数**freeproc**中回收内核栈与内核页表。有几点要注意：
- 先释放内核栈，才能释放内核页表，若先释放了整个页表的映射，就找不到内核栈的物理内存了
- 内核栈是每个进程所独有的，所以进程销毁内核栈也销毁，包括**页表映射**与**物理内存**
- 内核页表本身是每个进程独有的，但其映射的物理内存其实是整个内核的物理内存，是全局的，所以**不能释放物理内存**，只能释放页表

参考释放用户页表的proc_freepagetable()，其中调用了两个函数：
- uvmunmap()用于释放页表npages页的映射关系，其最后一个参数do_free代表是否释放物理内存
- uvmfree()用于释放页表本身

**释放内核栈：**
do_free设置为1，释放物理内存
```c
if(p->kstack)
    uvmunmap(p->kernelpagetable, p->kstack, 1, 1);
  p->kstack = 0;
```
**释放内核页表：**
当初在proc_kvmmap()中初始化了什么映射，这里就要释放什么映射
do_free设置为0，不释放物理内存。
最后用uvmfree()释放页表本身

```c
// 释放内核页表
void proc_freekernelpgtbl(pagetable_t kernelpagetable) {
    uvmunmap(kernelpagetable, UART0, 1, 0);
    uvmunmap(kernelpagetable, VIRTIO0, 1, 0);
    uvmunmap(kernelpagetable, CLINT, 0x10000 / PGSIZE, 0);
    uvmunmap(kernelpagetable, PLIC, 0x400000 / PGSIZE, 0);
    uvmunmap(kernelpagetable, KERNBASE, (PHYSTOP - KERNBASE) / PGSIZE, 0);
    uvmunmap(kernelpagetable, TRAMPOLINE, 1, 0);
    uvmfree(kernelpagetable, 0);
}
```

```c
if(p->kernelpagetable){
    proc_freekernelpgtbl(p->kernelpagetable);
  }
  p->kernelpagetable=0;
  p->state = UNUSED;
```
**释放内核页表的另一种方法**
也可以参照freewalk()写一个递归函数，遍历页表的每一层然后释放，这样就不用一一枚举映射关系然后手动释放了（还得判断大小）。
但注意，freewalk()只清除了页表的**前两级映射**，所以不能直接调用，得略作修改，清除三级映射

```c
// 清除三级映射并释放页表
void proc_freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V))
    {
      pagetable[i] = 0;
      // this PTE points to a lower-level page table.
      if((pte & (PTE_R | PTE_W | PTE_X)) == 0){
        uint64 child = PTE2PA(pte);
        proc_freewalk((pagetable_t)child);
      }
    }
  }
  kfree((void *)pagetable);
}
```
**6.** 修改 kvmpa() 函数
这点在hints中没有提及，但必须修改。
kvmpa() 函数用于将内核虚拟地址转换为物理地址, 其中调用 walk() 函数时使用了全局的内核页表. 此时需要换位当前进程的内核页表
解决方案是给kvmpa()增加一个页表参数

```c
uint64
kvmpa(pagetable_t kernel_pagetable, uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;

  pte = walk(kernel_pagetable, va, 0);
  if (pte == 0)
    panic("kvmpa");
  if ((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa + off;
}
```
virtio_disk_rw中调用了kvmpa，这里加入每个进程单独的内核页表这个参数
```c
void
virtio_disk_rw(struct buf *b, int write)
{	
	//...
	disk.desc[idx[0]].addr = (uint64) kvmpa(myproc()->kernelpagetable, (uint64) &buf0);
	//...
}
```

## （四）Simplify copyin/copyinstr
### （1）实验要求
可以说前面的实验内容都是在为这一小节做铺垫。
在上一个实验中，已经使得每一个进程都拥有独立的内核态页表了，这个实验的目标是，在进程的内核态页表中维护一个**用户态页表映射的副本**，这样使得内核态也可以对用户态传进来的指针（逻辑地址）进行解引用。
这样做相比原来 copyin 的实现的优势是，原来的 copyin 是通过软件（walk()函数）模拟访问页表的过程获取物理地址，再从物理地址拷贝内容。
而在内核维护用户页表的映射副本的话，可以利用 CPU 的硬件寻址功能进行寻址，效率更高并且可以受快表加速。
- 等于说，将软件层面上虚拟地址到物理地址的转换提升到了硬件层面上，性能大大提高。
- 还有一个性能提升的地方，假设内核需要访问用户程序的结构体p中某个值，如p->a，用copyin需要将整个结构体p都拷贝过来，工作量大。但是如果内核中有该结构体p的映射，那么就可以直接访问p->a了，简化了不少。

### （2）实验步骤
**1.** 修改copyin()与copyinstr()
按照要求替换两个函数的主体即可。
原先的两个函数使用walk()找到用户页表对应的物理地址，解析后再返回给内核空间。
但新的函数直接进行虚拟地址的拷贝即可，因为内核页表拥有了用户页表的副本，维护了到该地址的映射，可以通过硬件直接寻址
```c
// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
}
```
**2.** 编写用户页表拷贝到内核页表的函数**u2kvmcopy()**
- 参考函数uvmcopy()，其来源于fork()。它将父进程的**页表与物理内存**都复制了一份，拷贝到了子进程。但其实我们并不需要拷贝物理内存，只需要拷贝页表即可，内核页表中用户页表的副本，与用户页表指向的是**同一块物理内存**
- 拷贝函数**u2kvmcopy(pagetable_t userpgtbl, pagetable_t kernelpgtbl, uint64 start, uint64 sz)**，表示将用户页表中，从start开始，大小为sz字节的页表拷贝到内核页表中
- hints中写道：A page with PTE_U set cannot be accessed in kernel mode，所以我们需要清除PTE中的PTE_U标志
- 要用**PGROUNDUP**(start)将起始地址变为PGSIZE的整数倍，否则会报错page not present
- 在这里有一点要**特别说明**，内核空间的0~PLIC段是用于拷贝用户页表的,但从内核地址空间的图可以看到，PLIC 的低地址处是有一个 CLINT 部分的, 因此在页表复制是可能会引发重映射, 解决方案是将 Lab3-2 中 proc_kvminit() 函数中为 CLINT 部分创建映射的代码注释掉（这也就是前文中埋下的伏笔）, 因为其存在重映射的可能, 也就从另一方面说明了该部分应该不会被实际映射。

```c
// 拷贝用户页表到内核页表，start为起始位置，sz为大小
// 与uvmcopy不同的是，这里的物理地址不需要重新分配(mem多余)，只要添加一份映射即可
int u2kvmcopy(pagetable_t userpgtbl, pagetable_t kernelpgtbl, uint64 start, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  uint64 start_page = PGROUNDUP(start); // 从整数页开始
  for (i = start_page; i < start + sz; i += PGSIZE)
  { 
    // 不需要kalloc分配空间了
    if ((pte = walk(userpgtbl, i, 0)) == 0)
      panic("u2kvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("u2kvmcopy: page not present");
    pa = PTE2PA(*pte);
    // & ~PTE_U 表示将该页的权限设置为非用户页
    // 必须清除用户页标志，否则内核无法访问。
    flags = PTE_FLAGS(*pte) & (~PTE_U);
    // 用mappages添加内核页表到该物理地址的映射
    if (mappages(kernelpgtbl, i, PGSIZE, pa, flags) != 0)
      goto err;
  }
  return 0;

err:
  uvmunmap(kernelpgtbl, start_page, (i - start_page) / PGSIZE, 0); // 记得第四个参数要设置为0，因为这里没有分配新的物理内存，所以也不需要释放，如果释放，会释放用户页表的物理内存，那就出错了
  return -1;
}

```
接下来，三个修改了用户页表的地方，内核页表也需要同步修改，修改方式就是调用我们刚写好的u2kvmcopy()

**3.** fork()

```c
int
fork(void)
{
	// ...
	// Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 拷贝用户页表到内核页表
->if(u2kvmcopy(np->pagetable, np->kernelpagetable, 0, np->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  np->parent = p;
  // ...
}
```
**4.** exec()
exec是在原进程的基础上**替换**成新的进程（本质上还是同一个进程，不过PCB的信息都替换了），所以要先解除原进程kernelpagetable的映射,再将新进程pagetable拷贝到新进程kernelpagetable上，不先解除映射的话就会发生冲突了。

```c
int
exec(char *path, char **argv)
{
  // ...  
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  // 可知exec是把原进程替换成新的进程，所以要先解除原进程kernelpagetable的映射
  // PGROUNDUP向上取整到PGSIZE的整数倍
  uvmunmap(p->kernelpagetable, 0, PGROUNDUP(oldsz)/PGSIZE, 0); // do_free设置为0，清除映射即可
  if(u2kvmcopy(p->pagetable, p->kernelpagetable, 0, p->sz) < 0)
    goto bad;

  if (p->pid == 1) {
    vmprint(p->pagetable);
  }
  // ...
}

```
**5.** sbrk()
sbrk() 函数即系统调用 sys_brk() 函数, 最终会调用 kernel/proc.c 中的 growproc() 函数, 用来增长或减少虚拟内存空间
有几点要修改：
- 用户空间的范围在0~PLIC中，所以在n>0时要保证sz + n <=PLIC
- n>0 时, 在 uvmalloc() 分配新的内存后, 要将新增的用户地址空间使用 u2kvmcopy() 复制到内核页表
- n<0 时, 则需要在 uvmdealloc() 之后将内核页表的这部分空间解除映射。 需要注意的是, uvmdealloc() 底层调用的 uvmunmap() 函数是连同物理地址一起释放了, 因此在这里不能直接调用该函数释放内核页表, 这会导致物理地址重复释放。故重写了一个kvmdealloc()函数，调用uvmunmap() 但将do_free置为0

**kvmdealloc()函数：**
```c
// 与 uvmdealloc 功能类似，将程序内存从 oldsz 缩减到 newsz。但区别在于不释放物理内存
uint64
kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
  }

  return newsz;
}
```

**修改后的growproc()函数：**

```c
// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > PLIC)
      return -1;
    if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
    {
      return -1;
    }
    if(u2kvmcopy(p->pagetable, p->kernelpagetable, p->sz, n) < 0)
      return -1;
  }
  else if (n < 0)
  {
    uvmdealloc(p->pagetable, sz, sz + n);
    sz = kvmdealloc(p->kernelpagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}
```
**6.** 修改 userinit()
userinit() 函数用于初始化 xv6 启动时第一个用户进程, 该进程的加载是独立的, 因此也需要将其用户页表拷贝到内核页表

```c
// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;
  // 拷贝用户页表到内核页表
  u2kvmcopy(p->pagetable, p->kernelpagetable, 0, p->sz);

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}
```
## （五）总结
**1.** 详细了解了页表  
**2.** 虚拟内存相关知识，空间分配、回收、映射  
**3.** 实现了页表的新功能，从全局内核页表到独立内核页表的替换  
**4.** 简化了用户进程与内核之间的数据传输，将其升级为硬件方式  
