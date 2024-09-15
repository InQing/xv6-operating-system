好消息，本次lab6只有一个小实验
坏消息，一个顶俩

## （一）COW Fork
- 来源：父进程fork出子进程后，99%的情况下会执行exec替换内存，故子进程拷贝的父进程内存绝大部分是多余的。一个办法是直接共享父子进程的内存，但这会导致对共享内存的**写**干扰，这时就需要利用**page fault**来实现灵活的共享内存。
- 原理：父子进程保持共享内存，只有当子进程需要**写**该页时，才触发page fault，子进程进行内存拷贝，否者维持共享
- 步骤：开始时父子进程共享内存页，但是都为**只读模式** -> 子进程尝试往某页写入 ，触发page fault -> 内核复制该页并为子进程添加映射 ->对**子进程**开放该复制页的**写权限** ->返回导致page fault的指令重新运行；
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/523ba9fbddc14e9ab357560e8c0f5f5c.png#pic_center)
## （二）Implement copy-on write
### （1）整体思路
- 修改uvmcopy()，将内存拷贝该为内存共享
- 修改usertrap()和copyout()，处理page，fault，处理方法为复制该页，重新映射
- 添加对共享内存页的计数，只有当计数为0时才真正释放该页
- 用RSW标志位记录是否为COW的共享页面
- 注意读/写权限的更改

### （2）共享页计数结构
由于在Cow fork中，每个物理页面有n个进程共享，所以要为每个页面维护一个cow_ref==n，当该页面有新进程共享时，++cow_ref。发生page fault后，复制页面，该进程映射到了新的页面，故原页面的共享进程数--cow_ref。当cow_ref == 0时候，才表示这个页面已经没有映射，需要销毁。
因为后面步骤都要涉及共享页的计数，所以率先编写计数结构。
**1.计数数组**
很显然，计数结构是一个数组，对每个物理页面，维护共享进程的个数，
要考虑的问题如下：
- 如何通过数组下标索引到物理页面
- 数组的大小如何选取
- 数组的类型
- 数组放在哪里好

*下标：*
下标索引好说，hints中已经给出，可以由pa/PGSIZE索引。

*数组大小：*
接着找到kinit()里freerange的范围，可以看到物理页面（RAM）的地址范围是end~PHYSTOP，但是end的注释为first address after kernel，随内核代码段和数据段的大小而变化，是个变量，不能用于确定数组的大小。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/3bca3665793d4e889b3b03dd89f541ea.png#pic_center)
所以，这里我们将物理页面的范围看作最大值：KERNBASE~PHYSTOP，那么数组的大小就是`((PHYSTOP - KERNBASE) / PGSIZE)`。

*数组类型：*
考虑到数组的大小很大，为了尽可能的节约内存，我们想让数组的类型所占字节尽可能的小。
在`param.h`中定义了最大进程数

```c
#define NPROC        64  // maximum number of processes
```
那么共享进程数就一定小于64，uint8就可以满足我们的需求。

*数组位置：*
关于数组放在哪里，它显然是一个内核的全局变量，可以放在vm.c中，在要用到的地方用extern声明，但这其实不是最好的方案，我们稍后再讨论。

**2.计数的锁结构**
计数数组是一个**临界资源**，访问时要加锁（设计锁的保守性原则）。参考对空闲链表的加锁方案，可以加自旋锁维护每个计数数组。
将锁与计数整合到一个结构体中，如下就有了我们的计数结构：

```c
struct COW{
    uint8 cow_ref;
    struct spinlock lock;
} cow[(PHYSTOP - KERNBASE) / PGSIZE];
```
对每个锁需要初始化，添加初始化函数`initlock_cow()`
```c
void 
initlock_cow(void){
    int i;
    for (i = 0; i < (PHYSTOP - KERNBASE) / PGSIZE; ++i)
        initlock(&cow[i].lock, "cow");
}
```
初始化可以和空闲链表的锁结构初始化放在一起，也就是`kinit()`中

```c
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock_cow();
  freerange(end, (void *)PHYSTOP);
}
```

**3. 计数结构的增减**
自增自减前都需要上锁，这里设计成了两个函数。有人可能会觉得设计成函数多此一举，别急，接着看下面的4.设计思路

