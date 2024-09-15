

## （一）前置知识：锁与缓存区
### （1）锁
- 使用锁的**保守性原则**：两个及以上的进程需要访问一个共享数据结构，且会对之做出修改->使用锁
- 思考锁的方式：
1.锁帮助避免了更新丢失
2.锁可以原子化操作
3.锁维护了不变量（不变量可以改变，但必须在结束前恢复）
- 锁的**内部实现**：关中断(避免死锁)->循环->尝试获取锁(由硬件实现的原子操作，RISC-V中的amoswap指令)->获取成功，开中断
	- 其中还禁止了CPU的指令重排，防止临界区的操作被重排到临界区外：用__sync_synchronize指令设置memory barrier
	- 对于禁用中断，acquire调用push_off，release调用pop_off，用以跟踪当前CPU上锁的嵌套级别，当计数达到零时，pop_off恢复最外层临界区域开始时存在的开中断状态。

### （2）缓存区

![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/f830ef569cb448e7a14626726113e1bd.png#pic_center)
Buffer cache位于文件系统的第二层，主要**功能**如下：
1. 同步对磁盘块的访问，以确保磁盘块在内存中只有一个副本，并且一次只有一个内核线程使用该副本
2. 缓存常用块。缓存区是一个双向环形链表，大小固定，最近使用的块排在链表头部，若空间不够，则LRU策略实现对缓存的替换
 
 
 Buffer cache提供的主要**接口**如下：
 1. bread：
调用bget()获取一个缓存块（已上睡眠锁），其中包含一个可以在内存中读取或修改的块的副本
2. bwrite：
将修改后的缓冲区写入磁盘上的相应块
3. brelse：
释放缓存块与睡眠锁。缓存块插入在缓存区链表头部。
4. bget:
遍历（从前往后）缓存区链表寻找缓存块是否已经存在，存在则上锁，返回该块。不存在则再次遍历（从后往前）寻找空闲块，写入信息，返回。


## （二）Memory allocator
### （1）实验要求
xv6设计中，内核进行内存分配时，使用**全局**的空闲块链表与**单个**锁，故`kalloc`与`kfree`对锁的争用很大。

改进思路：
1. 为每个CPU都分配一个空闲块链表与对应的锁，进程只从**自身CPU**的链表上获取与释放内存块
2. 只当自身CPU空闲块不足时，从其它CPU的链表中**窃取**部分内存块

如此一来，不同的空闲链表中使用不同的锁，CPU对同一把锁的争用就大大减少了，从而提高了性能。

此处也能看出，改进**锁的性能**，往往是通过修改**共享数据结构**，从而减少对同一数据结构的访问。

### （2）实验步骤
**1.修改空闲块链表结构`kmem`**

```c
struct {
  struct spinlock lock;
  struct run *freelist;
  char lockname[8]; // 锁的名称
} kmem[NCPU];
```
改为为每个CPU都分配一个空闲块链表。
同时添加字段：锁的名称。当然，lockname单独拎出来作为全局数组也是可行的

**2.修改初始化函数`kinit()`**
修改后的kinit要做两件事：
1. 调用`initlock`为**每个CPU**的锁都初始化。
主要是为锁命名，此处需调用`snprintf`，将规范格式的字符串写入lockname。
2. 调用`freerange`为空闲块链表分配全部内存
文档中的hints写到：

	> Let freerange give all free memory to the CPU running freerange.

	个人不太理解这里的“**all**”是什么意思，按道理会有多个CPU运行freerange函数，如果不做修改，那么每个CPU都会获得**部分**的内存块。
如果想要获得**全部**内存块，就必须在调用`freerange`前关中断，保证只有一个CPU运行`freerange`函数。
当然，这里关不关中断都行。每个CPU获得**部分**内存块与一个CPU获得**全部**内存块，实测下来效率差距微乎其微，个人采用关中断的写法。

```c
void
kinit()
{
  int i;
  for (i = 0; i < NCPU; i++){
    // 将格式化的数据写入lock-name，并指定最大长度为sizeof(lockname)
    snprintf(kmem[i].lockname, 8, "kmem-%d", i);
    initlock(&kmem[i].lock, kmem[i].lockname);
  }

  push_off();
  freerange(end, (void *)PHYSTOP);
  pop_off();
}
```
**注：**
- 要注意`initlock`函数是对lockname的**浅拷贝**，即`lk->name = name;`。所以切记不要将lock内部定义为局部变量，这样的话等局部变量自动销毁，lk->name也就指向空内存了。这也是在kmem结构体中增加lockname字段的原因。
- `freerange`不需要任何修改，其调用`kfree`，修改已经在`kfree`中了

**3.修改内存块回收函数`kfree()`**
此前是将回收的内存块放入全局空闲块链表中，修改后，每个CPU有单独的空闲块链表了，那就放入运行`kfree`的CPU的链表中即可。
修改方式很简单，调用`cpuid`获取当前CPU标识（记得开关中断），然后将kmem改成kmem[cpuid]即可

```c
void
kfree(void *pa)
{
  struct run *r;
  uint id;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  id = cpuid();
  pop_off();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}
```
**4.修改内存块分配函数`kalloc()`**
除了将kmem换成kmem[cpuid]外，此函数的 **重点** 是：当前CPU空闲块不足时，向其它CPU的空闲链表中偷窃内存块。
构造`void steal(int id)`函数，表示编号为id的CPU向其它CPU中窃取内存块，并将内存块插入到自身空闲块链表的头部。
我们先修改`kalloc`，待会再来讨论`steal`的实现。


```c
void *
kalloc(void)
{
  struct run *r;
  int id;

  push_off();
  id = cpuid();
  pop_off();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // when memory isn't enough,
  // steal memory from other cpu's linked list
  if(!r){
    steal(id); // 窃取内存块并插入链表头部
    acquire(&kmem[id].lock);
    if((r = kmem[id].freelist) != 0)
      kmem[id].freelist = r->next;
    release(&kmem[id].lock);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

我们再来考虑`steal`。起初，个人的`steal`仅仅从其它CPU链表中中窃取**一块**内存块，但借鉴其它大佬思路后，认为窃取**一半**比**一块**要更好，理由如下：
1. 虽然窃取一半内存块比窃取一个慢很多，但是考虑**局部性原理**，内存不足的CPU很有可能进行**多次**窃取，而窃取的过程需要对多个CPU链表上锁，效率很低。所以窃取一半尽可能减少了窃取次数，性能更优。
2. 考虑到`kinit`时，个人将全部内存块都给了单独一个CPU，所以此处窃取一半，能更快让CPU之间的内存块均衡分配
3. 内存块的数量非常之多，只偷一个要偷到何年马月呀（

关于如何窃取一半，就是一个经典的快慢双指针问题了，不会的刷力扣去吧（

此外，还有一点也很重要，我们不是从id=0的CPU开始窃取，而是从缺内存的CPU向后轮询，即编号cpuid的CPU缺内存了，那就窃取编号为cpuid+1、+2的CPU的内存。
理由也很简单，总不能让编号靠前的CPU一直被偷吧，向后偷能尽可能让每个CPU被偷的次数平均，从而也能降低偷窃的次数，提高性能。

```c
// 编号id的cpu向后面的cpu偷窃内存
void
steal(int id){
  struct run *fast, *slow;
  int i, sid;

  for (i = 1; i < NCPU; i++){
    sid = (id + i) % NCPU; // 向后轮询
    acquire(&kmem[sid].lock);
    if(kmem[sid].freelist){
      slow = fast = kmem[sid].freelist;

      // 快慢双指针，找到一半的位置
      while(fast && fast->next){
        slow = slow->next;
        fast = fast->next->next;
      }
      // kmem[id]将kmem[sid]链表的前一半偷走
      kmem[id].freelist = kmem[sid].freelist;
      kmem[sid].freelist = slow->next;
      slow->next = 0;

      release(&kmem[sid].lock);
      break;
    }
    release(&kmem[sid].lock);
  }
}
```

### （3）实验结果
qemu中执行kalloctest，可以看出，锁的争用次数大大减少，kmem中的锁也不再是最具争用性的5个锁。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/2a3a065adcb344559ffc82e7bf14e511.png#pic_center)
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/9aebcea3e8b4454d81b44dc27899416f.png#pic_center)

## （三）Buffer cache
### （1）实验要求
缓存区采用全局的双向链表，配有一把锁，故不同进程访问缓存区时，对锁的争用很大。
同上述内存分配器的思路，依旧可以考虑**拆分链表**，配多把锁，尽可能减少不同进程并行访问同一链表的概率，从而减少锁的争用。

设计思路如下：
1. 采用哈希表（桶+链表）作为缓存区的数据结构，本质上仍然是将大链表拆分成小链表，从而减少访问相同数据结构的概率。每个缓存块根据块号索引到不同的桶中。
2. 每个哈希桶单独配置一把锁，减少争用。
3. 采用普通的链表而非双向链表，通过记录缓存块的**时间戳**来实现LRU策略
4. 当自身桶中无空闲缓冲块时，去别的桶窃取

### （2）实验步骤
**1.buf结构与哈希表结构**
缓存块**buf**的修改：增加时间戳字段，改成单链表。

```c
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint timestamp; // 时间戳
  struct buf *next; // 哈希桶的链表指针
  uchar data[BSIZE];
};
```
bcache中增加**哈希表**结构

```c
struct {
  struct buf buckets[NBUCKETS];       // 哈希表
  struct spinlock buclocks[NBUCKETS]; // 每个哈希表的锁
  char lockname[NBUCKETS][20];        // 哈希表锁的名称
  struct buf buf[NBUF];               // 缓冲区
  struct spinlock lock;               // bcache的锁
} bcache;
```
虽然每个哈希桶都有锁了，但是bcache的全局锁仍然被保留，这是为了避免死锁，后面会解释。

**2.修改`binit()`**
`binit`中初始化每个哈希桶的锁，然后为每个哈希桶分配**等量的缓存区**。
Memory allocator实验中是hints要求将所有内存块分配给一个CPU，这里没有要求了，因此根据个人理解选择了合适的策略：即初始化中为每个哈希桶分配**等量的缓存区**。

```c
void
binit(void)
{
  struct buf *b;
  int i;
  
  // 初始化锁
  for (i = 0; i < NBUCKETS; ++i){
    snprintf(bcache.lockname[i], 20, "bcache.bucket-%d", i);
    initlock(&bcache.buclocks[i], bcache.lockname[i]);
  }

  // 为每个哈希桶分配等量缓冲区
  for (b = bcache.buf; b < bcache.buf + NBUF; b++){
    i = (b - bcache.buf) % NBUCKETS;
    acquire(&bcache.buclocks[i]);
    b->next = bcache.buckets[i].next;
    bcache.buckets[i].next = b;
    release(&bcache.buclocks[i]);
    initsleeplock(&b->lock, "buffer");
  }
}
```

**3.修改`bget()`**
Buffer cache层的核心函数，其基本功能没有修改太多，但是操作的数据结构从双链表变为哈希表了。
函数**流程**如下：
1. 由blockno进行哈希，先索引到对应哈希桶，然后在桶中查找是否已有该块的缓存。
2. 若找到，返回上睡眠锁的缓存块。
3. 若未找到，则再次遍历该桶，查找**可重用**（`refcnt==0`）并且**最久未使用**(`timestamp`最小)的缓存块，
4. 若找到，则写入缓存块信息，然后返回该块。
5. 若仍未命中，则向后轮询：依此查找其它桶，有无空闲且时间戳最小的块。
6. 最终找到该块，然后将该块**移动**到最初的哈希桶中，写入信息，返回。

该函数对共享的哈希表进行了如此多操作，**最关键的一步来了：如何上锁呢？。**
- 首先，容易想到，对每个哈希桶操作前，都要上该哈希桶的锁，操作结束后释放。所以，操作1的前后进行了上锁与解锁。
- 如此引发的问题是，进程1在桶中寻找blockno块未果，释放锁，接着进程2抢到锁，又一次寻找blockno块，未果。于是两个进程接着进入步骤3、5，重复为同一个blockno申请了两个空闲缓冲块。
- 因此，我们会想到，步骤1结束后，不释放该桶的锁，直到某个进程为blockno找到空闲缓存区了，再释放该锁。
- 很可惜，这样**死锁**了。假设进程1给A桶上锁后，去B桶查找空闲块，同时进程2也给B桶上锁了，去A桶请求空闲块，于是二者相互请求对方的锁，但都不释放自己的锁，不出意料地发生死锁。
- 整理一下思路，我们现在要保证**同一时刻不能有两个进程重复地为同一个块找空闲缓存区**，但是，我们也不能通过上**缺块桶**的锁来保证原子性，因为会产生死锁。所以，我们只能上一个**全局锁**，牺牲一定效率来避免死锁，保证同一时刻只有一个进程在寻找缓存区。
- 因此，在步骤1结束后，卸去原先哈希桶的锁，然后上全局锁，进入找空闲缓存区的过程。由于卸去了哈希桶的锁，所以别的进程可能抢到该锁，重复找同一个blockno块，但未果后，它们因为没有全局锁，都会卡在步骤3之前。
- 第一个进程为该桶找到空闲块后，插入到哈希桶的顶部，解除全局锁并返回。此时，其它进程也抢到全局锁进入步骤3了，很显然，它们应该再次遍历自身的哈希桶，找一找自己需要的块，是否已经被人找到了。然后，在执行步骤3的操作。
- 
综上所述，**加锁改进**后的`bget()`流程如下：
函数**流程**如下：
1. 由blockno得到哈希值bucketno，索引到对应哈希桶。为bucketno桶上锁，然后在桶中查找是否已有该块的缓存。
2. 若找到，释放bucketno桶的锁，返回上睡眠锁的缓存块。
3. 若未找到，释放bucketno桶的锁，获取全局锁。 
4. 再次在bucketno桶中查找是否已有该缓冲块。
5. 找到，解除全局锁并返回。
6. 没找到，则从bucketno桶中查找**可重用**（`refcnt==0`）并且**最久未使用**(`timestamp`最小)的缓存块。
7. 若找到，则写入缓存块信息，解除全局锁，然后返回该块。
8. 若仍未命中，则向后轮询：依此查找其它桶（访问其它桶时也要获取其它桶的锁），有无空闲且时间戳最小的块。
9. 最终找到该块，然后将该块**移动**到最初的哈希桶中，写入信息，解除全局锁，返回。

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *cur = 0, *pre = 0;
  uint i, buckectno, sno;

  buckectno = blockno % NBUCKETS; // 哈希
  acquire(&bcache.buclocks[buckectno]);

  // 若命中，返回上锁的缓存区
  for (b = bcache.buckets[buckectno].next; b != 0; b = b->next)
  {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buclocks[buckectno]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  // 未命中，先从自身的桶中寻找有无空闲缓存区
  // 若无，则从其他桶中寻找替换
  // 寻找空闲且时间戳最小的缓存区（LRU）
  release(&bcache.buclocks[buckectno]);
  acquire(&bcache.lock);
  for (i = 0; i < NBUCKETS; ++i)
  {
    sno = (buckectno + i) % NBUCKETS;
    acquire(&bcache.buclocks[sno]);
    
    // 因为之前释放了该桶的锁，所以别的进程可能已经找了缓存区，故先在自己桶中找找缓存区是否已经存在
    if(sno == buckectno){
      for (b = bcache.buckets[buckectno].next; b != 0; b = b->next){
        if (b->dev == dev && b->blockno == blockno){
          b->refcnt++;
          release(&bcache.buclocks[sno]);
          release(&bcache.lock);
          acquiresleep(&b->lock);
          return b;
        }
      }
    }
    
    // 若不存在，则找找自己桶中有无可以复用的缓存区，还找不到，就去其他桶里偷
    for (b = &bcache.buckets[sno]; b->next != 0; b = b->next){
      if(b->next->refcnt == 0 && (!cur || b->next->timestamp < cur->timestamp)){
        pre = b; // 因为要从链表中移除节点，所以记录pre
        cur = b->next;
      }
    }

    if(cur){
      // 更新缓存区信息
      cur->dev = dev;
      cur->blockno = blockno;
      cur->valid = 0;
      cur->refcnt = 1;
      // 将缓存区从原哈希桶的链表中摘下
      pre->next = cur->next;
      release(&bcache.buclocks[sno]);

      // 放到新哈希桶的链表中
      acquire(&bcache.buclocks[buckectno]);
      cur->next = bcache.buckets[buckectno].next;
      bcache.buckets[buckectno].next = cur;     
      release(&bcache.buclocks[buckectno]);

      release(&bcache.lock);
      acquiresleep(&cur->lock);
      return cur;
    }
    release(&bcache.buclocks[sno]);
  }
  panic("bget: no buffers");
}
```
**4.修改`brelse()`**
接下来都是轻松的工作了，由于此处是用时间戳实现LRU策略，所以brelse要做的仅仅是修改释放的缓冲块的时间戳，记录一下最后一次使用的时间。

```c
void
brelse(struct buf *b)
{
  uint bucketno;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.buclocks[bucketno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks; // 更新时间戳
  }
  release(&bcache.buclocks[bucketno]);
}
```
**5.修改`bpin()`与`bunpin()`**
这两个就更简单了，仅仅是修改了一下数据结构，把双链表的锁换成了哈希桶的锁

```c
void
bpin(struct buf *b) {
  uint bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.buclocks[bucketno]);
  b->refcnt++;
  release(&bcache.buclocks[bucketno]);
}

void
bunpin(struct buf *b) {
  uint bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.buclocks[bucketno]);
  b->refcnt--;
  release(&bcache.buclocks[bucketno]);
}
```
### （3）实验结果
bcache锁的争用次数大幅度减少
tot达到了理想值，0
bcache锁也不再是top5的锁。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/68becba361f44a1cbcf35bc8d08a1426.png#pic_center)