```c
void 
inc_cowref(uint64 pa){
    pa = (pa - KERNBASE) / PGSIZE;
    acquire(&cow[pa].lock);
    ++cow[pa].cow_ref;
    release(&cow[pa].lock);
}

uint8
dec_cowref(uint64 pa){
    uint8 ref;
    pa = (pa - KERNBASE) / PGSIZE;
    acquire(&cow[pa].lock);
    ref = --cow[pa].cow_ref;
    release(&cow[pa].lock);
    return ref;
}
```

**4.设计思路**
有个问题貌似很棘手，计数结构体与设计的这些函数放在哪呢？vm.c中？vm.c中是存放虚拟内存相关函数的，虽说COW也是属于虚拟内存，但放进去，和里面的一系列函数总感觉很违和。
联想起C++面向对象的设计思路，这里的COW其实不就是一个类吗？`cow[(PHYSTOP - KERNBASE) / PGSIZE]`是类的私有数据成员，`initlock_cow(void)`、`inc_cowref(uint64 pa)`、`dec_cowref(uint64 pa)`三个函数是三个对外的接口，成员函数。
计数结构是私密的，只能通过对外接口来初始化和访问，具有良好的封装性。
C中没有类，有结构体，结构体中虽然也能存放我们的计数结构和三个函数，但显得有些臃肿了。我们不妨将它们都置于一个cow.c文件中，对外提供头文件，也能实现我们的封装性。
故最终，cow.c如下：

```c
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"

struct COW{
    uint8 cow_ref;
    struct spinlock lock;
} cow[(PHYSTOP - KERNBASE) / PGSIZE];

void 
initlock_cow(void){
    int i;
    for (i = 0; i < (PHYSTOP - KERNBASE) / PGSIZE; ++i)
        initlock(&cow[i].lock, "cow");
}

void 
inc_cowref(uint64 pa){
    pa = (pa - KERNBASE) / PGSIZE;
    acquire(&cow[pa].lock);
    ++cow[pa].cow_ref;
    release(&cow[pa].lock);
}

uint8
dec_cowref(uint64 pa){
    uint8 ref;
    pa = (pa - KERNBASE) / PGSIZE;
    acquire(&cow[pa].lock);
    ref = --cow[pa].cow_ref;
    release(&cow[pa].lock);
    return ref;
}
```
Makefile中将cow.c加入编译

```c
OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printf.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/virtio_disk.o \
  $K/cow.o
```
def.h中添加原型

```c
//cow.c
void            initlock_cow(void);
void            inc_cowref(uint64);
uint8           dec_cowref(uint64);
```

### （3）COW标志位
hints中提到，可以用PTE中的RSW位作为COW页面的标记
RSW位即为“软件预留位”，提供给软件，用来标记额外需要的信息，一共有两位，我们取一位即可。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/2e1fffda3c4c4bdf8485da9078b34438.png#pic_center)
按照规范，riscv.h中define一下

```c
#define PTE_COW (1L << 8) // flag for COW folk
```

### （4）修改 uvmcopy()
COW中，uvmcopy()并不实际分配内存，只是共享内存，并且修改标志位为只读，等待page fault后再实际拷贝。
- 注释掉分配物理内存的代码
- 为父子进程都添加PTE_W与PTE_COW标志位
- `inc_cowref(pa)`，表示该页面共享进程数+1

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  // char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    *pte = (*pte & ~PTE_W) | PTE_COW; // 将父子进程的pte置为不可写,同时添加COW标志位
    flags = PTE_FLAGS(*pte);
    inc_cowref(pa);
    // 注释掉分配物理内存的代码
    // if((mem = kalloc()) == 0)
    //   goto err;
    // memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

```
### （5）构造物理内存分配函数
由lazy alloction的实验我们知道了，有两个地方需要重新分配物理内存：一个是发生page fault后在usertrap()中捕捉到，另一个是在系统调用中，这里不会发生page fault，要手动捕捉页面异常并分配内存。

但需要特别注意，本实验中只捕捉**写入**标志为**COW页面**的异常，从而引出一个问题：

lazy alloction实验中，对于系统调用的异常场景，我们修改的是walkaddr()函数。但此处只有copyout()是 **写入** COW 页面，需要处理COW异常，若修改了walkaddr()，则copyin()等一类函数调用walkaddr()时，存在将其它情况误判成写入COW页面异常的风险。比如copyin()中 **读** COW页面也会被判异常从而分配新内存，因为在walkaddr中无法区别到底是读还是写（对于系统调用，没有page fault，r_scause()也不会保存错误的指令类型）

显然，这并不是我们想看到的，故处理方式是：构造一个全新的`walkaddr_cow()`函数，只在**写入页面时**调用。更具体来说，只在`usertrap()`中`r_scause() == 15`的情况与`copyout()`中调用。


综上所述，让我们来明确一下`walkaddr_cow()`函数的作用：
1. 只处理COW页面的写入异常（只判断是否为COW页面，是否为**写入**交给函数外判断）：1.解除旧映射。2.分配新的物理内存并添加新映射。3.添加PTE_W并删除PTW_COW标志。
2. 难免捕捉到写入其它页面的异常（比如写入无PTE_V标志的页面），由于朴素xv6并不处理这些异常，只是单纯地杀死进程，所以该函数要做的也只是返回0表示失败。
3. 为了copyout()的需要，返回新分配的物理地址

如上，可以给出`walkaddr_cow()`函数的代码：

```c
// 如果是COW页面，则复制物理内存并映射
// 返回va对应的物理地址pa
// 0表示失败
uint64
walkaddr_cow(pagetable_t pagetable, uint64 va){
  uint64 pa;
  pte_t *pte;
  uint flags;
  char *mem;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);

  // 如果是COW页面
  if((*pte & PTE_COW) != 0){
    if((mem = kalloc()) == 0)
      return 0;
    memmove(mem, (void *)pa, PGSIZE);
    
    // 置为可写并清除COW标记
    flags = (PTE_FLAGS(*pte) | PTE_W) & ~PTE_COW;
    // 注意最后再取消映射，否则前面释放了pte和pa，这里就要操作空内存了
    uvmunmap(pagetable, PGROUNDDOWN(va), 1, 1);
    if(mappages(pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      return 0;
    }
    return (uint64)mem;
  }

  return pa;
}
```
- 请注意，解除旧映射要在操作完pte最后进行，若先解除了映射，pte置为0，那就是操作无效内存了
- uvmunmap()的do_free参数置为1，其实就相当于dec_cowref(pa)，只不过我们将操作放在了kree()中做
- 注：观看视频lec12后补充一种写法，不需要解除旧映射并添加新映射，直接修改pte即可(但仍然要kfree一下物理页面)，如下：
```c
*pte = PA2PTE((uinta64)mem) | PTE_W | PTE_R | PTE_X | PTE_V | PTE_U;
kfree(pa);
```
kfree()经修改后（后文才会修改kfree，但hints中已经提到kfree的修改方式），实际意义是减少页面的引用次数ref，只有当ref为0时页面才会真正被释放。
假设此处有父子两个进程共享同一页面，那么ref就是2。当写时复制发生时，运用“直接修改pte”的方法，子进程的pte（或者父进程的页面）映射到了别处，所以此页面的ref应该减1，此处调用kfree(pa)的目的也就是让ref减1，并没有真正释放内存。

在usertrap()中调用：

```c
else if(r_scause() == 15){
    // COW fork导致的page fault
    if(walkaddr_cow(p->pagetable, r_stval()) == 0)
      p->killed = 1;
  }
```

在copyout()中调用：

```c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr_cow(pagetable, va0); // 改为新函数，处理COW页面的写入
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```
### （6）释放物理内存
调用kfree(pa)时，进行dec_cowref(pa)，共享进程数减1。只当该页面计数为0时，才真正释放页面

```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  //  修改计数，不为0则返回
  if(dec_cowref((uint64)pa))
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

kalloc()初始化的时候计数为1

```c
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    inc_cowref((uint64)r); // 初始化时计数为1
  }
  return (void*)r;
}
```

`freerange()`的修改
这一点很坑人，内核启动时`kinit()`会调用`freerange()`将页面全部`kfree()`一遍，而我们的cow数组一开始都是0，kfree()一遍就全部变成-1了……
所以要先加一遍。

```c
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    inc_cowref((uint64)p); // 防止计数变为-1
    kfree(p);
  }
}
```
